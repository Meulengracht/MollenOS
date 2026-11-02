/**
 * Copyright, Philip Meulengracht
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

#include <ddk/interrupts/interrupt.h>
#include <ddk/io.h>
#include <ddk/utils.h>
#include <stdlib.h>
#include "xhci.h"

static uintptr_t
__EventRingDequeueAddress(
        _In_ XhciController_t* controller)
{
    return controller->EventRingDeviceAddress +
           (controller->EventRingIndex * sizeof(XhciTransferRequestBlock_t));
}

static void
__ProcessEventRing(
        _In_ XhciController_t* controller)
{
    int portEvent = 0;
    int transferEvent = 0;

    while ((controller->EventRing[controller->EventRingIndex].Control & XHCI_TRB_CONTROL_CYCLE) ==
           (controller->EventRingCycle ? XHCI_TRB_CONTROL_CYCLE : 0)) {
        XhciTransferRequestBlock_t* trb = &controller->EventRing[controller->EventRingIndex];

        dma_rmb();

        // Port status events carry the changed port id in the event TRB. The
        // common stack only needs a root-hub port notification, so we let the
        // port code rescan PORTSC to keep change-bit handling in one place.
        switch (XHCI_TRB_CONTROL_TYPE_GET(trb->Control)) {
            case XHCI_TRB_TYPE_TRANSFER_EVENT:
                transferEvent |= XhciTransferHandleEvent(controller, trb);
                break;
            case XHCI_TRB_TYPE_COMMAND_COMPLETION:
                XhciCommandHandleCompletion(controller, trb);
                transferEvent = 1;
                break;
            case XHCI_TRB_TYPE_PORT_STATUS_CHANGE:
                portEvent = 1;
                break;
            default:
                break;
        }

        controller->EventRingIndex++;
        if (controller->EventRingIndex == XHCI_EVENT_RING_ENTRIES) {
            controller->EventRingIndex = 0;
            controller->EventRingCycle = !controller->EventRingCycle;
        }
    }

    WRITE_VOLATILE(controller->InterrupterRegisters->EventRingDequeuePointer,
            __EventRingDequeueAddress(controller) | XHCI_INTERRUPTER_ERDP_BUSY);

    if (portEvent) {
        bool initial = false;
        XhciPortScan(controller, initial);
    }
    if (transferEvent) {
        UsbManagerProcessTransfers(&controller->Base);
    }
}

irqstatus_t
OnFastInterrupt(
        _In_ InterruptFunctionTable_t* InterruptTable,
        _In_ InterruptResourceTable_t* ResourceTable)
{
    XhciController_t* controller = (XhciController_t*)INTERRUPT_RESOURCE(ResourceTable, 0);
    uintptr_t         registerBase = INTERRUPT_IOSPACE(ResourceTable, 0)->Access.Memory.VirtualBase;
    XhciCapabilityRegisters_t* capRegisters = (XhciCapabilityRegisters_t*)registerBase;
    XhciOperationalRegisters_t* opRegisters = (XhciOperationalRegisters_t*)(registerBase + capRegisters->CapLength);
    XhciRuntimeRegisters_t* runtimeRegisters = (XhciRuntimeRegisters_t*)(registerBase + (capRegisters->RuntimeOffset & ~0x1FUL));
    XhciInterrupterRegisters_t* interrupterRegisters = (XhciInterrupterRegisters_t*)((uintptr_t)runtimeRegisters + 0x20);
    reg32_t status = opRegisters->UsbStatus;
    reg32_t management = interrupterRegisters->Management;

    if (!(status & (XHCI_OP_USBSTS_EVENT | XHCI_OP_USBSTS_HOSTERROR)) &&
        !(management & XHCI_INTERRUPTER_MANAGEMENT_PENDING)) {
        return IRQSTATUS_NOT_HANDLED;
    }

    // An event can arrive between the USBSTS and IMAN reads on a shared IRQ.
    // IP alone must schedule a drain so the consumer releases event-handler busy.
    if (management & XHCI_INTERRUPTER_MANAGEMENT_PENDING) {
        status |= XHCI_OP_USBSTS_EVENT;
    }

    // Fast handlers run from a kernel mapping and cannot call userspace helpers.
    // These register members are volatile; order acknowledgement before signalling.
    opRegisters->UsbStatus = status & (XHCI_OP_USBSTS_EVENT | XHCI_OP_USBSTS_HOSTERROR);
    interrupterRegisters->Management = management | XHCI_INTERRUPTER_MANAGEMENT_PENDING;
    mb();
    atomic_fetch_or(&controller->Base.InterruptStatus, status);

    InterruptTable->EventSignal(ResourceTable->HandleResource);
    return IRQSTATUS_HANDLED;
}

void
HciInterruptCallback(
        _In_ UsbManagerController_t* baseController)
{
    XhciController_t* controller = (XhciController_t*)baseController;
    reg32_t           interruptStatus;

ProcessInterrupt:
    interruptStatus = atomic_exchange(&controller->Base.InterruptStatus, 0);

    if (interruptStatus & XHCI_OP_USBSTS_EVENT) {
        __ProcessEventRing(controller);
    }

    if (interruptStatus & XHCI_OP_USBSTS_HOSTERROR) {
        ERROR("XHCI-Failure: host controller error; stopping controller");
        if (controller->State == XHCI_CONTROLLER_RUNNING) {
            controller->State = XHCI_CONTROLLER_STOPPING;
        }
        if (XhciHalt(controller) == OS_EOK) {
            controller->State = XHCI_CONTROLLER_HALTED;
            UsbManagerClearTransfers(&controller->Base);
        }
    }

    if (atomic_load(&controller->Base.InterruptStatus) != 0) {
        goto ProcessInterrupt;
    }
}
