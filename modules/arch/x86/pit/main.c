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
 * MollenOS X86 PIT (Timer) Driver
 * http://wiki.osdev.org/PIT
 */

/* Includes 
 * - System */
#include <os/driver/contracts/timer.h>
#include "pit.h"

/* Includes
 * - Library */
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* Since there only exists a single pit
 * chip on-board we keep some static information
 * in this driver */
static Pit_t *GlbPit = NULL;

/* OnInterrupt
 * Is called when one of the registered devices
 * produces an interrupt. On successful handled
 * interrupt return OsNoError, otherwise the interrupt
 * won't be acknowledged */
InterruptStatus_t OnInterrupt(void *InterruptData)
{
	// Initiate pointer
	Pit_t *Pit = (Pit_t*)InterruptData;

	// Update stats
	Pit->NsCounter += Pit->NsTick;
	Pit->Ticks++;

	// No further processing is needed
	return InterruptHandled;
}

/* OnLoad
 * The entry-point of a driver, this is called
 * as soon as the driver is loaded in the system */
OsStatus_t OnLoad(void)
{
	// Variables
	size_t Divisor = 1193181;

	// Allocate a new instance of the pit-data
	GlbPit = (Pit_t*)malloc(sizeof(Pit_t));
	memset(GlbPit, 0, sizeof(Pit_t));

	// Create the io-space, again we have to create
	// the io-space ourselves
	GlbPit->IoSpace.Type = IO_SPACE_IO;
	GlbPit->IoSpace.PhysicalBase = PIT_IO_BASE;
	GlbPit->IoSpace.Size = PIT_IO_LENGTH;

	// Initialize the interrupt request
	GlbPit->Interrupt.Line = PIT_IRQ;
	GlbPit->Interrupt.Pin = INTERRUPT_NONE;
	GlbPit->Interrupt.Direct[0] = INTERRUPT_NONE;
	GlbPit->Interrupt.FastHandler = OnInterrupt;
	GlbPit->Interrupt.Data = GlbPit;

	// Update
	GlbPit->NsTick = 1000;

	// Create the io-space in system
	if (CreateIoSpace(&GlbPit->IoSpace) != OsNoError) {
		return OsError;
	}

	// No problem, last thing is to acquire the
	// io-space, and just return that as result
	if (AcquireIoSpace(&GlbPit->IoSpace) != OsNoError) {
		return OsError;
	}

	// Initialize the cmos-contract
	InitializeContract(&GlbPit->Timer, UUID_INVALID, 1,
		ContractTimer, "PIT Timer Interface");

	// Install interrupt in system
	// Install a fast interrupt handler
	GlbPit->Irq = RegisterInterruptSource(&GlbPit->Interrupt,
		INTERRUPT_NOTSHARABLE | INTERRUPT_FAST);

	// Register our irq as a system timer
	if (GlbPit->Irq != UUID_INVALID) {
		RegisterSystemTimer(GlbPit->Irq, GlbPit->NsTick);
	}

	// We want a frequncy of 1000 hz
	Divisor /= 1000;

	// We use counter 0, select counter 0 and configure it
	WriteIoSpace(&GlbPit->IoSpace, PIT_REGISTER_COMMAND,
		PIT_COMMAND_MODE3 | PIT_COMMAND_FULL |
		PIT_COMMAND_COUNTER_0, 1);

	// Write divisor to the PIT chip
	WriteIoSpace(&GlbPit->IoSpace, PIT_REGISTER_COUNTER0,
		(uint8_t)(Divisor & 0xFF), 1);
	WriteIoSpace(&GlbPit->IoSpace, PIT_REGISTER_COUNTER0,
		(uint8_t)((Divisor >> 8) & 0xFF), 1);
	return OsNoError;
}

/* OnUnload
 * This is called when the driver is being unloaded
 * and should free all resources allocated by the system */
OsStatus_t OnUnload(void)
{
	// Destroy the io-space we created
	if (GlbPit->IoSpace.Id != 0) {
		ReleaseIoSpace(&GlbPit->IoSpace);
		DestroyIoSpace(GlbPit->IoSpace.Id);
	}

	// Free up allocated resources
	free(GlbPit);
	return OsNoError;
}

/* OnRegister
 * Is called when the device-manager registers a new
 * instance of this driver for the given device */
OsStatus_t OnRegister(MCoreDevice_t *Device)
{
	// Update contracts to bind to id 
	// The CMOS/RTC is a fixed device
	// and thus we don't support multiple instances
	if (GlbPit->Timer.DeviceId == UUID_INVALID) {
		GlbPit->Timer.DeviceId = Device->Id;
	}

	// Now register the clock contract
	return RegisterContract(&GlbPit->Timer);
}

/* OnUnregister
 * Is called when the device-manager wants to unload
 * an instance of this driver from the system */
OsStatus_t OnUnregister(MCoreDevice_t *Device)
{
	// The PIT is a fixed device
	// and thus we don't support multiple instances
	_CRT_UNUSED(Device);
	return OsNoError;
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
	// Unused parameters
	_CRT_UNUSED(Arg0);
	_CRT_UNUSED(Arg1);
	_CRT_UNUSED(Arg2);

	// Which kind of query type is being done?
	if (QueryType == ContractTimer
		&& QueryFunction == __TIMER_QUERY) {
		PipeSend(Queryee, ResponsePort, &GlbPit->Ticks, sizeof(clock_t));
	}
	return OsNoError;
}
