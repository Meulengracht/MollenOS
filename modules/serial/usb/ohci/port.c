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
#include <os/utils.h>
#include "ohci.h"

/* Includes
 * - Library */
#include <threads.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* OhciPortReset
 * Resets the given port and returns the result of the reset */
OsStatus_t
OhciPortReset(
    _In_ OhciController_t *Controller, 
    _In_ int Index)
{
    // Set reset bit to initialize reset-procedure
    Controller->Registers->HcRhPortStatus[Index] = OHCI_PORT_RESET;

    // Wait for it to clear, with timeout
    WaitForCondition((Controller->Registers->HcRhPortStatus[Index] & OHCI_PORT_RESET) == 0,
        200, 10, "Failed to reset device on port %i\n", Index);

    // Don't matter if timeout, try to enable it
    // If power-mode is port-power, also power it
    if (Controller->PowerMode == PortControl) {
        Controller->Registers->HcRhPortStatus[Index] = OHCI_PORT_ENABLED | OHCI_PORT_POWER;
    }
    else {
        Controller->Registers->HcRhPortStatus[Index] = OHCI_PORT_ENABLED;
    }

    // We need a delay here to allow the port to settle
    thrd_sleepex(50);
    return OsSuccess;
}

/* OhciPortPrepare
 * Resets the port and also clears out any event on the port line. */
OsStatus_t
OhciPortPrepare(
    _In_ OhciController_t *Controller, 
    _In_ int Index)
{
    // Run reset procedure
    OhciPortReset(Controller, Index);

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

    // Done
    return OsSuccess;
}

/* OhciPortInitialize
 * Initializes a port when the port has changed it's connection
 * state, no allocations are done here */
OsStatus_t
OhciPortInitialize(
    _In_ OhciController_t *Controller, 
    _In_ int Index)
{
    // Wait for port-power to stabilize
    thrd_sleepex(Controller->PowerOnDelayMs);

    // Run reset procedure
    OhciPortPrepare(Controller, Index);

    // Run usb port event
    return UsbEventPort(Controller->Base.Device.Id, Index);
}

/* OhciPortGetStatus 
 * Retrieve the current port status, with connected and enabled information */
void 
OhciPortGetStatus(
    _In_ OhciController_t *Controller,
    _In_ int Index,
    _Out_ UsbHcPortDescriptor_t *Port)
{
    // Variables
    reg32_t Status;

    // Now we can get current port status
    Status = Controller->Registers->HcRhPortStatus[Index];

    // Is port connected?
    if (Status & OHCI_PORT_CONNECTED) {
        Port->Connected = 1;
    }
    else {
        Port->Connected = 0;
    }

    // Is port enabled?
    if (Status & OHCI_PORT_ENABLED) {
        Port->Enabled = 1;
    }
    else {
        Port->Enabled = 0;
    }

    // Detect speed of the connected device/port
    if (Status & OHCI_PORT_LOW_SPEED) {
        Port->Speed = LowSpeed;
    }
    else {
        Port->Speed = FullSpeed;
    }
}

/* OhciPortCheck
 * Detects connection/error events on the given port */
OsStatus_t 
OhciPortCheck(
    _In_ OhciController_t *Controller, 
    _In_ int Index)
{
    // Variables
    OsStatus_t Result = OsSuccess;

    // We only care about connection events here
    if (Controller->Registers->HcRhPortStatus[Index] & OHCI_PORT_CONNECT_EVENT) {
        if (!(Controller->Registers->HcRhPortStatus[Index] & OHCI_PORT_CONNECTED)) {
            // Wait for port-power to stabilize
            thrd_sleepex(Controller->PowerOnDelayMs);

            // All ports must be reset when attached
            OhciPortReset(Controller, Index);
        }

        // Send out an event
        Result = UsbEventPort(Controller->Base.Device.Id, Index);
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

    // Done
    return Result;
}

/* OhciPortsCheck
 * Enumerates all the ports and detects for connection/error events */
OsStatus_t
OhciPortsCheck(
    _In_ OhciController_t *Controller)
{
    // Variables
    int i;
    for (i = 0; i < (int)(Controller->Base.PortCount); i++) {
        OhciPortCheck(Controller, i);
    }
    return OsSuccess;
}
