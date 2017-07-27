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
 * MollenOS MCore - Enhanced Host Controller Interface Driver
 * TODO:
 * - Power Management
 * - Isochronous Transport
 * - Transaction Translator Support
 */

/* Includes
 * - System */
#include <os/thread.h>
#include "ehci.h"

/* Includes
 * - Library */
#include <string.h>

/* EhciPortReset
 * Resets the given port and returns the result of the reset */
OsStatus_t
EhciPortReset(
	_In_ EhciController_t *Controller, 
	_In_ int Index)
{
	// Variables
	reg32_t Temp = Controller->OpRegisters->Ports[Index];

	// If we are per-port handled, and power is not enabled
	// then switch it on, and give it some time to recover
	if (Controller->SParameters & EHCI_SPARAM_PPC
		&& !(Temp & EHCI_PORT_POWER)) {
		Temp |= EHCI_PORT_POWER;
		Controller->OpRegisters->Ports[Index] = Temp;
		ThreadSleep(20);
	}

	// We must set the port-reset and keep the signal asserted for atleast 50 ms
	// now, we are going to keep the signal alive for (much) longer due to 
	// some devices being slow AF
	
	// The EHCI documentation says we should 
	// disable enabled and assert reset together
	Temp &= ~(EHCI_PORT_ENABLED);
	Temp |= EHCI_PORT_RESET;
	Controller->OpRegisters->Ports[Index] = Temp;

	// Wait 100 ms for reset
	ThreadSleep(100);

	// Clear reset signal
	Temp = Controller->OpRegisters->Ports[Index];
	Temp &= ~(EHCI_PORT_RESET);
	Controller->OpRegisters->Ports[Index] = Temp;

	// Wait for deassertion: 
	// The reset process is actually complete when software reads a 
	// zero in the PortReset bit
	Temp = 0;
	WaitForConditionWithFault(Temp, 
		(Controller->OpRegisters->Ports[Index] & EHCI_PORT_RESET) == 0, 250, 10);

	// Clear the RWC bit
	Controller->OpRegisters->Ports[Index] |= EHCI_PORT_RWC;

	// Now, if the port has a high-speed 
	// device, the enabled port is set
	if (!(Controller->OpRegisters->Ports[Index] & EHCI_PORT_ENABLED)) {
		if (EHCI_SPARAM_CCCOUNT(Controller->SParameters) != 0) {
			Controller->OpRegisters->Ports[Index] |= EHCI_PORT_COMPANION_HC;
		}
		return OsError;
	}

	// Done
	return OsSuccess;
}

/* EhciPortGetStatus 
 * Retrieve the current port status, with connected and enabled information */
void 
EhciPortGetStatus(
	_In_ EhciController_t *Controller,
	_In_ int Index,
	_Out_ UsbHcPortDescriptor_t *Port)
{
	// Variables
	reg32_t Status;

	// Now we can get current port status
	Status = Controller->OpRegisters->Ports[Index];

	// Is port connected?
	if (Status & EHCI_PORT_CONNECTED) {
		Port->Connected = 1;
	}
	else {
		Port->Connected = 0;
	}

	// Is port enabled?
	if (Status & EHCI_PORT_ENABLED) {
		Port->Enabled = 1;
	}
	else {
		Port->Enabled = 0;
	}

	// EHCI Only deals in high-speed
	Port->Speed = HighSpeed;
}

/* EhciPortSetup 
 * Waits for power-on delay and resets port */
void
EhciPortSetup(
	_In_ EhciController_t *Controller,
	_In_ int Index)
{
	// Variables
	OsStatus_t ReturnValue = 0;

	// Wait for power-on delay
	ThreadSleep(100);

	// Trace
	TRACE("EHCI-Port Pre-Reset: 0x%x", Controller->OpRegisters->Ports[Index]);

	// Reset port
	ReturnValue = EhciPortReset(Controller, Index);

	// Trace
	TRACE("EHCI-Port Pre-Reset: 0x%x", Controller->OpRegisters->Ports[Index]);

	// Done
	return ReturnValue;
}

/* EhciPortCheck
 * Performs a current status-check on the given port. This automatically
 * registers any events that happen. */
void
EhciPortCheck(
	_In_ EhciController_t *Controller,
	_In_ int Index)
{
	// Variables
	reg32_t Status = Controller->OpRegisters->Ports[Index];

	// Connection event? Otherwise ignore
	if (!(Status & EHCI_PORT_CONNECT_EVENT)) {
		return;
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
			return;
		}
	}

	// Fire off event
	UsbEventPort(Controller->Base.Id, Index);
}

/* EhciPortScan
 * Scans all ports of the controller for event-changes and handles
 * them accordingly. */
void
EhciPortScan(
	_In_ EhciController_t *Controller)
{
	// Variables
	size_t i;

	// Enumerate ports
	for (i = 0; i < Controller->Base.PortCount; i++) {
		EhciPortCheck(Controller, i);
	}
}
