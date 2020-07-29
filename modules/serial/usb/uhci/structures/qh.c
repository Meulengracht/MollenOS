/**
 * MollenOS
 *
 * Copyright 2016, Philip Meulengracht
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
 * Universal Host Controller Interface Driver
 * TODO:
 *    - Power Management
 */
//#define __TRACE

#include <ddk/utils.h>
#include "../uhci.h"

void
UhciQhCalculateQueue(
    _In_ UhciQueueHead_t* Qh)
{
    int Exponent, Queue;

    // Calculate the correct index
    for (Exponent = 7; Exponent >= 0; --Exponent) {
        if ((1 << Exponent) <= (int)Qh->Object.FrameInterval) {
            break;
        }
    }

    Queue     = 9 - Exponent;
    Qh->Queue = Queue & 0xFF;
    TRACE("Queue for interrupt transfer: %i", Queue);
}

OsStatus_t
UhciQhInitialize(
    _In_ UhciController_t*     Controller,
    _In_ UsbManagerTransfer_t* Transfer)
{
    UhciQueueHead_t* Qh     = (UhciQueueHead_t*)Transfer->EndpointDescriptor;
    OsStatus_t       Status = OsSuccess;

    Qh->Link          = UHCI_LINK_END;
    Qh->Child         = UHCI_LINK_END;
    Qh->Object.Flags |= UHCI_LINK_QH;

    // Allocate bandwidth if int/isoc
    if (Transfer->Transfer.Type == USB_TRANSFER_INTERRUPT || Transfer->Transfer.Type == USB_TRANSFER_ISOCHRONOUS) {
        Status = UsbSchedulerAllocateBandwidth(Controller->Base.Scheduler, 
            Transfer->Transfer.PeriodicInterval, Transfer->Transfer.Transactions[0].Type,
            Transfer->Transfer.MaxPacketSize, Transfer->Transfer.Transactions[0].Length,
            Transfer->Transfer.Type, Transfer->Transfer.Speed, (uint8_t*)Qh);
        if (Status != OsError) {
            UhciQhCalculateQueue(Qh);
        }
    }
    else {
        Qh->Queue = UHCI_POOL_QH_ASYNC;
    }
    return Status;
}

void
UhciQhDump(
    _In_ UhciController_t* Controller,
    _In_ UhciQueueHead_t*  Qh)
{
    uintptr_t PhysicalAddress = 0;

    UsbSchedulerGetPoolElement(Controller->Base.Scheduler, UHCI_QH_POOL, 
        Qh->Object.Index & USB_ELEMENT_INDEX_MASK, NULL, &PhysicalAddress);
    WARNING("QH(0x%x): Flags 0x%x, Link 0x%x, Child 0x%x", 
        PhysicalAddress, Qh->Object.Flags, Qh->Link, Qh->Child);
}

void
UhciQhRestart(
    _In_ UhciController_t*     Controller,
    _In_ UsbManagerTransfer_t* Transfer)
{
    UhciQueueHead_t* Qh           = (UhciQueueHead_t*)Transfer->EndpointDescriptor;
    uintptr_t        LinkPhysical = 0;
    
    // Do some extra processing for periodics
    Qh->BufferBase  = Transfer->Transactions[0].DmaTable.entries[
        Transfer->Transactions[0].SgIndex].address;
    Qh->BufferBase += Transfer->Transactions[0].SgOffset;

    // Reinitialize the queue-head
    UsbSchedulerGetPoolElement(Controller->Base.Scheduler, UHCI_TD_POOL, 
        Qh->Object.DepthIndex & USB_ELEMENT_INDEX_MASK, NULL, &LinkPhysical);
    Qh->Child = LinkPhysical | UHCI_LINK_DEPTH;
}

void
UhciQhLink(
    _In_ UhciController_t* Controller,
    _In_ UhciQueueHead_t*  Qh)
{
    UhciQueueHead_t* QueueQh = NULL;
    uint16_t         Marker;

    // Get the queue for this queue-head
    UsbSchedulerGetPoolElement(Controller->Base.Scheduler, UHCI_QH_POOL, 
        Qh->Queue, (uint8_t**)&QueueQh, NULL);

    // Handle async and interrupt a little differently
    if (Qh->Queue >= UHCI_POOL_QH_ASYNC) {
        Marker = USB_ELEMENT_NO_INDEX;
    }
    else {
        Marker = USB_ELEMENT_CREATE_INDEX(UHCI_QH_POOL, UHCI_POOL_QH_ASYNC);
    }
    
    // Link it
    UsbSchedulerChainElement(Controller->Base.Scheduler, UHCI_QH_POOL,
        (uint8_t*)QueueQh, UHCI_QH_POOL, (uint8_t*)Qh, Marker, USB_CHAIN_BREATH);
}

void
UhciQhUnlink(
    _In_ UhciController_t* Controller,
    _In_ UhciQueueHead_t*  Qh)
{
    UhciQueueHead_t* QueueQh = NULL;

    // Get the queue for this queue-head
    UsbSchedulerGetPoolElement(Controller->Base.Scheduler, UHCI_QH_POOL, 
        Qh->Queue, (uint8_t**)&QueueQh, NULL);

    // Unlink it
    UsbSchedulerUnchainElement(Controller->Base.Scheduler, 
        UHCI_QH_POOL, (uint8_t*)QueueQh, UHCI_QH_POOL, (uint8_t*)Qh, USB_CHAIN_BREATH);
}
