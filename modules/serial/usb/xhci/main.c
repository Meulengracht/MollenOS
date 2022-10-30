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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS MCore - Advanced Host Controller Interface Driver
 * TODO:
 *	- Port Multiplier Support
 *	- Power Management
 */
//#define __TRACE

/* Includes 
 * - System */
#include <ddk/disk.h>
#include <os/mollenos.h>
#include <ddk/utils.h>
#include "manager.h"

/* Includes
 * - Library */
#include <ds/list.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* Globals
 * State-tracking variables */
static List_t *GlbControllers = NULL;

/* OnInterrupt
 * Is called when one of the registered devices
 * produces an interrupt. On successful handled
 * interrupt return OsSuccess, otherwise the interrupt
 * won't be acknowledged */
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

/* OnLoad
 * The entry-point of a driver, this is called
 * as soon as the driver is loaded in the system */
oscode_t OnLoad(void)
{
	// Initialize state for this driver
	GlbControllers = ListCreate(KeyInteger);

	// Initialize the device manager here
	return AhciManagerInitialize();
}

/* OnUnload
 * This is called when the driver is being unloaded
 * and should free all resources allocated by the system */
void OnUnload(void)
{
	// Iterate registered controllers
	foreach(cNode, GlbControllers) {
		AhciControllerDestroy((AhciController_t*)cNode->Data);
	}

	// Data is now cleaned up, destroy list
	ListDestroy(GlbControllers);

	// Cleanup the internal device manager
	return AhciManagerDestroy();
}

/* OnRegister
 * Is called when the device-manager registers a new
 * instance of this driver for the given device */
oscode_t OnRegister(Device_t *Device)
{
	// Variables
	AhciController_t *Controller = NULL;
	DataKey_t Key;
	
	// Register the new controller
	Controller = AhciControllerCreate(Device);

	// Sanitize
	if (Controller == NULL) {
		return OS_EUNKNOWN;
	}

	// Use the device-id as key
	Key.Value.Integer = (int)Device->Id;

	// Append the controller to our list
	ListAppend(GlbControllers, ListCreateNode(Key, Key, Controller));

	// Done - no error
	return OsSuccess;
}

/* OnUnregister
 * Is called when the device-manager wants to unload
 * an instance of this driver from the system */
oscode_t OnUnregister(Device_t *Device)
{
	// Variables
	AhciController_t *Controller = NULL;
	DataKey_t Key;

	// Set the key to the id of the device to find
	// the bound controller
	Key.Value.Integer = (int)Device->Id;

	// Lookup controller
	Controller = (AhciController_t*)
		ListGetDataByKey(GlbControllers, Key, 0);

	// Sanitize lookup
	if (Controller == NULL) {
		return OS_EUNKNOWN;
	}

	// Remove node from list
	ListRemoveByKey(GlbControllers, Key);

	// Destroy it
	return AhciControllerDestroy(Controller);
}

/* OnQuery
 * Occurs when an external process or server quries
 * this driver for data, this will correspond to the query
 * function that is defined in the contract */
oscode_t
OnQuery(
	_In_     MContractType_t        QueryType, 
	_In_     int                    QueryFunction, 
	_In_Opt_ MRemoteCallArgument_t* Arg0,
	_In_Opt_ MRemoteCallArgument_t* Arg1,
	_In_Opt_ MRemoteCallArgument_t* Arg2,
    _In_     MRemoteCallAddress_t*  Address)
{
	// Unused params
	_CRT_UNUSED(Arg2);

	// Sanitize the QueryType
	if (QueryType != ContractDisk) {
		return OS_EUNKNOWN;
	}

	// Which kind of function has been invoked?
	switch (QueryFunction) {
		// Query stats about a disk identifier in the form of
		// a DiskDescriptor
	case __DISK_QUERY_STAT: {
		// Get parameters
		AhciDevice_t *Device = NULL;
		UUId_t DiskId = (UUId_t)Arg0->Data.Value;

		// Lookup device
		Device = AhciManagerGetDevice(DiskId);

		// Write the descriptor back
		if (Device != NULL) {
			return SendPipe(Queryee, ResponsePort,
				(void*)&Device->Descriptor, sizeof(DiskDescriptor_t));
		}
		else {
            oscode_t Result = OS_EUNKNOWN;
			return SendPipe(Queryee, ResponsePort,
				(void*)&Result, sizeof(OsStatus_t));
		}

	} break;

		// Read or write sectors from a disk identifier
		// They have same parameters with different direction
	case __DISK_QUERY_WRITE:
	case __DISK_QUERY_READ: {
		// Get parameters
		DiskOperation_t *Operation = (DiskOperation_t*)Arg1->Data.Buffer;
		UUId_t DiskId = (UUId_t)Arg0->Data.Value;

		// Create a new transaction
		AhciTransaction_t *Transaction =
			(AhciTransaction_t*)malloc(sizeof(AhciTransaction_t));
		
		// Set sender stuff so we can send a response
		Transaction->Requester = Queryee;
		Transaction->Pipe = ResponsePort;
		
		// Store buffer-object stuff
		Transaction->Address = Operation->PhysicalBuffer;
		Transaction->SectorCount = Operation->SectorCount;

		// Lookup device
		Transaction->Device = AhciManagerGetDevice(DiskId);

		// Determine the kind of operation
		if (Transaction->Device != NULL
			&& Operation->Direction == __DISK_OPERATION_READ) {
			if (AhciReadSectors(Transaction, Operation->AbsSector) != OsSuccess) {
				OsStatus_t Result = OS_EUNKNOWN;
				return SendPipe(Queryee, ResponsePort, (void*)&Result, sizeof(OsStatus_t));
			}
			else {
				return OsSuccess;
			}
		}
		else if (Transaction->Device != NULL
			&& Operation->Direction == __DISK_OPERATION_WRITE) {
			if (AhciWriteSectors(Transaction, Operation->AbsSector) != OsSuccess) {
				OsStatus_t Result = OS_EUNKNOWN;
				return SendPipe(Queryee, ResponsePort, (void*)&Result, sizeof(OsStatus_t));
			}
			else {
				return OsSuccess;
			}
		}
		else {
			OsStatus_t Result = OS_EUNKNOWN;
			return SendPipe(Queryee, ResponsePort, (void*)&Result, sizeof(OsStatus_t));
		}

	} break;

		// Other cases not supported
	default: {
		return OS_EUNKNOWN;
	}
	}
}
