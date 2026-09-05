/**
 * Copyright 2026, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 */

//#define __TRACE

#include <ddk/interrupt.h>
#include <ddk/io.h>
#include <ddk/utils.h>
#include <stdlib.h>
#include "xhci.h"

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
    reg32_t status = READ_VOLATILE(opRegisters->UsbStatus);
    reg32_t management = READ_VOLATILE(interrupterRegisters->Management);

    if (!(status & (XHCI_OP_USBSTS_EVENT | XHCI_OP_USBSTS_HOSTERROR)) &&
        !(management & XHCI_INTERRUPTER_MANAGEMENT_PENDING)) {
        return IRQSTATUS_NOT_HANDLED;
    }

    WRITE_VOLATILE(opRegisters->UsbStatus, status & (XHCI_OP_USBSTS_EVENT | XHCI_OP_USBSTS_HOSTERROR));
    WRITE_VOLATILE(interrupterRegisters->Management, management | XHCI_INTERRUPTER_MANAGEMENT_PENDING);
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
        XhciPortScan(controller);
    }

    if (interruptStatus & XHCI_OP_USBSTS_HOSTERROR) {
        ERROR("XHCI-Failure: host controller error; stopping controller");
        XhciHalt(controller);
        UsbManagerClearTransfers(&controller->Base);
    }

    if (atomic_load(&controller->Base.InterruptStatus) != 0) {
        goto ProcessInterrupt;
    }
}
