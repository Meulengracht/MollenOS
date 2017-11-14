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
 * MollenOS - High Performance Event Timer (HPET) Driver
 *  - Contains the implementation of the HPET driver for mollenos
 */
#define __TRACE

/* Includes 
 * - System */
#include <os/driver/contracts/timer.h>
#include <os/mollenos.h>
#include <os/utils.h>
#include "hpet.h"

/* Includes
 * - Library */
#include <ds/collection.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* Globals
 * State-tracking variables */
static ACPI_TABLE_HPET *__GlbHPET = NULL;
static AcpiDescriptor_t __GlbACPI;
static Collection_t *GlbControllers = NULL;

/* OnInterrupt
 * This driver uses fast-interrupt */
InterruptStatus_t
OnInterrupt(
    _In_Opt_ void *InterruptData,
    _In_Opt_ size_t Arg0,
    _In_Opt_ size_t Arg1,
    _In_Opt_ size_t Arg2)
{
    // Unused
    _CRT_UNUSED(InterruptData);
    _CRT_UNUSED(Arg0);
    _CRT_UNUSED(Arg1);
    _CRT_UNUSED(Arg2);
	return InterruptHandled;
}

/* OnFastInterrupt
 * Is called when one of the registered devices
 * produces an interrupt. On successful handled
 * interrupt return OsSuccess, otherwise the interrupt
 * won't be acknowledged */
InterruptStatus_t
OnFastInterrupt(
    _In_Opt_ void *InterruptData)
{
    // Variables
	HpController_t *Controller = NULL;
	reg32_t InterruptStatus;
	int i;

	// Instantiate the pointer
	Controller = (HpController_t*)InterruptData;
	HpRead(Controller, HPET_REGISTER_INTSTATUS, &InterruptStatus);

	// Trace
	TRACE("Interrupt - Status 0x%x", InterruptStatus);

	// Was the interrupt even from this controller?
	if (!InterruptStatus) {
		return InterruptNotHandled;
	}

	// Iterate the port-map and check if the interrupt
	// came from that timer
	for (i = 0; i < HPET_MAXTIMERCOUNT; i++) {
		if (InterruptStatus & (1 << i)
			&& Controller->Timers[i].Enabled) {
			if (Controller->Timers[i].SystemTimer) {
				Controller->Clock++;
			}
			if (!Controller->Timers[i].PeriodicSupport) {
				// Non periodic timer fired, what now?
				WARNING("HPET::NON-PERIODIC TIMER FIRED");
			}
		}
	}

	// Write clear interrupt register and return
	HpWrite(Controller, HPET_REGISTER_INTSTATUS, InterruptStatus);
	return InterruptHandled;
}

/* OnTimeout
 * Is called when one of the registered timer-handles
 * times-out. A new timeout event is generated and passed
 * on to the below handler */ 
OsStatus_t
OnTimeout(
	_In_ UUId_t Timer,
	_In_ void *Data)
{
	_CRT_UNUSED(Timer);
	_CRT_UNUSED(Data);
	return OsSuccess;
}

/* OnLoad
 * The entry-point of a driver, this is called
 * as soon as the driver is loaded in the system */
OsStatus_t OnLoad(void)
{
	// Variables
	ACPI_TABLE_HEADER *Header = NULL;

	// Initialize state for this driver
	GlbControllers = CollectionCreate(KeyInteger);

	// Load ACPI
	// If it's not present we should abort driver
	if (AcpiQueryStatus(&__GlbACPI) != OsSuccess) {
		__GlbHPET = NULL;
		return OsError;
	}

	// Find the HPET table
	// If it's not present we should abort driver
	if (AcpiQueryTable(ACPI_SIG_HPET, &Header) != OsSuccess) {
		__GlbHPET = NULL;
		return OsError;
	}

	// Update pointer
	__GlbHPET = (ACPI_TABLE_HPET*)Header;

	// No errors
	return OsSuccess;
}

/* OnUnload
 * This is called when the driver is being unloaded
 * and should free all resources allocated by the system */
OsStatus_t OnUnload(void)
{
	// Iterate registered controllers
	foreach(cNode, GlbControllers) {
		HpControllerDestroy((HpController_t*)cNode->Data);
	}

	// Data is now cleaned up, destroy list
	CollectionDestroy(GlbControllers);
	return OsSuccess;
}

/* OnRegister
 * Is called when the device-manager registers a new
 * instance of this driver for the given device */
OsStatus_t OnRegister(MCoreDevice_t *Device)
{
	// Variables
	HpController_t *Controller = NULL;
	DataKey_t Key;

	// Sanitize hpet status
	if (__GlbHPET == NULL) {
		return OsError;
	}
	
	// Register the new controller
	Controller = HpControllerCreate(Device, __GlbHPET);

	// Sanitize
	if (Controller == NULL) {
		return OsError;
	}

	// Use the device-id as key
	Key.Value = (int)Device->Id;

	// Append the controller to our list
	CollectionAppend(GlbControllers, CollectionCreateNode(Key, Controller));

	// Done - no error
	return OsSuccess;
}

/* OnUnregister
 * Is called when the device-manager wants to unload
 * an instance of this driver from the system */
OsStatus_t OnUnregister(MCoreDevice_t *Device)
{
	// Variables
	HpController_t *Controller = NULL;
	DataKey_t Key;

	// Sanitize hpet status
	if (__GlbHPET == NULL) {
		return OsError;
	}

	// Set the key to the id of the device to find
	// the bound controller
	Key.Value = (int)Device->Id;

	// Lookup controller
	Controller = (HpController_t*)
		CollectionGetDataByKey(GlbControllers, Key, 0);

	// Sanitize lookup
	if (Controller == NULL) {
		return OsError;
	}

	// Remove node from list
	CollectionRemoveByKey(GlbControllers, Key);

	// Destroy it
	return HpControllerDestroy(Controller);
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
	// Variables
	HpController_t *Controller = NULL;

	// Unused params
	_CRT_UNUSED(Arg0);
	_CRT_UNUSED(Arg1);
	_CRT_UNUSED(Arg2);

	// Sanitize the QueryType
	if (QueryType != ContractTimer
		&& QueryType != ContractTimerPerformance) {
		return OsError;
	}

	// Sanitize controller count
	if (CollectionLength(GlbControllers) == 0) {
		return OsError;
	}

	// Lookup first controller
	Controller = (HpController_t*)CollectionBegin(GlbControllers)->Data;

	// Which kind of function has been invoked?
	switch (QueryFunction) {
	// Query the clock counter
	case __TIMER_QUERY: {
		return PipeSend(Queryee, ResponsePort, 
			&Controller->Clock, sizeof(clock_t));
	} break;

	// Query the frequency of the HPET main counter
	case __TIMER_PERFORMANCE_FREQUENCY: {
		return PipeSend(Queryee, ResponsePort,
			&Controller->Frequency, sizeof(LargeInteger_t));
	} break;

	// Query the current readings of the main counter
	case __TIMER_PERFORMANCE_QUERY: {
		// Get a reading first
		LargeInteger_t Value;
		HpReadPerformanceCounter(Controller, &Value);
		return PipeSend(Queryee, ResponsePort,
			&Value, sizeof(LargeInteger_t));
	} break;

		// Other cases not supported
	default: {
		return OsError;
	}
	}
}
