/**
 * MollenOS
 *
 * Copyright 2018, Philip Meulengracht
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Enhanced Host Controller Interface Driver
 * TODO:
 * - Power Management
 * - Transaction Translator Support
 */
//#define __TRACE

#include <os/mollenos.h>
#include <ddk/utils.h>
#include "../ehci.h"
#include <stdlib.h>
#include <assert.h>
#include <string.h>

size_t
EhciTdFill(
    _In_ EhciController_t*         Controller,
    _In_ EhciTransferDescriptor_t* Td,
    _In_ uintptr_t                 BufferAddress, 
    _In_ size_t                    Length)
{
    size_t LengthRemaining  = Length;
    size_t Count            = 0;
    int    i;

    // Sanitize parameters
    if (Length == 0 || BufferAddress == 0) {
        return 0;
    }

    // Iterate buffers
    for (i = 0; LengthRemaining > 0 && i < 5; i++) {
        uintptr_t Physical = BufferAddress + (i * 0x1000);
        
        // Update buffer
        Td->Buffers[i]    = (i == 0) ? Physical : EHCI_TD_BUFFER(Physical);
        Td->ExtBuffers[i] = 0;
#if __BITS == 64
        if (Controller->CParameters & EHCI_CPARAM_64BIT) {
            Td->ExtBuffers[i] = EHCI_TD_EXTBUFFER(Physical);
        }
#endif

        // Update iterators
        Count           += MIN(0x1000, LengthRemaining);
        LengthRemaining -= MIN(0x1000, LengthRemaining);
    }
    return Count; // Return how many bytes were "buffered"
}

size_t
EhciTdSetup(
    _In_ EhciController_t*         Controller,
    _In_ EhciTransferDescriptor_t* Td,
    _In_ uintptr_t                 Address,
    _In_ size_t                    Length)
{
    size_t CalculatedLength;

    // Initialize the transfer-descriptor
    Td->Link            = EHCI_LINK_END;
    Td->AlternativeLink = EHCI_LINK_END;

    Td->Status = EHCI_TD_ACTIVE;
    Td->Token  = EHCI_TD_SETUP | EHCI_TD_ERRCOUNT;

    // Calculate the length of the setup transfer
    CalculatedLength = EhciTdFill(Controller, Td, Address, Length);
    Td->Length       = (uint16_t)(EHCI_TD_LENGTH(CalculatedLength));

    // Store copies
    Td->OriginalLength = Td->Length;
    Td->OriginalToken  = Td->Token;
    return CalculatedLength;
}

size_t
EhciTdIo(
    _In_ EhciController_t*         Controller,
    _In_ EhciTransferDescriptor_t* Td,
    _In_ size_t                    MaxPacketSize,
    _In_ uint8_t                   transactionType,
    _In_ uintptr_t                 Address,
    _In_ size_t                    Length,
    _In_ int                       Toggle)
{
    uintptr_t NullTdPhysical   = 0;
    uint8_t   PId              = (transactionType == USB_TRANSACTION_IN) ? EHCI_TD_IN : EHCI_TD_OUT;
    size_t    CalculatedLength = 0;

    // Initialize the new Td
    Td->Link   = EHCI_LINK_END;
    Td->Status = EHCI_TD_ACTIVE;
    Td->Token  = PId | EHCI_TD_ERRCOUNT;

    // Always stop transaction on short-reads
    if (PId == EHCI_TD_IN) {
        UsbSchedulerGetPoolElement(Controller->Base.Scheduler, EHCI_TD_POOL,
            EHCI_TD_NULL, NULL, &NullTdPhysical);
        Td->AlternativeLink = LODWORD(NullTdPhysical);
    }

    // Calculate the length of the transfer
    CalculatedLength = EhciTdFill(Controller, Td, Address, Length);
    Td->Length       = (uint16_t)(EHCI_TD_LENGTH(CalculatedLength));
    if (Toggle) {
        Td->Length |= EHCI_TD_TOGGLE;
    }

    // Calculate next toggle 
    // if transaction spans multiple transfers
    if (Length > 0 && !(DIVUP(Length, MaxPacketSize) % 2)) {
        Toggle ^= 0x1;
    }
    Td->OriginalLength = Td->Length;
    Td->OriginalToken  = Td->Token;
    return CalculatedLength;
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
        Transfer->Transfer.Speed == USB_SPEED_HIGH ? (Td->Status & 0xFC) : Td->Status);
    
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
            if (Transfer->Transfer.Transactions[i].Length > Transfer->Transactions[i].BytesTransferred) {
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
    int Toggle = UsbManagerGetToggle(Transfer->DeviceId, &Transfer->Transfer.Address);

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
    UsbManagerSetToggle(Transfer->DeviceId, &Transfer->Transfer.Address, Toggle ^ 1);
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
    int       Toggle            = UsbManagerGetToggle(Transfer->DeviceId, &Transfer->Transfer.Address);

    Td->OriginalLength &= ~(EHCI_TD_TOGGLE);
    if (Toggle) {
        Td->OriginalLength = EHCI_TD_TOGGLE;
    }
    UsbManagerSetToggle(Transfer->DeviceId, &Transfer->Transfer.Address, Toggle ^ 1);

    Td->Status = EHCI_TD_ACTIVE;
    Td->Length = Td->OriginalLength;
    Td->Token  = Td->OriginalToken;
    
    // Adjust buffer if not just restart
    if (Transfer->Status != TransferNAK) {
        BufferBaseUpdated = ADDLIMIT(BufferBase, Td->Buffers[0], 
            BufferStep, BufferBase + Transfer->Transfer.PeriodicBufferSize);
        EhciTdFill(Controller, Td, BufferBaseUpdated, BufferStep);
    }
}
