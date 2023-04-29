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
 */

//#define __TRACE
#define __need_minmax
#include <ddk/utils.h>
#include <os/mollenos.h>
#include "../ehci.h"
#include <stdlib.h>

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
    /**
     * When calling these initializers for TDs, the TDs have already been
     * correctly linked, and their links set accordingly. So we do not touch
     * the links, unless they need a specific hardcoded value.
     */
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
    /**
     * When calling these initializers for TDs, the TDs have already been
     * correctly linked, and their links set accordingly. So we do not touch
     * the links, unless they need a specific hardcoded value.
     */

    // Initialize the new Td
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
EHCITDDump(
    _In_ EhciController_t*         controller,
    _In_ EhciTransferDescriptor_t* td)
{
    uintptr_t physicalAddress = 0;

    UsbSchedulerGetPoolElement(
            controller->Base.Scheduler,
            EHCI_TD_POOL,
            td->Object.Index & USB_ELEMENT_INDEX_MASK,
            NULL,
            &physicalAddress
    );
    WARNING("EHCI: TD(0x%x), Link(0x%x), AltLink(0x%x), Status(0x%x), Token(0x%x)",
            physicalAddress, td->Link, td->AlternativeLink, td->Status, td->Token);
    WARNING("          Length(0x%x), Buffer0(0x%x:0x%x), Buffer1(0x%x:0x%x)",
            td->Length, td->ExtBuffers[0], td->Buffers[0], td->ExtBuffers[1], td->Buffers[1]);
    WARNING("          Buffer2(0x%x:0x%x), Buffer3(0x%x:0x%x), Buffer4(0x%x:0x%x)",
            td->ExtBuffers[2], td->Buffers[2], td->ExtBuffers[3], td->Buffers[3], td->ExtBuffers[4], td->Buffers[4]);
}

void
EHCITDVerify(
        _In_ struct HCIProcessReasonScanContext* scanContext,
        _In_ EhciTransferDescriptor_t*           td)
{
    enum USBSpeed speed = scanContext->Transfer->Speed;
    int           cc;

    // Have we already processed this one? In that case we ignore
    // it completely.
    if (td->Object.Flags & USB_ELEMENT_PROCESSED) {
        scanContext->ElementsExecuted++;
        return;
    }

    // If the TD is still active do nothing.
    if (td->Status & EHCI_TD_ACTIVE) {
        return;
    }

    // Get error code based on type of transfer
    cc = EHCIConditionCodeToIndex(speed == USBSPEED_HIGH ? (td->Status & 0xFC) : td->Status);
    if (cc != 0) {
        scanContext->Result = EHCIErrorCodeToTransferStatus(cc);
    }

    if (__Transfer_IsAsync(scanContext->Transfer) && (td->OriginalLength & EHCI_TD_LENGTHMASK) != 0) {
        int bytesTransferred = (td->OriginalLength - td->Length) & EHCI_TD_LENGTHMASK;
        int bytesRequested   = td->OriginalLength & EHCI_TD_LENGTHMASK;
        if (bytesTransferred < bytesRequested) {
            scanContext->Short = true;
        }
        scanContext->BytesTransferred += bytesTransferred;
    }

    // mark TD as processed, and retrieve the data-toggle
    td->Object.Flags |= USB_ELEMENT_PROCESSED;
    scanContext->LastToggle = (td->OriginalLength & EHCI_TD_TOGGLE) ? 1 : 0;
    scanContext->ElementsProcessed++;
    scanContext->ElementsExecuted++;
}

void
EHCITDRestart(
    _In_ EhciController_t*         controller,
    _In_ UsbManagerTransfer_t*     transfer,
    _In_ EhciTransferDescriptor_t* td)
{
    int toggle = UsbManagerGetToggle(&controller->Base, &transfer->Address);

    // Make sure we clear the PROCESSED status on the TD, otherwise it
    // won't be processed again
    td->Object.Flags &= ~(USB_ELEMENT_PROCESSED);

    td->OriginalLength &= ~(EHCI_TD_TOGGLE);
    if (toggle) {
        td->OriginalLength = EHCI_TD_TOGGLE;
    }
    UsbManagerSetToggle(&controller->Base, &transfer->Address, toggle ^ 1);

    td->Status = EHCI_TD_ACTIVE;
    td->Length = td->OriginalLength;
    td->Token  = td->OriginalToken;

    if (transfer->Type == USBTRANSFER_TYPE_INTERRUPT && transfer->ResultCode != USBTRANSFERCODE_NAK) {
        uintptr_t bufferStep = transfer->MaxPacketSize;
        uintptr_t bufferBaseUpdated = ADDLIMIT(
                                              transfer->Elements[0].Data.Address,
                                              td->Buffers[0],
                                              bufferStep,
                                              (transfer->Elements[0].Data.Address + transfer->Elements[0].Length)
        );
        __FillTD(controller, td, bufferBaseUpdated, bufferStep);
    }
}
