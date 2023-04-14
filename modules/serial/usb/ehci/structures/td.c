/**
 * Copyright 2023, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Enhanced Host Controller Interface Driver
 * TODO:
 * - Power Management
 * - Transaction Translator Support
 */
//#define __TRACE
#define __need_minmax
#include <ddk/utils.h>
#include <os/mollenos.h>
#include "../ehci.h"
#include <stdlib.h>
#include <assert.h>
#include <string.h>

static void
__FillTD(
    _In_ EhciController_t*         controller,
    _In_ EhciTransferDescriptor_t* td,
    _In_ uintptr_t                 dataAddress,
    _In_ size_t                    length)
{
    size_t bytesLeft = length;
    int    i;

    // Sanitize parameters
    if (length == 0 || dataAddress == 0) {
        return;
    }

    // Iterate buffers
    for (i = 0; bytesLeft > 0 && i < 5; i++) {
        uintptr_t address = dataAddress + (i * 0x1000);
        
        // Update buffer
        td->Buffers[i]    = (i == 0) ? address : EHCI_TD_BUFFER(address);
        td->ExtBuffers[i] = 0;
#if __BITS == 64
        if (controller->CParameters & EHCI_CPARAM_64BIT) {
            td->ExtBuffers[i] = EHCI_TD_EXTBUFFER(address);
        }
#endif
        bytesLeft -= MIN(0x1000, bytesLeft);
    }
}

void
EHCITDSetup(
    _In_ EhciController_t*         controller,
    _In_ EhciTransferDescriptor_t* td,
    _In_ uintptr_t                 dataAddress)
{
    // Initialize the transfer-descriptor
    td->Link            = EHCI_LINK_END;
    td->AlternativeLink = EHCI_LINK_END;

    td->Status = EHCI_TD_ACTIVE;
    td->Token  = EHCI_TD_SETUP | EHCI_TD_ERRCOUNT;

    // Calculate the length of the setup transfer
    __FillTD(controller, td, dataAddress, sizeof(usb_packet_t));
    td->Length = (uint16_t)(EHCI_TD_LENGTH(sizeof(usb_packet_t)));

    // Store copies
    td->OriginalLength = td->Length;
    td->OriginalToken  = td->Token;
}

void
EHCITDData(
    _In_ EhciController_t*         controller,
    _In_ EhciTransferDescriptor_t* td,
    _In_ uint8_t                   pid,
    _In_ uintptr_t                 dataAddress,
    _In_ size_t                    length,
    _In_ int                       toggle)
{
    // Initialize the new Td
    td->Link   = EHCI_LINK_END;
    td->Status = EHCI_TD_ACTIVE;
    td->Token  = pid | EHCI_TD_ERRCOUNT;

    // Always stop transaction on short-reads
    if (pid == EHCI_TD_IN) {
        uintptr_t NullTdPhysical = 0;
        UsbSchedulerGetPoolElement(
                controller->Base.Scheduler,
                EHCI_TD_POOL,
                EHCI_TD_NULL,
                NULL,
                &NullTdPhysical
        );
        td->AlternativeLink = LODWORD(NullTdPhysical);
    }

    // Calculate the length of the transfer
    __FillTD(controller, td, dataAddress, length);
    td->Length = (uint16_t)(EHCI_TD_LENGTH(length));
    if (toggle) {
        td->Length |= EHCI_TD_TOGGLE;
    }

    td->OriginalLength = td->Length;
    td->OriginalToken  = td->Token;
}

void
EhciTdDump(
    _In_ EhciController_t*          Controller,
    _In_ EhciTransferDescriptor_t*  Td)
{
    uintptr_t PhysicalAddress;

    UsbSchedulerGetPoolElement(Controller->Base.Scheduler, EHCI_TD_POOL, 
        Td->Object.Index & USB_ELEMENT_INDEX_MASK, NULL, &PhysicalAddress);
    WARNING("EHCI: TD(0x%x), Link(0x%x), AltLink(0x%x), Status(0x%x), Token(0x%x)",
        PhysicalAddress, Td->Link, Td->AlternativeLink, Td->Status, Td->Token);
    WARNING("          Length(0x%x), Buffer0(0x%x:0x%x), Buffer1(0x%x:0x%x)",
        Td->Length, Td->ExtBuffers[0], Td->Buffers[0], Td->ExtBuffers[1], Td->Buffers[1]);
    WARNING("          Buffer2(0x%x:0x%x), Buffer3(0x%x:0x%x), Buffer4(0x%x:0x%x)", 
        Td->ExtBuffers[2], Td->Buffers[2], Td->ExtBuffers[3], Td->Buffers[3], Td->ExtBuffers[4], Td->Buffers[4]);
}

