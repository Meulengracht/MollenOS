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
 */
//#define __TRACE

/* Includes
 * - System */
#include <os/utils.h>
#include "ehci.h"

/* Includes
 * - Library */
#include <threads.h>
#include <string.h>

/* EhciPortClearBits
 * Clears the given bits without touching the R/WC bits */
void
EhciPortClearBits(
    _In_ EhciController_t*          Controller,
    _In_ int                        Index,
    _In_ reg32_t                    Bits)
{
    reg32_t PortBits    = Controller->OpRegisters->Ports[Index];
    PortBits            &= ~(EHCI_PORT_RWC);
    PortBits            &= ~(Bits);
    Controller->OpRegisters->Ports[Index] = PortBits;
}

/* EhciPortSetBits
 * Sets the given bits without touching the R/WC bits */
void
EhciPortSetBits(
    _In_ EhciController_t*          Controller,
    _In_ int                        Index,
    _In_ reg32_t                    Bits)
{
    reg32_t PortBits    = Controller->OpRegisters->Ports[Index];
    PortBits            &= ~(EHCI_PORT_RWC);
    PortBits            |= Bits;
    Controller->OpRegisters->Ports[Index] = PortBits;
}

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
    if ((EhciHci->SParameters & EHCI_SPARAM_PPC) && !(Temp & EHCI_PORT_POWER)) {
        EhciPortSetBits(EhciHci, Index, EHCI_PORT_POWER);
        thrd_sleepex(20);
    }
    
    // We must set the port-reset and keep the signal asserted for atleast 50 ms
    // now, we are going to keep the signal alive for (much) longer due to 
    // some devices being slow AF
    
    // The EHCI documentation says we should 
    // disable enabled and assert reset together
    EhciPortClearBits(EhciHci, Index, EHCI_PORT_ENABLED);
    EhciPortSetBits(EhciHci, Index, EHCI_PORT_RESET);

    // Wait 200 ms for reset
    thrd_sleepex(200);
    EhciPortClearBits(EhciHci, Index, EHCI_PORT_RESET);

    // Wait for deassertion: 
    // The reset process is actually complete when software reads a 
    // zero in the PortReset bit
    Temp    = 0;
    WaitForConditionWithFault(Temp, (EhciHci->OpRegisters->Ports[Index] & EHCI_PORT_RESET) == 0, 250, 10);
    if (Temp != 0) {
        ERROR("EHCI::Host controller failed to reset the port in time.");
        return OsError;
    }
    EhciPortSetBits(EhciHci, Index, EHCI_PORT_RWC);

    // Now, if the port has a high-speed 
    // device, the enabled port is set
    if (!(EhciHci->OpRegisters->Ports[Index] & EHCI_PORT_ENABLED)) {
        if (EHCI_SPARAM_CCCOUNT(EhciHci->SParameters) != 0) {
            EhciPortSetBits(EhciHci, Index, EHCI_PORT_COMPANION_HC);
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

/* EhciPortCheck
 * Performs a current status-check on the given port. This automatically
 * registers any events that happen. */
OsStatus_t
EhciPortCheck(
    _In_ EhciController_t*          Controller,
    _In_ size_t                     Index)
{
    // Variables
    reg32_t Status = Controller->OpRegisters->Ports[Index];

    // Clear all event bits
    EhciPortSetBits(Controller, Index, EHCI_PORT_RWC);

    // Over-current event. We should tell the usb-stack this port
    // is now disabled and to disable anything related to this device
    if (Status & EHCI_PORT_OC_EVENT) {
        ERROR("Port %u reported over current. TODO");
        return OsSuccess;
    }

    // Connection event?
    if (Status & EHCI_PORT_CONNECT_EVENT) {
        TRACE("EhciPortCheck(Index %u, Status 0x%x)", Index, Status);

        // Determine the type of connection event
        // because the port might not be for us
        // We have to release it in case of low-speed
        if (Status & EHCI_PORT_CONNECTED) {
            if (EHCI_PORT_LINESTATUS(Status) == EHCI_LINESTATUS_RELEASE) {
                if (EHCI_SPARAM_CCCOUNT(Controller->SParameters) != 0) {
                    EhciPortSetBits(Controller, Index, EHCI_PORT_COMPANION_HC);
                }
                return OsSuccess;
            }
        }
        return UsbEventPort(Controller->Base.Device.Id, 0, (uint8_t)(Index & 0xFF));
    }

    // Enable event. This can only happen when it gets disabled due to something
    // like suspend or that i imagine.
    if (Status & EHCI_PORT_ENABLE_EVENT) {
        ERROR("Port %u is now disabled. TODO");
        return OsSuccess;
    }
    return OsError;
}

/* EhciPortScan
 * Scans all ports of the controller for event-changes and handles
 * them accordingly. */
void
EhciPortScan(
    _In_ EhciController_t*          Controller,
    _In_ reg32_t                    ChangeBits)
{
    for (size_t i = 0; i < Controller->Base.PortCount; i++) {
        if (ChangeBits & (1 << i)) {
            EhciPortCheck(Controller, i);
        }
    }
}
