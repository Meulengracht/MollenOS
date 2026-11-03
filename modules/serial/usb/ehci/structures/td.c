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
    _In_ EhciController_t*             controller,
    _In_ EhciTransferDescriptor_t*     td,
    _In_ const struct TransferElement* element)
{
    // Use the mapped address for each page, since device segments may not be
    // contiguous. A SETUP packet that crosses a page boundary needs both pages.
    for (int i = 0; i < 5; i++) {
        uintptr_t address = element->Data.EHCI.Lengths[i] != 0 ? element->Data.EHCI.Addresses[i] : 0;
        
        td->Buffers[i] = address;
        td->ExtBuffers[i] = 0;
#if __BITS == 64
        if (controller->CParameters & EHCI_CPARAM_64BIT) {
            td->ExtBuffers[i] = EHCI_TD_EXTBUFFER(address);
        }
#endif
    }
}

void
EHCITDSetup(
    _In_ EhciController_t*             controller,
    _In_ EhciTransferDescriptor_t*     td,
    _In_ const struct TransferElement* element)
{
    // The TD must already be linked. Keep the next link and terminate the
    // alternative link for SETUP.
    td->AlternativeLink = EHCI_LINK_END;

    td->Token  = EHCI_TD_SETUP | EHCI_TD_ERRCOUNT;

    // Set the buffer addresses and SETUP packet length.
    __FillTD(controller, td, element);
    td->Length = (uint16_t)(EHCI_TD_LENGTH(sizeof(usb_packet_t)));

    // Save the length and token for restart.
    td->OriginalLength = td->Length;
    td->OriginalToken  = td->Token;
    dma_mb();
    td->Status = EHCI_TD_ACTIVE;
}

void
EHCITDData(
    _In_ EhciController_t*             controller,
    _In_ EhciTransferDescriptor_t*     td,
    _In_ uint8_t                       pid,
    _In_ const struct TransferElement* element,
    _In_ int                           toggle)
{
    // The TD must already be linked. Only the alternative link for IN
    // transfers is changed here to stop on a short read.

    // Initialize the token.
    td->Token  = pid | EHCI_TD_ERRCOUNT;

    // Stop the transaction on a short read.
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

    // Set the buffer addresses and transfer length.
    __FillTD(controller, td, element);
    td->Length = (uint16_t)(EHCI_TD_LENGTH(element->Length));
    if (toggle) {
        td->Length |= EHCI_TD_TOGGLE;
    }

    td->OriginalLength = td->Length;
    td->OriginalToken  = td->Token;
    dma_mb();
    td->Status = EHCI_TD_ACTIVE;
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

    // Count previously processed TDs without processing their results again.
    if (td->Object.Flags & USB_ELEMENT_PROCESSED) {
        scanContext->ElementsExecuted++;
        return;
    }

    // If the TD is still active, leave its result for a later scan.
    if (td->Status & EHCI_TD_ACTIVE) {
        return;
    }

    // Decode the status according to the device speed.
    cc = EHCIConditionCodeToIndex(speed == USBSPEED_HIGH ? (td->Status & 0xFC) : td->Status);
    if (cc != 0) {
        scanContext->Result = EHCIErrorCodeToTransferStatus(cc);
    }

    if ((td->OriginalLength & EHCI_TD_LENGTHMASK) != 0) {
        int bytesTransferred = (td->OriginalLength - td->Length) & EHCI_TD_LENGTHMASK;
        int bytesRequested   = td->OriginalLength & EHCI_TD_LENGTHMASK;
        if (bytesTransferred < bytesRequested) {
            scanContext->Short = true;
        }
        scanContext->BytesTransferred += bytesTransferred;
    }

    // Mark the TD as processed and record its data toggle.
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

    td->Object.Flags &= ~USB_ELEMENT_PROCESSED;
    td->Length = td->OriginalLength & ~EHCI_TD_TOGGLE;
    if (toggle) {
        td->Length |= EHCI_TD_TOGGLE;
    }
    td->Token = td->OriginalToken;
    UsbManagerSetToggle(&controller->Base, &transfer->Address, toggle ^ 1);
    dma_wmb();
    td->Status = EHCI_TD_ACTIVE;
}
