/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS MCore - Open Host Controller Interface Driver
 * TODO:
 *    - Power Management
 */
//#define __TRACE

/* Includes 
 * - System */
#include <os/mollenos.h>
#include <ddk/utils.h>
#include "../common/hci.h"
#include "ohci.h"

/* Includes
 * - Library */
#include <threads.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* HciPortReset
 * Resets the given port and returns the result of the reset */
OsStatus_t
HciPortReset(
    _In_ UsbManagerController_t*    Controller, 
    _In_ int                        Index)
{
    // Variables
    OhciController_t *OhciCtrl = (OhciController_t*)Controller;

    // Set reset bit to initialize reset-procedure
    OhciCtrl->Registers->HcRhPortStatus[Index] = OHCI_PORT_RESET;

    // Wait for it to clear, with timeout
    WaitForCondition((OhciCtrl->Registers->HcRhPortStatus[Index] & OHCI_PORT_RESET) == 0,
        200, 10, "Failed to reset device on port %i\n", Index);

    // Don't matter if timeout, try to enable it
    // If power-mode is port-power, also power it
    if (OhciCtrl->PowerMode == PortControl) {
        OhciCtrl->Registers->HcRhPortStatus[Index] = OHCI_PORT_ENABLED | OHCI_PORT_POWER;
    }
    else {
        OhciCtrl->Registers->HcRhPortStatus[Index] = OHCI_PORT_ENABLED;
    }
    return OsSuccess;
}

/* OhciPortPrepare
 * Resets the port and also clears out any event on the port line. */
OsStatus_t
OhciPortPrepare(
    _In_ OhciController_t*          Controller, 
    _In_ int                        Index)
{
    // Run reset procedure
    HciPortReset(&Controller->Base, Index);

    // Clear connection event
    if (Controller->Registers->HcRhPortStatus[Index] & OHCI_PORT_CONNECT_EVENT) {
        Controller->Registers->HcRhPortStatus[Index] = OHCI_PORT_CONNECT_EVENT;
    }

    // Clear enable event
    if (Controller->Registers->HcRhPortStatus[Index] & OHCI_PORT_ENABLE_EVENT) {
        Controller->Registers->HcRhPortStatus[Index] = OHCI_PORT_ENABLE_EVENT;
    }

    // Clear suspend event
    if (Controller->Registers->HcRhPortStatus[Index] & OHCI_PORT_SUSPEND_EVENT) {
        Controller->Registers->HcRhPortStatus[Index] = OHCI_PORT_SUSPEND_EVENT;
    }

    // Clear over-current event
    if (Controller->Registers->HcRhPortStatus[Index] & OHCI_PORT_OVERCURRENT_EVENT) {
        Controller->Registers->HcRhPortStatus[Index] = OHCI_PORT_OVERCURRENT_EVENT;
    }

    // Clear reset event
    if (Controller->Registers->HcRhPortStatus[Index] & OHCI_PORT_RESET_EVENT) {
        Controller->Registers->HcRhPortStatus[Index] = OHCI_PORT_RESET_EVENT;
    }
    return OsSuccess;
}

/* OhciPortInitialize
 * Initializes a port when the port has changed it's connection
 * state, no allocations are done here */
OsStatus_t
OhciPortInitialize(
    _In_ OhciController_t*          Controller, 
    _In_ int                        Index)
{
    // Wait for port-power to stabilize
    thrd_sleepex(Controller->PowerOnDelayMs);
    OhciPortPrepare(Controller, Index);
    return UsbEventPort(Controller->Base.Device.Id, 0, (uint8_t)(Index & 0xFF));
}

/* HciPortGetStatus 
 * Retrieve the current port status, with connected and enabled information */
void
HciPortGetStatus(
    _In_  UsbManagerController_t*   Controller,
    _In_  int                       Index,
    _Out_ UsbHcPortDescriptor_t*    Port)
{
    // Variables
    OhciController_t *OhciCtrl  = (OhciController_t*)Controller;
    reg32_t Status;

    // Now we can get current port status
    Status          = OhciCtrl->Registers->HcRhPortStatus[Index];

    // Update metrics
    Port->Connected = (Status & OHCI_PORT_CONNECTED) == 0 ? 0 : 1;
    Port->Enabled   = (Status & OHCI_PORT_ENABLED) == 0 ? 0 : 1;
    Port->Speed     = (Status & OHCI_PORT_LOW_SPEED) == 0 ? FullSpeed : LowSpeed;
}

/* OhciPortCheck
 * Detects connection/error events on the given port */
OsStatus_t 
OhciPortCheck(
    _In_ OhciController_t*  Controller, 
    _In_ int                Index)
{
    // Variables
    OsStatus_t Result = OsSuccess;

    // We only care about connection events here
    if (Controller->Registers->HcRhPortStatus[Index] & OHCI_PORT_CONNECT_EVENT) {
        if (!(Controller->Registers->HcRhPortStatus[Index] & OHCI_PORT_CONNECTED)) {
            // Wait for port-power to stabilize and then reset
            thrd_sleepex(Controller->PowerOnDelayMs);
            HciPortReset(&Controller->Base, Index);
        }
        Result = UsbEventPort(Controller->Base.Device.Id, 0, (uint8_t)(Index & 0xFF));
    }

    // Clear connection event
    if (Controller->Registers->HcRhPortStatus[Index] & OHCI_PORT_CONNECT_EVENT) {
        Controller->Registers->HcRhPortStatus[Index] = OHCI_PORT_CONNECT_EVENT;
    }

    // Clear enable event
    if (Controller->Registers->HcRhPortStatus[Index] & OHCI_PORT_ENABLE_EVENT) {
        Controller->Registers->HcRhPortStatus[Index] = OHCI_PORT_ENABLE_EVENT;
    }

    // Clear suspend event
    if (Controller->Registers->HcRhPortStatus[Index] & OHCI_PORT_SUSPEND_EVENT) {
        Controller->Registers->HcRhPortStatus[Index] = OHCI_PORT_SUSPEND_EVENT;
    }

    // Clear over-current event
    if (Controller->Registers->HcRhPortStatus[Index] & OHCI_PORT_OVERCURRENT_EVENT) {
        Controller->Registers->HcRhPortStatus[Index] = OHCI_PORT_OVERCURRENT_EVENT;
    }

    // Clear reset event
    if (Controller->Registers->HcRhPortStatus[Index] & OHCI_PORT_RESET_EVENT) {
        Controller->Registers->HcRhPortStatus[Index] = OHCI_PORT_RESET_EVENT;
    }
    return Result;
}

/* OhciPortsCheck
 * Enumerates all the ports and detects for connection/error events */
OsStatus_t
OhciPortsCheck(
    _In_ OhciController_t*  Controller) {
    for (int i = 0; i < (int)(Controller->Base.PortCount); i++) {
        OhciPortCheck(Controller, i);
    }
    return OsSuccess;
}
