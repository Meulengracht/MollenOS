/**
 * Copyright 2026, Philip Meulengracht
 */

#include <ddk/utils.h>
#include <string.h>
#include <threads.h>
#include "xhci.h"

static enum USBSpeed
__PortSpeed(
    _In_ reg32_t portStatus)
{
    switch (XHCI_PORTSC_SPEED(portStatus)) {
        case 1: return USBSPEED_FULL;
        case 2: return USBSPEED_LOW;
        case 3: return USBSPEED_HIGH;
        case 4: return USBSPEED_SUPER;
        case 5: return USBSPEED_SUPER_PLUS;
        default: return USBSPEED_FULL;
    }
}

oserr_t
HCIPortReset(
    _In_ UsbManagerController_t* controllerBase,
    _In_ int                     index)
{
    XhciController_t* controller = (XhciController_t*)controllerBase;
    reg32_t           portStatus;

    if (index < 0 || index >= (int)controllerBase->PortCount) {
        return OS_EINVALPARAMS;
    }

    portStatus = READ_VOLATILE(controller->PortRegisters[index].PortSc);
    if (!(portStatus & XHCI_PORTSC_CCS)) {
        return OS_ENOENT;
    }

    WRITE_VOLATILE(
            controller->PortRegisters[index].PortSc,
            (portStatus & ~XHCI_PORTSC_W1C) | XHCI_PORTSC_PR
    );
    thrd_sleep(&(struct timespec) { .tv_nsec = 50 * NSEC_PER_MSEC }, NULL);
    return OS_EOK;
}

void
HCIPortStatus(
    _In_ UsbManagerController_t* controllerBase,
    _In_ int                     index,
    _In_ USBPortDescriptor_t*    port)
{
    XhciController_t* controller = (XhciController_t*)controllerBase;
    reg32_t           portStatus = 0;

    memset(port, 0, sizeof(USBPortDescriptor_t));
    if (index < 0 || index >= (int)controllerBase->PortCount) {
        return;
    }

    portStatus       = READ_VOLATILE(controller->PortRegisters[index].PortSc);
    port->Connected  = (portStatus & XHCI_PORTSC_CCS) != 0;
    port->Enabled    = (portStatus & XHCI_PORTSC_PED) != 0;
    port->Speed      = __PortSpeed(portStatus);
}
