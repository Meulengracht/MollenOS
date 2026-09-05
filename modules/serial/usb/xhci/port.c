/**
 * Copyright 2026, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 */

//#define __TRACE

#include <ddk/io.h>
#include <ddk/utils.h>
#include <threads.h>
#include "xhci.h"

static enum USBSpeed
__XhciSpeedToUsbSpeed(
        _In_ reg32_t portStatus)
{
    switch (XHCI_PORT_STATUS_SPEED(portStatus)) {
        case 1:
            return USBSPEED_FULL;
        case 2:
            return USBSPEED_LOW;
        case 3:
            return USBSPEED_HIGH;
        case 4:
            return USBSPEED_SUPER;
        default:
            return USBSPEED_SUPER_PLUS;
    }
}

void
XhciPortClearChanges(
        _In_ XhciController_t* controller,
        _In_ size_t            index,
        _In_ reg32_t           changes)
{
    reg32_t status = READ_VOLATILE(controller->Ports[index].StatusControl);

    // PORTSC mixes read/write state bits with write-1-to-clear change bits. Keep
    // the writable state bits stable and only echo the change bits we observed.
    WRITE_VOLATILE(controller->Ports[index].StatusControl,
            (status & XHCI_PORT_STATUS_RW_MASK) | (changes & XHCI_PORT_STATUS_CHANGE_BITS));
}

oserr_t
HCIPortReset(
        _In_ UsbManagerController_t* baseController,
        _In_ int                     index)
{
    XhciController_t* controller = (XhciController_t*)baseController;
    reg32_t           status;
    int               fault;

    if (!controller || index < 0 || (size_t)index >= baseController->PortCount) {
        return OS_EINVALPARAMS;
    }

    status = READ_VOLATILE(controller->Ports[index].StatusControl);
    if (!(status & XHCI_PORT_STATUS_CONNECTED)) {
        return OS_ENOENT;
    }

    if (!(status & XHCI_PORT_STATUS_POWER)) {
        WRITE_VOLATILE(controller->Ports[index].StatusControl,
                (status & XHCI_PORT_STATUS_RW_MASK) | XHCI_PORT_STATUS_POWER | XHCI_PORT_STATUS_CHANGE_BITS);
        thrd_sleep(&(struct timespec) { .tv_nsec = 20 * NSEC_PER_MSEC }, NULL);
        status = READ_VOLATILE(controller->Ports[index].StatusControl);
    }

    WRITE_VOLATILE(controller->Ports[index].StatusControl,
            (status & XHCI_PORT_STATUS_RW_MASK) | XHCI_PORT_STATUS_POWER | XHCI_PORT_STATUS_RESET |
            XHCI_PORT_STATUS_CHANGE_BITS);

    WaitForConditionWithFault(fault,
            (READ_VOLATILE(controller->Ports[index].StatusControl) & XHCI_PORT_STATUS_RESET) == 0,
            250,
            10);
    if (fault) {
        ERROR("XHCI::Host controller failed to reset port %i in time.", index);
        return OS_ETIMEOUT;
    }

    status = READ_VOLATILE(controller->Ports[index].StatusControl);
    XhciPortClearChanges(controller, (size_t)index, status);
    return (status & XHCI_PORT_STATUS_ENABLED) ? OS_EOK : OS_EUNKNOWN;
}

void
HCIPortStatus(
        _In_  UsbManagerController_t* baseController,
        _In_  int                     index,
        _Out_ USBPortDescriptor_t*    port)
{
    XhciController_t* controller = (XhciController_t*)baseController;
    reg32_t           status;

    if (!controller || !port || index < 0 || (size_t)index >= baseController->PortCount) {
        return;
    }

    status = READ_VOLATILE(controller->Ports[index].StatusControl);
    port->Connected = (status & XHCI_PORT_STATUS_CONNECTED) == 0 ? 0 : 1;
    port->Enabled = (status & XHCI_PORT_STATUS_ENABLED) == 0 ? 0 : 1;
    port->Speed = __XhciSpeedToUsbSpeed(status);
}

void
XhciPortScan(
        _In_ XhciController_t* controller)
{
    for (size_t i = 0; i < controller->Base.PortCount; i++) {
        reg32_t status = READ_VOLATILE(controller->Ports[i].StatusControl);
        reg32_t changes = status & XHCI_PORT_STATUS_CHANGE_BITS;

        if (changes == 0) {
            continue;
        }

        XhciPortClearChanges(controller, i, changes);
        if (changes & XHCI_PORT_STATUS_CONNECT_CHANGE) {
            TRACE("XhciPortScan(port=%u, status=0x%x)", (unsigned int)i, status);
            (void)UsbEventPort(controller->Base.Device->Base.Id, (uint8_t)(i & 0xFF));
        }
    }
}
