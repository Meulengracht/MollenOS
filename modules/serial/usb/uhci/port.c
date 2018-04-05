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
 * MollenOS MCore - Universal Host Controller Interface Driver
 * Todo:
 * Power Management
 */
//#define __TRACE

/* Includes
 * - System */
#include <os/utils.h>
#include "uhci.h"

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
	uint16_t pOffset    = (UHCI_REGISTER_PORT_BASE + (Index * 2));
	uint16_t TempValue  = 0;

	// Send reset-signal to controller
	UhciWrite16(Controller, pOffset, UHCI_PORT_RESET);

	// Wait atlest 50 ms (per USB specification)
	thrd_sleepex(60);

	// Now re-read port and deassert the signal
	// We preserve the state now
	TempValue           = UhciRead16(Controller, pOffset);
	UhciWrite16(Controller, pOffset, TempValue & ~UHCI_PORT_RESET);

	// Now wait for the connection-status bit
	TempValue           = 0;
	WaitForConditionWithFault(TempValue, (UhciRead16(Controller, pOffset) & UHCI_PORT_CONNECT_STATUS), 10, 1);
	if (TempValue != 0) {
		WARNING("UHCI: Port %i failed to come online in a timely manner after reset...", Index);
	}

	// Enable port & clear event bits
	TempValue           = UhciRead16(Controller, pOffset);
	UhciWrite16(Controller, pOffset, TempValue | UHCI_PORT_CONNECT_EVENT 
		| UHCI_PORT_ENABLED_EVENT | UHCI_PORT_ENABLED);

	// Wait for enable, with timeout
	TempValue           = 0;
	WaitForConditionWithFault(TempValue, (UhciRead16(Controller, pOffset) & UHCI_PORT_ENABLED), 25, 10);

	// Sanitize the result
	if (TempValue != 0) {
		WARNING("UHCI: Port %u enable time-out!", Index);
		return OsError;
	}

	// Wait 30 ms more 
	// I found this wait to be EXTREMELY 
	// crucical, otherwise devices would stall. 
	// because I accessed them to quickly after the reset
	thrd_sleepex(30);
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
    uint16_t pStatus    = 0;
    
	// Now we can get current port status
	pStatus             = UhciRead16(Controller, (UHCI_REGISTER_PORT_BASE + (Index * 2)));

    // Debug
    TRACE("UhciPortGetStatus(Port %i, Status 0x%x)", Index, pStatus);
    
	// Update port metrics
    Port->Connected = (pStatus & UHCI_PORT_CONNECT_STATUS) == 0 ? 0 : 1;
    Port->Enabled   = (pStatus & UHCI_PORT_ENABLED) == 0 ? 0 : 1;
    Port->Speed     = (pStatus & UHCI_PORT_LOWSPEED) == 0 ? FullSpeed : LowSpeed;
}

/* UhciPortCheck
 * Detects if any connection activity has occurred on the given port and
 * controller. If any activity was detected, usb service will be contacted. */
OsStatus_t
UhciPortCheck(
	_In_ UhciController_t*          Controller, 
	_In_ int                        Index)
{
	// Variables
	uint16_t pStatus = UhciRead16(Controller, (UHCI_REGISTER_PORT_BASE + (Index * 2)));
	if (!(pStatus & UHCI_PORT_CONNECT_EVENT)) {
		return OsSuccess;
    }

    // Debug
    TRACE("Uhci-Port(%i): 0x%x", Index, pStatus);

	// Clear connection event so we don't redetect
	UhciWrite16(Controller, (UHCI_REGISTER_PORT_BASE + (Index * 2)), UHCI_PORT_CONNECT_EVENT);
	return UsbEventPort(Controller->Base.Device.Id, Index);
}

/* UhciPortsCheck
 * Enumerates ports and checks for any pending events. This also
 * notifies the usb-service if any connection changes appear */
OsStatus_t
UhciPortsCheck(
	_In_ UhciController_t*          Controller) {
	for (int i = 0; i < (int)(Controller->Base.PortCount); i++) {
		UhciPortCheck(Controller, i);
	}
	return OsSuccess;
}

/* UhciPortPrepare
 * Resets the port and also clears out any event on the port line. */
OsStatus_t
UhciPortPrepare(
	_In_ UhciController_t*          Controller, 
	_In_ int                        Index)
{
	// Trace
	TRACE("UhciPortPrepare(Port %i)", Index);
	return HciPortReset(&Controller->Base, Index);
}