void
EhciTdValidate(
    _In_ UsbManagerTransfer_t*     Transfer,
    _In_ EhciTransferDescriptor_t* Td)
{
    int ErrorCode;
    int i;

    // Sanitize active status
    if (Td->Status & EHCI_TD_ACTIVE) {
        // If this one is still active, but it's an transfer that has
        // elements processed - resync toggles
        if (Transfer->Status != TransferInProgress) {
            Transfer->Flags |= TransferFlagSync;
        }
        return;
    }

    // Get error code based on type of transfer
    ErrorCode = EhciConditionCodeToIndex(
            Transfer->Base.Speed == USB_SPEED_HIGH ? (Td->Status & 0xFC) : Td->Status);
    
    if (ErrorCode != 0) {
        Transfer->Status = EhciGetStatusCode(ErrorCode);
        return; // Skip bytes transferred
    }
    else if (Transfer->Status == TransferInProgress) {
        Transfer->Status = TransferFinished;
    }

    // Add bytes transferred
    if ((Td->OriginalLength & EHCI_TD_LENGTHMASK) != 0) {
        int BytesTransferred = (Td->OriginalLength - Td->Length) & EHCI_TD_LENGTHMASK;
        int BytesRequested   = Td->OriginalLength & EHCI_TD_LENGTHMASK;
        
        if (BytesTransferred < BytesRequested) {
            Transfer->Flags |= TransferFlagShort;

            // On short transfers we might have to sync, but only 
            // if there are un-processed td's after this one
            if (Td->Object.DepthIndex != USB_ELEMENT_NO_INDEX) {
                Transfer->Flags |= TransferFlagSync;
            }
        }
        for (i = 0; i < USB_TRANSACTIONCOUNT; i++) {
            if (Transfer->Base.Transactions[i].Length > Transfer->Transactions[i].BytesTransferred) {
                Transfer->Transactions[i].BytesTransferred += BytesTransferred;
                break;
            }
        }
    }
}

void
EhciTdSynchronize(
    _In_ UsbManagerTransfer_t*     Transfer,
    _In_ EhciTransferDescriptor_t* Td)
{
    int Toggle = UsbManagerGetToggle(Transfer->DeviceID, &Transfer->Base.Address);

    // Is it neccessary?
    if (Toggle == 1 && (Td->Length & EHCI_TD_TOGGLE)) {
        return;
    }

    Td->Length &= ~(EHCI_TD_TOGGLE);
    if (Toggle) {
        Td->Length |= EHCI_TD_TOGGLE;
    }

    // Update copy
    Td->OriginalLength  = Td->Length;
    UsbManagerSetToggle(Transfer->DeviceID, &Transfer->Base.Address, Toggle ^ 1);
}

void
EhciTdRestart(
    _In_ EhciController_t*         Controller,
    _In_ UsbManagerTransfer_t*     Transfer,
    _In_ EhciTransferDescriptor_t* Td)
{
    uintptr_t BufferBaseUpdated = 0;
    uintptr_t BufferBase        = 0;
    uintptr_t BufferStep        = 0;
    int       Toggle            = UsbManagerGetToggle(Transfer->DeviceID, &Transfer->Base.Address);

    Td->OriginalLength &= ~(EHCI_TD_TOGGLE);
    if (Toggle) {
        Td->OriginalLength = EHCI_TD_TOGGLE;
    }
    UsbManagerSetToggle(Transfer->DeviceID, &Transfer->Base.Address, Toggle ^ 1);

    Td->Status = EHCI_TD_ACTIVE;
    Td->Length = Td->OriginalLength;
    Td->Token  = Td->OriginalToken;
    
    // Adjust buffer if not just restart
    if (Transfer->Status != TransferNAK) {
        BufferBaseUpdated = ADDLIMIT(BufferBase, Td->Buffers[0], 
            BufferStep, BufferBase + Transfer->Base.BufferSize);
        __FillTD(Controller, Td, BufferBaseUpdated, BufferStep);
    }
}
