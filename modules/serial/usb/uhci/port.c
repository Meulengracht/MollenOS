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
 * Finish the FSBR implementation, right now there is no guarantee of order ls/fs/bul
 * The isochronous unlink/link needs improvements, it does not support multiple isocs in same frame 
 */

/* Includes
 * - System */
#include "uhci.h"

/* Includes
 * - Library */
#include <string.h>

/* Ports */
void UhciPortReset(UhciController_t *Controller, uint32_t Port)
{
	/* Calc */
	uint16_t Temp;
	uint16_t pOffset = (UHCI_REGISTER_PORT_BASE + ((uint16_t)Port * 2));

	/* Step 1. Send reset signal */
	UhciWrite16(Controller, pOffset, UHCI_PORT_RESET);

	/* Wait atlest 50 ms (per USB specification) */
	StallMs(60);

	/* Now deassert reset signal */
	Temp = UhciRead16(Controller, pOffset);
	UhciWrite16(Controller, pOffset, Temp & ~UHCI_PORT_RESET);

	/* Wait for CSC */
	Temp = 0;
	WaitForConditionWithFault(Temp,
		(UhciRead16(Controller, pOffset) & UHCI_PORT_CONNECT_STATUS), 10, 1);

	/* Even if it fails, try to enable anyway */
	if (Temp) {
		LogDebug("UHCI", "Port failed to come online in a timely manner after reset...");
	}

	/* Step 2. Enable Port & clear event bits */
	Temp = UhciRead16(Controller, pOffset);
	UhciWrite16(Controller, pOffset, 
		Temp | UHCI_PORT_CONNECT_EVENT | UHCI_PORT_ENABLED_EVENT | UHCI_PORT_ENABLED);

	/* Wait for enable, with timeout */
	Temp = 0;
	WaitForConditionWithFault(Temp,
		(UhciRead16(Controller, pOffset) & UHCI_PORT_ENABLED), 25, 10);

	/* Sanity */
	if (Temp) {
		LogDebug("UHCI", "Port %u enable time-out!", Port);
	}

	/* Wait 30 ms more 
	 * I found this wait to be EXTREMELY 
	 * crucical, otherwise devices would stall. 
	 * because I accessed them to quickly after the reset */
	StallMs(30);
}

/* Detect any port changes */
void UhciPortCheck(UhciController_t *Controller, int Port)
{
	/* Get port status */
	uint16_t pStatus = UhciRead16(Controller, (UHCI_REGISTER_PORT_BASE + ((uint16_t)Port * 2)));
	UsbHc_t *Hcd;

	/* Has there been a connection event? */
	if (!(pStatus & UHCI_PORT_CONNECT_EVENT))
		return;

	/* Clear connection event */
	UhciWrite16(Controller, 
		(UHCI_REGISTER_PORT_BASE + ((uint16_t)Port * 2)), UHCI_PORT_CONNECT_EVENT);

	/* Get HCD data */
	Hcd = UsbGetHcd(Controller->HcdId);

	/* Sanity */
	if (Hcd == NULL)
		return;

	/* Connect event? */
	if (pStatus & UHCI_PORT_CONNECT_STATUS)
	{
		/* Connection Event */
		UsbEventCreate(Hcd, Port, HcdConnectedEvent);
	}
	else
	{
		/* Disconnect Event */
		UsbEventCreate(Hcd, Port, HcdDisconnectedEvent);
	}
}

/* Go through ports */
void UhciPortsCheck(void *Data)
{
	/* Cast & Vars */
	UhciController_t *Controller = (UhciController_t*)Data;
	int i;

	/* Iterate ports and check */
	for (i = 0; i < (int)Controller->NumPorts; i++)
		UhciPortCheck(Controller, i);

	/* Disable FSBR */
}

/* Gets port status */
void UhciPortSetup(void *Data, UsbHcPort_t *Port)
{
	UhciController_t *Controller = (UhciController_t*)Data;
	uint16_t pStatus = 0;

	/* Reset Port */
	UhciPortReset(Controller, Port->Id);

	/* Dump info */
	pStatus = UhciRead16(Controller, (UHCI_REGISTER_PORT_BASE + ((uint16_t)Port->Id * 2)));

#ifdef UHCI_DIAGNOSTICS
	LogDebug("UHCI", "UHCI %u.%u Status: 0x%x", Controller->Id, Port->Id, pStatus);
#endif

	/* Is it connected? */
	if (pStatus & UHCI_PORT_CONNECT_STATUS)
		Port->Connected = 1;
	else
		Port->Connected = 0;

	/* Enabled? */
	if (pStatus & UHCI_PORT_ENABLED)
		Port->Enabled = 1;
	else
		Port->Enabled = 0;

	/* Lowspeed? */
	if (pStatus & UHCI_PORT_LOWSPEED)
		Port->Speed = LowSpeed;
	else
		Port->Speed = FullSpeed;
}
