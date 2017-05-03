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
 *	- Power Management
 */
//#define __TRACE

/* Includes 
 * - System */
#include <os/mollenos.h>
#include <os/utils.h>
#include "ohci.h"

/* Includes
 * - Library */
#include <ds/list.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* This resets a Port */
void OhciPortReset(OhciController_t *Controller, size_t Port)
{
	/* Set reset */
	Controller->Registers->HcRhPortStatus[Port] = OHCI_PORT_RESET;

	/* Wait with timeout */
	WaitForCondition((Controller->Registers->HcRhPortStatus[Port] & OHCI_PORT_RESET) == 0,
		200, 10, "USB_OHCI: Failed to reset device on port %u\n", Port);

	/* Set Enable */
	if (Controller->PowerMode == OHCI_PWM_PORT_CONTROLLED)
		Controller->Registers->HcRhPortStatus[Port] = OHCI_PORT_ENABLED | OHCI_PORT_POWER;
	else
		Controller->Registers->HcRhPortStatus[Port] = OHCI_PORT_ENABLED;

	/* Delay before we make first transaction, it's important! */
	StallMs(50);
}

/* OhciPortInitialize
 * Initializes a port when the port has changed it's connection
 * state, no allocations are done here */
OsStatus_t
OhciPortInitialize(
	_In_ OhciController_t *Controller, 
	_In_ int Index)
{
	// Variables
	reg32_t Status;

	/* Wait for power to stabilize */
	StallMs(Controller->PowerOnDelayMs);

	/* Reset Port */
	OhciPortReset(Controller, Port->Id);

	/* Update information in Port */
	Status = Controller->Registers->HcRhPortStatus[Port->Id];

	/* Is it connected? */
	if (Status & OHCI_PORT_CONNECTED)
		Port->Connected = 1;
	else
		Port->Connected = 0;

	/* Is it enabled? */
	if (Status & OHCI_PORT_ENABLED)
		Port->Enabled = 1;
	else
		Port->Enabled = 0;

	/* Is it full-speed? */
	if (Status & OHCI_PORT_LOW_SPEED)
		Port->Speed = LowSpeed;
	else
		Port->Speed = FullSpeed;

	/* Clear Connect Event */
	if (Controller->Registers->HcRhPortStatus[Port->Id] & OHCI_PORT_CONNECT_EVENT)
		Controller->Registers->HcRhPortStatus[Port->Id] = OHCI_PORT_CONNECT_EVENT;

	/* If Enable Event bit is set, clear it */
	if (Controller->Registers->HcRhPortStatus[Port->Id] & OHCI_PORT_ENABLE_EVENT)
		Controller->Registers->HcRhPortStatus[Port->Id] = OHCI_PORT_ENABLE_EVENT;

	/* If Suspend Event is set, clear it */
	if (Controller->Registers->HcRhPortStatus[Port->Id] & OHCI_PORT_SUSPEND_EVENT)
		Controller->Registers->HcRhPortStatus[Port->Id] = OHCI_PORT_SUSPEND_EVENT;

	/* If Over Current Event is set, clear it */
	if (Controller->Registers->HcRhPortStatus[Port->Id] & OHCI_PORT_OVR_CURRENT_EVENT)
		Controller->Registers->HcRhPortStatus[Port->Id] = OHCI_PORT_OVR_CURRENT_EVENT;

	/* If reset bit is set, clear it */
	if (Controller->Registers->HcRhPortStatus[Port->Id] & OHCI_PORT_RESET_EVENT)
		Controller->Registers->HcRhPortStatus[Port->Id] = OHCI_PORT_RESET_EVENT;
}

/* Port Functions */
void OhciPortCheck(OhciController_t *Controller, int Port)
{
	/* Vars */
	UsbHc_t *HcCtrl;

	/* Was it connect event and not disconnect ? */
	if (Controller->Registers->HcRhPortStatus[Port] & OHCI_PORT_CONNECT_EVENT)
	{
		/* Reset on Attach */
		OhciPortReset(Controller, Port);

		if (!(Controller->Registers->HcRhPortStatus[Port] & OHCI_PORT_CONNECTED))
		{
			/* Nah, disconnect event */
			/* Get HCD data */
			HcCtrl = UsbGetHcd(Controller->HcdId);

			/* Sanity */
			if (HcCtrl == NULL)
				return;

			/* Disconnect */
			UsbEventCreate(HcCtrl, Port, HcdDisconnectedEvent);
		}

		/* If Device is enabled, and powered, set it up */
		if ((Controller->Registers->HcRhPortStatus[Port] & OHCI_PORT_ENABLED)
			&& (Controller->Registers->HcRhPortStatus[Port] & OHCI_PORT_POWER))
		{
			/* Get HCD data */
			HcCtrl = UsbGetHcd(Controller->HcdId);

			/* Sanity */
			if (HcCtrl == NULL)
			{
				LogDebug("OHCI", "Controller %u is zombie and is trying to register Ports!!", Controller->Id);
				return;
			}

			/* Register Device */
			UsbEventCreate(HcCtrl, Port, HcdConnectedEvent);
		}
	}

	/* Clear Connect Event */
	if (Controller->Registers->HcRhPortStatus[Port] & OHCI_PORT_CONNECT_EVENT)
		Controller->Registers->HcRhPortStatus[Port] = OHCI_PORT_CONNECT_EVENT;

	/* If Enable Event bit is set, clear it */
	if (Controller->Registers->HcRhPortStatus[Port] & OHCI_PORT_ENABLE_EVENT)
		Controller->Registers->HcRhPortStatus[Port] = OHCI_PORT_ENABLE_EVENT;

	/* If Suspend Event is set, clear it */
	if (Controller->Registers->HcRhPortStatus[Port] & OHCI_PORT_SUSPEND_EVENT)
		Controller->Registers->HcRhPortStatus[Port] = OHCI_PORT_SUSPEND_EVENT;

	/* If Over Current Event is set, clear it */
	if (Controller->Registers->HcRhPortStatus[Port] & OHCI_PORT_OVR_CURRENT_EVENT)
		Controller->Registers->HcRhPortStatus[Port] = OHCI_PORT_OVR_CURRENT_EVENT;

	/* If reset bit is set, clear it */
	if (Controller->Registers->HcRhPortStatus[Port] & OHCI_PORT_RESET_EVENT)
		Controller->Registers->HcRhPortStatus[Port] = OHCI_PORT_RESET_EVENT;
}

/* Port Check */
OsStatus_t
OhciPortsCheck(
	_In_ OhciController_t *Controller)
{
	// Variables
	size_t i;

	// Iterate ports and check for changes
	for (i = 0; i < Controller->Ports; i++) {
		OhciPortCheck(Controller, i);
	}
}
