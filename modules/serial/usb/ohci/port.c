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

#include <ddk/utils.h>
#include "../common/hci.h"
#include "ohci.h"
#include <threads.h>

static void
ClearPortEventBits(
    _In_ OhciController_t* Controller, 
    _In_ int               Index)
{
    reg32_t PortStatus = READ_VOLATILE(Controller->Registers->HcRhPortStatus[Index]);
    
    // Clear connection event
    if (PortStatus & OHCI_PORT_CONNECT_EVENT) {
        WRITE_VOLATILE(Controller->Registers->HcRhPortStatus[Index], OHCI_PORT_CONNECT_EVENT);
    }

    // Clear enable event
    if (PortStatus & OHCI_PORT_ENABLE_EVENT) {
        WRITE_VOLATILE(Controller->Registers->HcRhPortStatus[Index], OHCI_PORT_ENABLE_EVENT);
    }

    // Clear suspend event
    if (PortStatus & OHCI_PORT_SUSPEND_EVENT) {
        WRITE_VOLATILE(Controller->Registers->HcRhPortStatus[Index], OHCI_PORT_SUSPEND_EVENT);
    }

    // Clear over-current event
    if (PortStatus & OHCI_PORT_OVERCURRENT_EVENT) {
        WRITE_VOLATILE(Controller->Registers->HcRhPortStatus[Index], OHCI_PORT_OVERCURRENT_EVENT);
    }

    // Clear reset event
    if (PortStatus & OHCI_PORT_RESET_EVENT) {
        WRITE_VOLATILE(Controller->Registers->HcRhPortStatus[Index], OHCI_PORT_RESET_EVENT);
    }
}

oserr_t
HCIPortReset(
    _In_ UsbManagerController_t* Controller, 
    _In_ int                     Index)
{
    OhciController_t* OhciCtrl = (OhciController_t*)Controller;
    TRACE("HCIPortReset()");
    
    // Let power stabilize
    thrd_sleep(&(struct timespec) { .tv_nsec = OhciCtrl->PowerOnDelayMs * NSEC_PER_MSEC }, NULL);

    // Set reset bit to initialize reset-procedure
    WRITE_VOLATILE(OhciCtrl->Registers->HcRhPortStatus[Index], OHCI_PORT_RESET);

    // Wait for it to clear, with timeout
    WaitForCondition(
        (READ_VOLATILE(OhciCtrl->Registers->HcRhPortStatus[Index]) & OHCI_PORT_RESET) == 0,
        200, 10, "Failed to reset device on port %i\n", Index);

    // Don't matter if timeout, try to enable it
    // If power-mode is port-power, also power it
    if (OhciCtrl->PowerMode == OHCIPOWERMODE_PORTCONTROL) {
        WRITE_VOLATILE(OhciCtrl->Registers->HcRhPortStatus[Index], OHCI_PORT_ENABLED | OHCI_PORT_POWER);
    } else {
        WRITE_VOLATILE(OhciCtrl->Registers->HcRhPortStatus[Index], OHCI_PORT_ENABLED);
    }
    return OS_EOK;
}

void
HCIPortStatus(
        _In_  UsbManagerController_t* Controller,
        _In_  int                     Index,
        _Out_ USBPortDescriptor_t*    Port)
{
    OhciController_t* OhciCtrl  = (OhciController_t*)Controller;
    reg32_t           Status;

    // Now we can get current port status
    Status = READ_VOLATILE(OhciCtrl->Registers->HcRhPortStatus[Index]);

    // Update metrics
    Port->Connected = (Status & OHCI_PORT_CONNECTED) == 0 ? 0 : 1;
    Port->Enabled   = (Status & OHCI_PORT_ENABLED) == 0 ? 0 : 1;
    Port->Speed     = (Status & OHCI_PORT_LOW_SPEED) == 0 ? USBSPEED_FULL : USBSPEED_LOW;
}

static oserr_t
__CheckPortStatus(
    _In_ OhciController_t* Controller,
    _In_ int               Index,
    _In_ int               IgnorePowerOn)
{
    oserr_t oserr      = OS_EOK;
    reg32_t portStatus = READ_VOLATILE(Controller->Registers->HcRhPortStatus[Index]);
    TRACE("__CheckPortStatus(%i): 0x%x", Index, portStatus);

    // Clear bits now we have a copy
    ClearPortEventBits(Controller, Index);
    
    // We only care about connection events currently
    if (portStatus & OHCI_PORT_CONNECT_EVENT) {
        oserr = UsbEventPort(Controller->Base.Device->Base.Id, (uint8_t)(Index & 0xFF));
    }
    return oserr;
}

oserr_t
OHCICheckPorts(
    _In_ OhciController_t* controller,
    _In_ int               ignorePowerOn)
{
    TRACE("OHCICheckPorts()");
    if (spinlock_try_acquire(&controller->Base.Lock) != spinlock_acquired) {
        return OS_EBUSY;
    }
    
    for (int i = 0; i < (int)(controller->Base.PortCount); i++) {
        __CheckPortStatus(controller, i, ignorePowerOn);
    }
    spinlock_release(&controller->Base.Lock);
    return OS_EOK;
}
