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
 * MollenOS X86 PS2 Controller (Controller) Driver
 * http://wiki.osdev.org/PS2
 */

/* Includes 
 * - System */
#include <os/driver/contracts/base.h>
#include "ps2.h"

/* Includes
 * - Library */
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* Since there only exists a single ps2
 * chip on-board we keep some static information
 * in this driver */
static PS2Controller_t *GlbController = NULL;

/* PS2ReadStatus
 * Reads the status byte from the controller */
uint8_t PS2ReadStatus(void)
{
	return (uint8_t)ReadIoSpace(&GlbController->CommandSpace,
		PS2_REGISTER_STATUS, 1);
}

/* PS2Wait
 * Waits for the given flag to clear by doing a
 * number of iterations giving a fake-delay */
OsStatus_t PS2Wait(uint8_t Flags)
{
	/* Wait for flag to clear */
	for (int i = 0; i < 1000; i++) {
		if (!(PS2ReadStatus() & Flags)) {
			return OsNoError;
		}
	}

	/* Damn.. */
	return OsError;
}

/* PS2ReadData
 * Reads a byte from the PS2 controller data port */
uint8_t PS2ReadData(int Dummy)
{
	/* Hold response */
	uint8_t Response = 0;

	/* Only wait for input to be full in case
	 * we don't do dummy reads */
	if (Dummy == 0) {
		if (PS2Wait(PS2_STATUS_OUTPUT_FULL) == OsError) {
			return 0xFF;
		}
	}

	/* Retrieve the data from data io space */
	Response = (uint8_t)ReadIoSpace(&GlbController->DataSpace, 
		PS2_REGISTER_DATA, 1);

	/* Done */
	return Response;
}

/* PS2SendCommand
 * Writes the given command to the ps2-controller */
void PS2SendCommand(uint8_t Command)
{
	/* Wait for flag to clear, then write data */
	PS2Wait(PS2_STATUS_INPUT_FULL);
	WriteIoSpace(&GlbController->CommandSpace, 
		PS2_REGISTER_COMMAND, Command, 1);
}

/* OnInterrupt
 * Is called when one of the registered devices
 * produces an interrupt. On successful handled
 * interrupt return OsNoError, otherwise the interrupt
 * won't be acknowledged */
InterruptStatus_t OnInterrupt(void *InterruptData)
{
	/* No further processing is needed */
	return InterruptHandled;
}

/* OnLoad
 * The entry-point of a driver, this is called
 * as soon as the driver is loaded in the system */
OsStatus_t OnLoad(void)
{
	/* Allocate a new instance of the pit-data */
	GlbController = (PS2Controller_t*)malloc(sizeof(PS2Controller_t));
	memset(GlbController, 0, sizeof(PS2Controller_t));

	/* Create the io-spaces, again we have to create
	 * the io-space ourselves */
	GlbController->DataSpace.Type = IO_SPACE_IO;
	GlbController->DataSpace.PhysicalBase = PS2_IO_DATA_BASE;
	GlbController->DataSpace.Size = PS2_IO_LENGTH;

	GlbController->CommandSpace.Type = IO_SPACE_IO;
	GlbController->CommandSpace.PhysicalBase = PS2_IO_STATUS_BASE;
	GlbController->CommandSpace.Size = PS2_IO_LENGTH;

	/* Create both the io-spaces in system */
	if (CreateIoSpace(&GlbController->DataSpace) != OsNoError
		|| CreateIoSpace(&GlbController->CommandSpace) != OsNoError) {
		return OsError;
	}

	/* No problem, last thing is to acquire the
	 * io-spaces, and just return that as result */
	if (AcquireIoSpace(&GlbController->DataSpace) != OsNoError
		|| AcquireIoSpace(&GlbController->CommandSpace) != OsNoError) {
		return OsError;
	}

	/* Initialize the ps2-contract */
	InitializeContract(&GlbController->Controller, UUID_INVALID, 1,
		ContractTimer, "PS2 Controller Interface");

	/* We register the ps2-controller contract
	 * immediately since we need to support the
	 * OnRegister/OnUnregister events */
	RegisterContract(&GlbController->Controller);

	/* Anddddd we are done */
	return OsNoError;
}

/* OnUnload
 * This is called when the driver is being unloaded
 * and should free all resources allocated by the system */
OsStatus_t OnUnload(void)
{
	/* Destroy the io-space we created */
	if (GlbPit->IoSpace.Id != 0) {
		ReleaseIoSpace(&GlbPit->IoSpace);
		DestroyIoSpace(GlbPit->IoSpace.Id);
	}

	/* Free up allocated resources */
	free(GlbPit);

	/* Wuhuu */
	return OsNoError;
}

/* OnRegister
 * Is called when the device-manager registers a new
 * instance of this driver for the given device */
OsStatus_t OnRegister(MCoreDevice_t *Device)
{
	/* Update contracts to bind to id 
	 * The CMOS/RTC is a fixed device
	 * and thus we don't support multiple instances */
	if (GlbPit->Timer.DeviceId == UUID_INVALID) {
		GlbPit->Timer.DeviceId = Device->Id;
	}

	/* Now register the clock contract */
	RegisterContract(&GlbPit->Timer);

	/* Done, no more to do here */
	return OsNoError;
}

/* OnUnregister
 * Is called when the device-manager wants to unload
 * an instance of this driver from the system */
OsStatus_t OnUnregister(MCoreDevice_t *Device)
{
	/* The PIT is a fixed device
	 * and thus we don't support multiple instances */
	_CRT_UNUSED(Device);
	return OsNoError;
}

/* OnQuery
 * Occurs when an external process or server quries
 * this driver for data, this will correspond to the query
 * function that is defined in the contract */
OsStatus_t OnQuery(MContractType_t QueryType, UUId_t QueryTarget, int Port)
{
	/* Which kind of query type is being done? */
	if (QueryType == ContractTimer) {
		PipeSend(QueryTarget, Port, &GlbPit->Ticks, sizeof(clock_t));
	}

	/* Done! */
	return OsNoError;
}
