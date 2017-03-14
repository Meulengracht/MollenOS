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
 * MollenOS MCore - Advanced Host Controller Interface Driver
 * TODO:
 *	- Port Multiplier Support
 *	- Power Management
 */

/* Includes 
 * - System */
#include <os/driver/contracts/base.h>
#include <os/mollenos.h>
#include "ahci.h"

/* Includes
 * - Library */
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* OnInterrupt
 * Is called when one of the registered devices
 * produces an interrupt. On successful handled
 * interrupt return OsNoError, otherwise the interrupt
 * won't be acknowledged */
InterruptStatus_t OnInterrupt(void *InterruptData)
{
	// Variables
	AhciController_t *Controller = NULL;
	reg32_t InterruptStatus;
	int i;

	// Instantiate the pointer
	Controller = (AhciController_t*)InterruptData;
	InterruptStatus = Controller->Registers->InterruptStatus;

	// Was the interrupt even from this controller?
	if (!InterruptStatus) {
		return InterruptNotHandled;
	}

	// Iterate the port-map and check if the interrupt
	// came from that port
	for (i = 0; i < 32; i++) {
		if (Controller->Ports[i] != NULL
			&& ((InterruptStatus & (1 << i)) != 0)) {
			AhciPortInterruptHandler(Controller, Controller->Ports[i]);
		}
	}

	// Write clear interrupt register and return
	Controller->Registers->InterruptStatus = InterruptStatus;
	return InterruptHandled;
}

/* OnLoad
 * The entry-point of a driver, this is called
 * as soon as the driver is loaded in the system */
OsStatus_t OnLoad(void)
{

}

/* OnUnload
 * This is called when the driver is being unloaded
 * and should free all resources allocated by the system */
OsStatus_t OnUnload(void)
{

	/* Wuhuu */
	return OsNoError;
}

/* OnRegister
 * Is called when the device-manager registers a new
 * instance of this driver for the given device */
OsStatus_t OnRegister(MCoreDevice_t *Device)
{
	// Variables
	AhciController_t *Controller = NULL;
	
	// Register the new controller
	Controller = AhciControllerCreate(Device);

	// Sanitize
	if (Controller == NULL) {
		return OsError;
	}

	// 

	// Done - no error
	return OsNoError;
}

/* OnUnregister
 * Is called when the device-manager wants to unload
 * an instance of this driver from the system */
OsStatus_t OnUnregister(MCoreDevice_t *Device)
{
	/* Variables */
	OsStatus_t Result = OsNoError;

	/* Done! */
	return Result;
}

/* OnQuery
 * Occurs when an external process or server quries
 * this driver for data, this will correspond to the query
 * function that is defined in the contract */
OsStatus_t 
OnQuery(_In_ MContractType_t QueryType, 
		_In_ int QueryFunction, 
		_In_Opt_ RPCArgument_t *Arg0,
		_In_Opt_ RPCArgument_t *Arg1,
		_In_Opt_ RPCArgument_t *Arg2, 
		_In_ UUId_t Queryee, 
		_In_ int ResponsePort)
{
	/* Unused parameters */
	_CRT_UNUSED(QueryType);
	_CRT_UNUSED(QueryFunction);
	_CRT_UNUSED(Arg0);
	_CRT_UNUSED(Arg1);
	_CRT_UNUSED(Arg2);
	_CRT_UNUSED(Queryee);
	_CRT_UNUSED(ResponsePort);

	/* Done! */
	return OsNoError;
}
