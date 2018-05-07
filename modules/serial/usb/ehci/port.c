/* MollenOS
 *
 * Copyright 2018, Philip Meulengracht
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
 * MollenOS MCore - Enhanced Host Controller Interface Driver
 * TODO:
 * - Power Management
 * - Transaction Translator Support
 */

/* Includes
 * - System */
#include <os/utils.h>
#include "ehci.h"

/* Includes
 * - Library */
#include <threads.h>
#include <string.h>

/* HciPortReset
 * Resets the given port and returns the result of the reset */
OsStatus_t
HciPortReset(
    _In_ UsbManagerController_t*    Controller, 
    _In_ int                        Index)
{
    // Variables
    EhciController_t *EhciHci   = (EhciController_t*)Controller;
    reg32_t Temp                = EhciHci->OpRegisters->Ports[Index];

    // If we are per-port handled, and power is not enabled
    // then switch it on, and give it some time to recover
    if (EhciHci->SParameters & EHCI_SPARAM_PPC && !(Temp & EHCI_PORT_POWER)) {
        Temp                                    |= EHCI_PORT_POWER;
        EhciHci->OpRegisters->Ports[Index]      = Temp;
        thrd_sleepex(20);
    }

    // We must set the port-reset and keep the signal asserted for atleast 50 ms
    // now, we are going to keep the signal alive for (much) longer due to 
    // some devices being slow AF
    
    // The EHCI documentation says we should 
    // disable enabled and assert reset together
    Temp                                    &= ~(EHCI_PORT_ENABLED);
    Temp                                    |= EHCI_PORT_RESET;
    EhciHci->OpRegisters->Ports[Index]      = Temp;

    // Wait 100 ms for reset
    thrd_sleepex(100);

    // Clear reset signal
    Temp                                    = EhciHci->OpRegisters->Ports[Index];
    Temp                                    &= ~(EHCI_PORT_RESET);
    EhciHci->OpRegisters->Ports[Index]      = Temp;

    // Wait for deassertion: 
    // The reset process is actually complete when software reads a 
    // zero in the PortReset bit
    Temp                                    = 0;
    WaitForConditionWithFault(Temp, (EhciHci->OpRegisters->Ports[Index] & EHCI_PORT_RESET) == 0, 250, 10);

    // Clear the RWC bit
    EhciHci->OpRegisters->Ports[Index]      |= EHCI_PORT_RWC;

    // Now, if the port has a high-speed 
    // device, the enabled port is set
    if (!(EhciHci->OpRegisters->Ports[Index] & EHCI_PORT_ENABLED)) {
        if (EHCI_SPARAM_CCCOUNT(EhciHci->SParameters) != 0) {
            EhciHci->OpRegisters->Ports[Index] |= EHCI_PORT_COMPANION_HC;
        }
        return OsError;
    }
    return OsSuccess;
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
    EhciController_t *EhciHci   = (EhciController_t*)Controller;
    reg32_t Status              = 0;

    // Now we can get current port status
    Status          = EhciHci->OpRegisters->Ports[Index];

    // Is port connected?
    Port->Connected = (Status & EHCI_PORT_CONNECTED) == 0 ? 0 : 1;
    Port->Enabled   = (Status & EHCI_PORT_ENABLED) == 0 ? 0 : 1;
    Port->Speed     = HighSpeed; // Ehci only has high-speed root ports
}

/* EhciPortSetup 
 * Waits for power-on delay and resets port */
OsStatus_t
EhciPortSetup(
    _In_ EhciController_t*          Controller,
    _In_ int                        Index) {
    thrd_sleepex(100); // Wait for power-on delay
    return HciPortReset(&Controller->Base, Index);
}

/* EhciPortCheck
 * Performs a current status-check on the given port. This automatically
 * registers any events that happen. */
OsStatus_t
EhciPortCheck(
    _In_ EhciController_t*          Controller,
    _In_ int                        Index)
{
    // Variables
    reg32_t Status = Controller->OpRegisters->Ports[Index];

    // Connection event? Otherwise ignore
    if (!(Status & EHCI_PORT_CONNECT_EVENT)) {
        return OsSuccess;
    }

    // Clear all event bits
    Controller->OpRegisters->Ports[Index] = (Status | EHCI_PORT_RWC);

    // Determine the type of connection event
    // because the port might not be for us
    // We have to release it in case of low-speed
    if (Status & EHCI_PORT_CONNECTED) {
        if (EHCI_PORT_LINESTATUS(Status) == EHCI_LINESTATUS_RELEASE) {
            if (EHCI_SPARAM_CCCOUNT(Controller->SParameters) != 0) {
                Controller->OpRegisters->Ports[Index] |= EHCI_PORT_COMPANION_HC;
            }
            return OsSuccess;
        }
    }
    return UsbEventPort(Controller->Base.Device.Id, 0, (uint8_t)(Index & 0xFF));
}

/* EhciPortScan
 * Scans all ports of the controller for event-changes and handles
 * them accordingly. */
void
EhciPortScan(
    _In_ EhciController_t*          Controller) {
    for (size_t i = 0; i < Controller->Base.PortCount; i++) {
        EhciPortCheck(Controller, i);
    }
}
