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
#include "../uhci.h"
#include <stdlib.h>

void
UHCITDSetup(
        _In_ UhciTransferDescriptor_t* td,
        _In_ uint32_t                  device,
        _In_ uint32_t                  endpoint,
        _In_ enum USBSpeed             speed,
        _In_ uintptr_t                 address)
{
    reg32_t flags;

    // The TD must already be linked. Leave its links unchanged.

    flags  = UHCI_TD_ACTIVE;
    flags |= UHCI_TD_SETCOUNT(3);
    if (speed == USBSPEED_LOW) {
        flags |= UHCI_TD_LOWSPEED;
    }

    // Setup td header
    td->Header  = UHCI_TD_PID_SETUP;
    td->Header |= UHCI_TD_DEVICE_ADDR(device);
    td->Header |= UHCI_TD_EP_ADDR(endpoint);
    td->Header |= UHCI_TD_MAX_LEN(sizeof(usb_packet_t) - 1);

    td->Buffer = LODWORD(address);

    // Save the flags and header for restarting the TD.
    td->OriginalFlags  = flags;
    td->OriginalHeader = td->Header;
    
    // Set usb scheduler link info
    td->Object.Flags |= UHCI_LINK_DEPTH;

    // The descriptor fields must be visible to the controller before ACTIVE is set.
    dma_wmb();
    td->Flags = flags;
}

void
UHCITDData(
    _In_ UhciTransferDescriptor_t* td,
    _In_ enum USBTransferType      type,
    _In_ uint32_t                  pid,
    _In_ uint32_t                  device,
    _In_ uint32_t                  endpoint,
    _In_ enum USBSpeed             speed,
    _In_ uintptr_t                 address,
    _In_ uint32_t                  length,
    _In_ int                       toggle)
{
    reg32_t flags;

    // The TD must already be linked. Leave its links unchanged.

    // Setup td flags
    flags  = UHCI_TD_ACTIVE;
    flags |= UHCI_TD_SETCOUNT(3);
    if (speed == USBSPEED_LOW) {
        flags |= UHCI_TD_LOWSPEED;
    }
    if (type == USBTRANSFER_TYPE_ISOC) {
        flags |= UHCI_TD_ISOCHRONOUS;
    }

    // Enable short packet detection for IN transfers, except for zero-length
    // control packets.
    if (type == USBTRANSFER_TYPE_CONTROL) {
        if (pid == UHCI_TD_PID_IN && length > 0) {
            flags |= UHCI_TD_SHORT_PACKET;
        }
    }
    else if (pid == UHCI_TD_PID_IN) {
        flags |= UHCI_TD_SHORT_PACKET;
    }

    if (type == USBTRANSFER_TYPE_ISOC || type == USBTRANSFER_TYPE_INTERRUPT) {
        flags |= UHCI_TD_IOC;
    }

    // Setup td header
    td->Header  = pid;
    td->Header |= UHCI_TD_DEVICE_ADDR(device);
    td->Header |= UHCI_TD_EP_ADDR(endpoint);
    if (toggle) {
        td->Header |= UHCI_TD_DATA_TOGGLE;
    }

    // Setup size
    td->Header |= UHCI_TD_MAX_LEN(length - 1);

    // Store buffer
    td->Buffer = LODWORD(address);

    // Save the flags and header for restarting the TD.
    td->OriginalFlags  = flags;
    td->OriginalHeader = td->Header;

    // Set usb scheduler link info
    td->Object.Flags |= UHCI_LINK_DEPTH;

    // Isochronous TDs stay linked between packets. The descriptor fields must
    // be visible to the controller before ACTIVE is set.
    dma_wmb();
    td->Flags = flags;
}

void
UHCITDDump(
    _In_ UhciController_t*         Controller,
    _In_ UhciTransferDescriptor_t* Td)
{
    uintptr_t PhysicalAddress   = 0;

    UsbSchedulerGetPoolElement(Controller->Base.Scheduler, UHCI_TD_POOL, 
        Td->Object.Index & USB_ELEMENT_INDEX_MASK, NULL, &PhysicalAddress);
    WARNING("TD(0x%x): Link 0x%x, Flags 0x%x, Header 0x%x, Buffer 0x%x", 
        PhysicalAddress, Td->Link, Td->Flags, Td->Header, Td->Buffer);
}

void
UHCITDVerify(
        _In_ struct HCIProcessReasonScanContext* context,
        _In_ UhciTransferDescriptor_t*           td)
{
    int conditionCodeIndex;

    // Count previously processed TDs without adding their transferred bytes again.
    if (td->Object.Flags & USB_ELEMENT_PROCESSED) {
        context->ElementsExecuted++;
        return;
    }

    conditionCodeIndex = UhciConditionCodeToIndex(UHCI_TD_STATUS(td->Flags));

    // If the TD is still active do nothing.
    if (td->Flags & UHCI_TD_ACTIVE) {
        return;
    }

    if (conditionCodeIndex != 0) {
        context->Result = UHCIErrorCodeToTransferStatus(conditionCodeIndex);
        return; // Skip bytes transferred
    }

    // Length fields encode N-1. Mask after adding one so zero-length packets
    // contribute no bytes.
    if (td->Buffer != 0) {
        int bytesTransferred = (UHCI_TD_ACTUALLENGTH(td->Flags) + 1) & UHCI_TD_LENGTH_MASK;
        int bytesRequested   = (UHCI_TD_GET_LEN(td->Header) + 1) & UHCI_TD_LENGTH_MASK;
        if (bytesTransferred < bytesRequested) {
            context->Short = true;
        }
        context->BytesTransferred += bytesTransferred;
    }

    // Mark the TD as processed and record its data toggle.
    td->Object.Flags |= USB_ELEMENT_PROCESSED;
    context->LastToggle = (td->Flags & UHCI_TD_DATA_TOGGLE) ? 1 : 0;
    context->ElementsProcessed++;
    context->ElementsExecuted++;
}

void
UHCITDRestart(
        _In_ UhciController_t*         controller,
        _In_ UsbManagerTransfer_t*     transfer,
        _In_ UhciTransferDescriptor_t* td)
{
    int toggle = UsbManagerGetToggle(&controller->Base, &transfer->Address);

    // Clear PROCESSED so the next completion can be counted.
    td->Object.Flags &= ~(USB_ELEMENT_PROCESSED);

    td->OriginalFlags &= ~(UHCI_TD_DATA_TOGGLE);
    if (toggle) {
        td->OriginalFlags |= UHCI_TD_DATA_TOGGLE;
    }
    UsbManagerSetToggle(&controller->Base, &transfer->Address, toggle ^ 1);

    if (transfer->Type == USBTRANSFER_TYPE_INTERRUPT && transfer->ResultCode != USBTRANSFERCODE_NAK) {
        uintptr_t bufferStep = transfer->MaxPacketSize;
        uintptr_t bufferBaseUpdated = ADDLIMIT(
                transfer->Elements[0].Data.Address,
                td->Buffer,
                bufferStep,
                (transfer->Elements[0].Data.Address + transfer->Elements[0].Length)
        );
        td->Buffer = LODWORD(bufferBaseUpdated);
    }
    
    td->Header = td->OriginalHeader;
    td->Flags  = td->OriginalFlags;
}
