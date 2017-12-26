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
//#define __TRACE

/* Includes 
 * - System */
#include <os/driver/contracts/storage.h>
#include <os/mollenos.h>
#include <os/utils.h>
#include "manager.h"

/* Includes
 * - Library */
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* Globals
 * State-tracking variables */
static Collection_t *GlbControllers = NULL;

/* OnFastInterrupt
 * Is called for the sole purpose to determine if this source
 * has invoked an irq. If it has, silence and return (Handled) */
InterruptStatus_t
OnFastInterrupt(
    _In_Opt_ void *InterruptData)
{
	// Variables
	AhciController_t *Controller = NULL;
	reg32_t InterruptStatus;

	// Instantiate the pointer
	Controller = (AhciController_t*)InterruptData;
    InterruptStatus = Controller->Registers->InterruptStatus;

	// Trace
	TRACE("Interrupt - Status 0x%x", InterruptStatus);

	// Was the interrupt even from this controller?
	if (!InterruptStatus) {
		return InterruptNotHandled;
	}

	// Write clear interrupt register and return
    Controller->Registers->InterruptStatus = InterruptStatus;
    Controller->InterruptStatus |= InterruptStatus;
	return InterruptHandled;
}

/* OnInterrupt
 * Is called by external services to indicate an external interrupt.
 * This is to actually process the device interrupt */
InterruptStatus_t 
OnInterrupt(
    _In_Opt_ void *InterruptData,
    _In_Opt_ size_t Arg0,
    _In_Opt_ size_t Arg1,
    _In_Opt_ size_t Arg2)
{
    // Variables
	AhciController_t *Controller = NULL;
	reg32_t InterruptStatus;
    int i;

    // Unused
    _CRT_UNUSED(Arg0);
    _CRT_UNUSED(Arg1);
    _CRT_UNUSED(Arg2);

	// Instantiate the pointer
	Controller = (AhciController_t*)InterruptData;
    InterruptStatus = Controller->InterruptStatus;
    Controller->InterruptStatus = 0;

HandleInterrupt:
    // Iterate the port-map and check if the interrupt
	// came from that port
	for (i = 0; i < 32; i++) {
		if (Controller->Ports[i] != NULL
			&& ((InterruptStatus & (1 << i)) != 0)) {
			AhciPortInterruptHandler(Controller, Controller->Ports[i]);
		}
    }
    
    // Re-handle?
    if (Controller->InterruptStatus != 0) {
        goto HandleInterrupt;
    }
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
	// Initialize state for this driver
	GlbControllers = CollectionCreate(KeyInteger);

	// Initialize the device manager here
	return AhciManagerInitialize();
}

/* OnUnload
 * This is called when the driver is being unloaded
 * and should free all resources allocated by the system */
OsStatus_t OnUnload(void)
{
	// Iterate registered controllers
	foreach(cNode, GlbControllers) {
		AhciControllerDestroy((AhciController_t*)cNode->Data);
	}

	// Data is now cleaned up, destroy list
	CollectionDestroy(GlbControllers);

	// Cleanup the internal device manager
	return AhciManagerDestroy();
}

/* OnRegister
 * Is called when the device-manager registers a new
 * instance of this driver for the given device */
OsStatus_t OnRegister(MCoreDevice_t *Device)
{
	// Variables
	AhciController_t *Controller = NULL;
	DataKey_t Key;
	
	// Register the new controller
	Controller = AhciControllerCreate(Device);

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
	AhciController_t *Controller = NULL;
	DataKey_t Key;

	// Set the key to the id of the device to find
	// the bound controller
	Key.Value = (int)Device->Id;

	// Lookup controller
	Controller = (AhciController_t*)
		CollectionGetDataByKey(GlbControllers, Key, 0);

	// Sanitize lookup
	if (Controller == NULL) {
		return OsError;
	}

	// Remove node from list
	CollectionRemoveByKey(GlbControllers, Key);

	// Destroy it
	return AhciControllerDestroy(Controller);
}

/* OnQuery
 * Occurs when an external process or server quries
 * this driver for data, this will correspond to the query
 * function that is defined in the contract */
OsStatus_t 
OnQuery(_In_ MContractType_t QueryType, 
		_In_ int QueryFunction, 
		_In_Opt_ MRemoteCallArgument_t *Arg0,
		_In_Opt_ MRemoteCallArgument_t *Arg1,
		_In_Opt_ MRemoteCallArgument_t *Arg2, 
		_In_ UUId_t Queryee, 
		_In_ int ResponsePort)
{
	// Unused params
	_CRT_UNUSED(Arg2);

	// Sanitize the QueryType
	if (QueryType != ContractStorage) {
		return OsError;
	}

	// Which kind of function has been invoked?
	switch (QueryFunction) {
		// Query stats about a disk identifier in the form of
		// a StorageDescriptor
	case __STORAGE_QUERY_STAT: {
		// Get parameters
		AhciDevice_t *Device = NULL;
		UUId_t DiskId = (UUId_t)Arg0->Data.Value;

		// Lookup device
		Device = AhciManagerGetDevice(DiskId);

		// Write the descriptor back
		if (Device != NULL) {
			return PipeSend(Queryee, ResponsePort,
				(void*)&Device->Descriptor, sizeof(StorageDescriptor_t));
		}
		else {
			OsStatus_t Result = OsError;
			return PipeSend(Queryee, ResponsePort,
				(void*)&Result, sizeof(OsStatus_t));
		}

	} break;

		// Read or write sectors from a disk identifier
		// They have same parameters with different direction
	case __STORAGE_QUERY_WRITE:
	case __STORAGE_QUERY_READ: {
		// Get parameters
		StorageOperation_t *Operation = (StorageOperation_t*)Arg1->Data.Buffer;
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
			&& Operation->Direction == __STORAGE_OPERATION_READ) {
			if (AhciReadSectors(Transaction, Operation->AbsSector) != OsSuccess) {
				OsStatus_t Result = OsError;
				return PipeSend(Queryee, ResponsePort, (void*)&Result, sizeof(OsStatus_t));
			}
			else {
				return OsSuccess;
			}
		}
		else if (Transaction->Device != NULL
			&& Operation->Direction == __STORAGE_OPERATION_WRITE) {
			if (AhciWriteSectors(Transaction, Operation->AbsSector) != OsSuccess) {
				OsStatus_t Result = OsError;
				return PipeSend(Queryee, ResponsePort, (void*)&Result, sizeof(OsStatus_t));
			}
			else {
				return OsSuccess;
			}
		}
		else {
			OsStatus_t Result = OsError;
			return PipeSend(Queryee, ResponsePort, (void*)&Result, sizeof(OsStatus_t));
		}

	} break;

		// Other cases not supported
	default: {
		return OsError;
	}
	}
}
