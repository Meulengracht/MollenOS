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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Universal Host Controller Interface Driver
 * TODO:
 *    - Power Management
 */

//#define __TRACE

#include <ddk/utils.h>
#include "../uhci.h"

static void
__CalculateFrameQueue(
    _In_ UhciQueueHead_t* queueHead)
{
    int exponent, queue;

    // Select the largest power-of-two interval that fits FrameInterval.
    for (exponent = 7; exponent >= 0; --exponent) {
        if ((1 << exponent) <= (int)queueHead->Object.FrameInterval) {
            break;
        }
    }

    queue = 9 - exponent;
    queueHead->Queue = queue & 0xFF;
    TRACE("Queue for interrupt transfer: %i", queue);
}

oserr_t
UHCIQHInitialize(
        _In_ UhciController_t*     controller,
        _In_ UsbManagerTransfer_t* transfer,
        _In_ UhciQueueHead_t*       queueHead)
{
    queueHead->Link  = UHCI_LINK_END;
    queueHead->Child = UHCI_LINK_END;
    queueHead->Object.Flags |= UHCI_LINK_QH;

    // Periodic transfers need bandwidth allocated before selecting a frame queue.
    if (__Transfer_IsPeriodic(transfer)) {
        oserr_t oserr = UsbSchedulerAllocateBandwidth(
                controller->Base.Scheduler,
                transfer->TData.Periodic.Interval,
                transfer->MaxPacketSize,
                __Transfer_Direction(transfer),
                __Transfer_Length(transfer),
                transfer->Type,
                transfer->Speed,
                (uint8_t*)queueHead
        );
        if (oserr != OS_EOK) {
            return oserr;
        }
        __CalculateFrameQueue(queueHead);
    } else {
        queueHead->Queue = UHCI_POOL_QH_ASYNC;
    }
    return OS_EOK;
}

void
UHCIQHDump(
    _In_ UhciController_t* controller,
    _In_ UhciQueueHead_t*  queueHead)
{
    uintptr_t PhysicalAddress = 0;

    UsbSchedulerGetPoolElement(controller->Base.Scheduler, UHCI_QH_POOL,
                               queueHead->Object.Index & USB_ELEMENT_INDEX_MASK,
                               NULL, &PhysicalAddress);
    WARNING("QH(0x%x): Flags 0x%x, Link 0x%x, Child 0x%x",
            PhysicalAddress, queueHead->Object.Flags, queueHead->Link, queueHead->Child);
}

void
UHCIQHRestart(
    _In_ UhciController_t*     controller,
    _In_ UsbManagerTransfer_t* transfer)
{
    UhciQueueHead_t* queueHead    = (UhciQueueHead_t*)transfer->RootElement;
    uintptr_t        linkPhysical = 0;

    // Restart at the first transfer descriptor.
    UsbSchedulerGetPoolElement(controller->Base.Scheduler, UHCI_TD_POOL,
                               queueHead->Object.DepthIndex & USB_ELEMENT_INDEX_MASK,
                               NULL, &linkPhysical);
    // Descriptor writes must be visible before the controller follows Child.
    dma_wmb();
    queueHead->Child = linkPhysical | UHCI_LINK_DEPTH;
}

void
UHCIQHLink(
    _In_ UhciController_t* Controller,
    _In_ UhciQueueHead_t*  queueHead)
{
    UhciQueueHead_t* listQh = NULL;
    uint16_t         marker;

    // Find the list head for the selected queue.
    UsbSchedulerGetPoolElement(Controller->Base.Scheduler, UHCI_QH_POOL,
                               queueHead->Queue, (uint8_t**)&listQh, NULL);

    // Interrupt queue insertion stops at the async queue marker.
    if (queueHead->Queue >= UHCI_POOL_QH_ASYNC) {
        marker = USB_ELEMENT_NO_INDEX;
    }
    else {
        marker = USB_ELEMENT_CREATE_INDEX(UHCI_QH_POOL, UHCI_POOL_QH_ASYNC);
    }
    
    // Insert the queue head into the horizontal chain.
    UsbSchedulerChainElement(Controller->Base.Scheduler, UHCI_QH_POOL,
                             (uint8_t*)listQh, UHCI_QH_POOL, (uint8_t*)queueHead, marker, USB_CHAIN_BREATH);
}

void
UHCIQHUnlink(
    _In_ UhciController_t* controller,
    _In_ UhciQueueHead_t*  queueHead)
{
    UhciQueueHead_t* listQh = NULL;

    // Find the list head for the selected queue.
    UsbSchedulerGetPoolElement(controller->Base.Scheduler, UHCI_QH_POOL,
                               queueHead->Queue, (uint8_t**)&listQh, NULL);

    // Remove the queue head from the horizontal chain.
    UsbSchedulerUnchainElement(controller->Base.Scheduler,
                               UHCI_QH_POOL, (uint8_t*)listQh, UHCI_QH_POOL,
                               (uint8_t*)queueHead, USB_CHAIN_BREATH);
}
