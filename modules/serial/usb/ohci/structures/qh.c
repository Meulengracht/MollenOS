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

#include <os/mollenos.h>
#include <ddk/utils.h>
#include "../ohci.h"
#include <assert.h>
#include <stddef.h>

static uint16_t
__GetNullIndex(
        _In_ UsbManagerTransfer_t* transfer)
{
    if (transfer->Type == USBTRANSFER_TYPE_ISOC) {
        return USB_ELEMENT_CREATE_INDEX(OHCI_iTD_POOL, OHCI_iTD_NULL);
    }
    return USB_ELEMENT_CREATE_INDEX(OHCI_TD_POOL, OHCI_TD_NULL);
}

oserr_t
OHCIQHInitialize(
        _In_ OhciController_t*     controller,
        _In_ UsbManagerTransfer_t* transfer,
        _In_ OhciQueueHead_t*      qh)
{
    uint16_t  lastIndex = __GetNullIndex(transfer);
    uintptr_t linkEnd;
    oserr_t   oserr;
    TRACE("OHCIQHInitialize()");

    oserr = UsbSchedulerGetPoolElement(
            controller->Base.Scheduler,
            (lastIndex >> USB_ELEMENT_POOL_SHIFT) & USB_ELEMENT_POOL_MASK,
            lastIndex & USB_ELEMENT_INDEX_MASK,
            NULL,
            &linkEnd
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    qh->LinkPointer       = 0;
    qh->Current           = (reg32_t)linkEnd;
    qh->EndPointer        = (reg32_t)linkEnd;
    qh->Object.DepthIndex = lastIndex;

    qh->Flags = OHCI_QH_ADDRESS(transfer->Address.DeviceAddress);
    qh->Flags |= OHCI_QH_ENDPOINT(transfer->Address.EndpointAddress);
    qh->Flags |= OHCI_QH_DIRECTIONTD; // Retrieve from TD
    qh->Flags |= OHCI_QH_LENGTH(transfer->MaxPacketSize);
    qh->Flags |= OHCI_QH_TYPE(transfer->Type);

    if (transfer->Speed == USBSPEED_LOW) {
        qh->Flags |= OHCI_QH_LOWSPEED;
    }
    if (transfer->Type == USBTRANSFER_TYPE_ISOC) {
        qh->Flags |= OHCI_QH_ISOCHRONOUS;
    }

    // Allocate bandwidth if int/isoc
    if (__Transfer_IsPeriodic(transfer)) {
        oserr = UsbSchedulerAllocateBandwidth(
                controller->Base.Scheduler,
                transfer->TData.Periodic.Interval,
                transfer->MaxPacketSize,
                __Transfer_Direction(transfer),
                __Transfer_Length(transfer),
                transfer->Type,
                transfer->Speed,
                (uint8_t*)qh
        );
    }
    return oserr;
}

void
OHCIQHDump(
    _In_ OhciController_t* Controller,
    _In_ OhciQueueHead_t*  Qh)
{
    uintptr_t PhysicalAddress = 0;

    UsbSchedulerGetPoolElement(Controller->Base.Scheduler, OHCI_QH_POOL, 
        Qh->Object.Index & USB_ELEMENT_INDEX_MASK, NULL, &PhysicalAddress);
    WARNING("QH(0x%x): Flags 0x%x, Link 0x%x, Current 0x%x, End 0x%x", 
        PhysicalAddress, Qh->Object.Flags, Qh->LinkPointer, Qh->Current, Qh->EndPointer);
}

void
OHCIQHRestart(
    _In_ OhciController_t*     Controller,
    _In_ UsbManagerTransfer_t* Transfer)
{
    OhciQueueHead_t* Qh          = (OhciQueueHead_t*)Transfer->RootElement;
    uintptr_t        LinkAddress = 0;

    UsbSchedulerGetPoolElement(Controller->Base.Scheduler,
        (Qh->Object.DepthIndex >> USB_ELEMENT_POOL_SHIFT) & USB_ELEMENT_POOL_MASK, 
        Qh->Object.DepthIndex & USB_ELEMENT_INDEX_MASK, NULL, &LinkAddress);
    Qh->Current = LinkAddress;
    assert(Qh->Current != 0);
}

void
OHCIQHLink(
    _In_ OhciController_t* controller,
    _In_ uint8_t           type,
    _In_ OhciQueueHead_t*  qh)
{
    OhciQueueHead_t* rootQH = NULL;
    reg32_t          commandStatus = READ_VOLATILE(controller->Registers->HcCommandStatus);

    // Switch based on type of transfer
    if (type == USBTRANSFER_TYPE_CONTROL) {
        // Is there anyone waiting?
        if (controller->TransactionsWaitingControl > 0) {
            UsbSchedulerGetPoolElement(controller->Base.Scheduler, OHCI_QH_POOL,
                                       controller->TransactionQueueControlIndex & USB_ELEMENT_INDEX_MASK,
                                       (uint8_t**)&rootQH, NULL);
            UsbSchedulerChainElement(controller->Base.Scheduler, OHCI_QH_POOL, (uint8_t*)rootQH,
                                     OHCI_QH_POOL, (uint8_t*)qh, 0, USB_CHAIN_BREATH);
        }
        else {
            controller->TransactionQueueControlIndex = qh->Object.Index;
        }
        controller->TransactionsWaitingControl++;

        // Enable?
        if (!(commandStatus & OHCI_COMMAND_CONTROL_FILLED)) {
            OHCIReloadAsynchronous(controller, USBTRANSFER_TYPE_CONTROL);
            controller->QueuesActive |= OHCI_CONTROL_CONTROL_ACTIVE;
        }
    } else if (type == USBTRANSFER_TYPE_BULK) {
        // Is there anyone waiting?
        if (controller->TransactionsWaitingBulk > 0) {
            UsbSchedulerGetPoolElement(controller->Base.Scheduler, OHCI_QH_POOL,
                                       controller->TransactionQueueBulkIndex & USB_ELEMENT_INDEX_MASK,
                                       (uint8_t**)&rootQH, NULL);
            UsbSchedulerChainElement(controller->Base.Scheduler, OHCI_QH_POOL, (uint8_t*)rootQH,
                                     OHCI_QH_POOL, (uint8_t*)qh, 0, USB_CHAIN_BREATH);
        } else {
            controller->TransactionQueueBulkIndex = qh->Object.Index;
        }
        controller->TransactionsWaitingBulk++;

        // Enable?
        if (!(commandStatus & OHCI_COMMAND_BULK_FILLED)) {
            OHCIReloadAsynchronous(controller, USBTRANSFER_TYPE_BULK);
            controller->QueuesActive |= OHCI_CONTROL_BULK_ACTIVE;
        }
    }
}
