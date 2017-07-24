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
#include "ehci.h"

/* Includes
 * - Library */
#include <string.h>

/* Port Logic */
int EhciPortReset(EhciController_t *Controller, size_t Port)
{
	/* Vars */
	uint32_t Temp = Controller->OpRegisters->Ports[Port];

	/* Enable port power */
	if (Controller->SParameters & EHCI_SPARAM_PPC
		&& !(Temp & EHCI_PORT_POWER)) 
	{
		/* Set bit */
		Temp |= EHCI_PORT_POWER;

		/* Write back */
		Controller->OpRegisters->Ports[Port] = Temp;

		/* Wait for power */
		StallMs(20);
	}

	/* We must set the port-reset and keep the signal asserted for atleast 50 ms
	 * now, we are going to keep the signal alive for (much) longer due to 
	 * some devices being slow af */

	/* The EHCI documentation says we should 
	 * disable enabled and assert reset together */
	Temp &= ~(EHCI_PORT_ENABLED);
	Temp |= EHCI_PORT_RESET;

	/* Write */
	Controller->OpRegisters->Ports[Port] = Temp;

	/* Wait */
	StallMs(100);

	/* Deassert signal */
	Temp = Controller->OpRegisters->Ports[Port];
	Temp &= ~(EHCI_PORT_RESET);
	Controller->OpRegisters->Ports[Port] = Temp;

	/* Wait for deassertion: 
	 * The reset process is actually complete when software reads a zero in the PortReset bit */
	Temp = 0;
	WaitForConditionWithFault(Temp, (Controller->OpRegisters->Ports[Port] & EHCI_PORT_RESET) == 0, 250, 10);

	/* Clear RWC */
	Controller->OpRegisters->Ports[Port] |= EHCI_PORT_RWC;

	/* Now, if the port has a high-speed 
	 * device, the enabled port is set */
	if (!(Controller->OpRegisters->Ports[Port] & EHCI_PORT_ENABLED)) {
		if (EHCI_SPARAM_CCCOUNT(Controller->SParameters) != 0)
			Controller->OpRegisters->Ports[Port] |= EHCI_PORT_COMPANION_HC;
		return -1;
	}

	/* Done! */
	return 0;
}

/* Setup Port */
void EhciPortSetup(void *cData, UsbHcPort_t *Port)
{
	/* Cast */
	EhciController_t *Controller = (EhciController_t*)cData;
	int RetVal = 0;

	/* Step 1. Wait for power to stabilize */
	StallMs(100);

#ifdef EHCI_DIAGNOSTICS
	/* Debug */
	LogDebug("EHCI", "Port Pre-Reset: 0x%x", Controller->OpRegisters->Ports[Port->Id]);
#endif

	/* Step 2. Reset Port */
	RetVal = EhciPortReset(Controller, Port->Id);

#ifdef EHCI_DIAGNOSTICS
	/* Debug */
	LogDebug("EHCI", "Port Post-Reset: 0x%x", Controller->OpRegisters->Ports[Port->Id]);
#endif

	/* Evaluate the status of the reset */
	if (RetVal) 
	{
		/* Not for us */
		Port->Connected = 0;
		Port->Enabled = 0;
	}
	else
	{
		/* High Speed */
		uint32_t Status = Controller->OpRegisters->Ports[Port->Id];

		/* Connected? */
		if (Status & EHCI_PORT_CONNECTED)
			Port->Connected = 1;

		/* Enabled? */
		if (Status & EHCI_PORT_ENABLED)
			Port->Enabled = 1;

		/* High Speed */
		Port->Speed = HighSpeed;
	}
}

/* Port Status Check */
void EhciPortCheck(EhciController_t *Controller, size_t Port)
{
	/* Vars */
	uint32_t Status = Controller->OpRegisters->Ports[Port];
	UsbHc_t *HcCtrl;

	/* Connection event? */
	if (!(Status & EHCI_PORT_CONNECT_EVENT))
		return;

	/* Clear Event Bits */
	Controller->OpRegisters->Ports[Port] = (Status | EHCI_PORT_RWC);

	/* Get HCD data */
	HcCtrl = UsbGetHcd(Controller->HcdId);

	/* Sanity */
	if (HcCtrl == NULL)
	{
		LogDebug("EHCI", "Controller %u is zombie and is trying to give events!", Controller->Id);
		return;
	}

	/* Connect or Disconnect? */
	if (Status & EHCI_PORT_CONNECTED)
	{
		/* Ok, something happened
		* but the port might not be for us */
		if (EHCI_PORT_LINESTATUS(Status) == EHCI_LINESTATUS_RELEASE) {
			/* This is a low-speed device */
			if (EHCI_SPARAM_CCCOUNT(Controller->SParameters) != 0)
				Controller->OpRegisters->Ports[Port] |= EHCI_PORT_COMPANION_HC;
		}
		else
			UsbEventCreate(HcCtrl, (int)Port, HcdConnectedEvent);
	}
	else
	{
		/* Disconnect */
		UsbEventCreate(HcCtrl, (int)Port, HcdDisconnectedEvent);
	}
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
