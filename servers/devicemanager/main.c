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
 * MollenOS MCore - Device Manager
 * - Initialization + Event Mechanism
 */
//#define __TRACE

/* Includes
 * - System */
#include <os/driver/contracts/base.h>
#include <os/driver/driver.h>
#include <os/utils.h>
#include <ds/list.h>
#include <bus.h>

/* Includes
 * - C-Library */
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <ctype.h>

/* Globals 
 * Keep track of all devices and contracts */
static List_t *__GlbContracts = NULL;
static List_t *__GlbDevices = NULL;
static UUId_t GlbDeviceIdGen = 0, GlbDriverIdGen = 0;
static int GlbInitialized = 0;
static int GlbRun = 0;

/* OnLoad
 * The entry-point of a server, this is called
 * as soon as the server is loaded in the system */
OsStatus_t OnLoad(void)
{
	// Setup list
	__GlbDevices = ListCreate(KeyInteger, LIST_NORMAL);
	__GlbContracts = ListCreate(KeyInteger, LIST_NORMAL);

	// Init variables
	GlbDeviceIdGen = 0;
	GlbDriverIdGen = 0;
	GlbInitialized = 1;
	GlbRun = 1;

	// Register us with server manager
	RegisterServer(__DEVICEMANAGER_TARGET);

	// Enumerate bus controllers/devices */
	return BusEnumerate();
}

/* OnUnload
 * This is called when the server is being unloaded
 * and should free all resources allocated by the system */
OsStatus_t OnUnload(void)
{
	return OsNoError;
}

/* OnEvent
 * This is called when the server recieved an external evnet
 * and should handle the given event*/
OsStatus_t OnEvent(MRemoteCall_t *Message)
{
	// Which function is called?
	switch (Message->Function) {
		// Handles registration of a new device 
		// and store it with a custom version of
		// our own MCoreDevice
		case __DEVICEMANAGER_REGISTERDEVICE: {
			UUId_t Result;

			// Evaluate request, but don't free
			// the allocated device storage, we need it
			if (RegisterDevice((UUId_t)Message->Arguments[0].Data.Value,
				(MCoreDevice_t*)Message->Arguments[1].Data.Buffer, NULL,
				(Flags_t)Message->Arguments[2].Data.Value, &Result) != OsNoError) {
				Result = UUID_INVALID;
			}

			// Write the result back to the caller
			PipeSend(Message->Sender, Message->ResponsePort,
				&Result, sizeof(UUId_t));
		} break;

		// Unregisters a device from the system, and 
		// signals all drivers that are attached to 
		// un-attach
		case __DEVICEMANAGER_UNREGISTERDEVICE: {

		} break;

		// Queries device information and returns
		// information about the device and the drivers
		// attached
		case __DEVICEMANAGER_QUERYDEVICE: {

		} break;

		// What do?
		case __DEVICEMANAGER_IOCTLDEVICE: {
			// Extract argumenters
			MCoreDevice_t *Device = NULL;
			OsStatus_t Result = OsError;
			DataKey_t Key;

			// Lookup device
			Key.Value = (int)Message->Arguments[0].Data.Value;
			Device = ListGetDataByKey(__GlbDevices, Key, 0);

			// Sanitizie
			if (Device != NULL) {
				if (Message->Arguments[1].Data.Value == __DEVICEMANAGER_IOCTL_BUS) {
					Result = BusDeviceIoctl(Device, Message->Arguments[2].Data.Value);
				}
			}

			// Write back response
			PipeSend(Message->Sender, Message->ResponsePort,
				&Result, sizeof(OsStatus_t));
		} break;

		// Registers a driver for the given device 
		// We then store what contracts are related to 
		// which devices in order to keep track
		case __DEVICEMANAGER_REGISTERCONTRACT: {
			// Extract arguments
			MContract_t *Contract = (MContract_t*)
				Message->Arguments[0].Data.Buffer;
			UUId_t Result = UUID_INVALID;

			// Update sender in contract
			Contract->DriverId = Message->Sender;

			// Evaluate request, but don't free
			// the allocated contract storage, we need it
			if (RegisterContract(Contract, &Result) != OsNoError) {
				Result = UUID_INVALID;
			}

			// Write the result back to the caller
			PipeSend(Message->Sender, Message->ResponsePort,
				&Result, sizeof(UUId_t));
		} break;

		// For now this function is un-implemented
		case __DEVICEMANAGER_UNREGISTERCONTRACT: {
			// Not Implemented
		} break;

		// Query a contract for information 
		// This usually redirects a message to
		// the corresponding driver
		case __DEVICEMANAGER_QUERYCONTRACT: {
			// Hold a response buffer
			void *ResponseBuffer = malloc(Message->Result.Length);

			// Query contract
			if (QueryContract((MContractType_t)Message->Arguments[0].Data.Value, 
					(int)Message->Arguments[1].Data.Value,
					(Message->Arguments[2].Type == ARGUMENT_REGISTER) ?
					&Message->Arguments[2].Data.Value : Message->Arguments[2].Data.Buffer,
					Message->Arguments[2].Length,
					(Message->Arguments[3].Type == ARGUMENT_REGISTER) ?
					&Message->Arguments[3].Data.Value : Message->Arguments[3].Data.Buffer,
					Message->Arguments[3].Length,
					(Message->Arguments[4].Type == ARGUMENT_REGISTER) ?
					&Message->Arguments[4].Data.Value : Message->Arguments[4].Data.Buffer,
					Message->Arguments[4].Length,
					ResponseBuffer, Message->Result.Length) == OsNoError) {
				PipeSend(Message->Sender, Message->ResponsePort,
					ResponseBuffer, Message->Result.Length);
			}
			else {
				PipeSend(Message->Sender, Message->ResponsePort, NULL, sizeof(void*));
			}

			// Cleanup
			free(ResponseBuffer);
		} break;

		default: {
		} break;
	}

	return OsNoError;
}

/* Device Registering
 * Allows registering of a new device in the
 * device-manager, and automatically queries
 * for a driver for the new device */
OsStatus_t
RegisterDevice(
	_In_ UUId_t Parent,
	_In_ MCoreDevice_t *Device, 
	_In_ __CONST char *Name,
	_In_ Flags_t Flags,
	_Out_ UUId_t *Id)
{
	// Variables
	MCoreDevice_t *CopyDevice = NULL;
	DataKey_t Key;

	// Not sure what to do with this rn
	_CRT_UNUSED(Parent);

	// Update name and print debug information
	if (Name != NULL) {
		memcpy(&Device->Name[0], Name, strlen(Name));
		TRACE("Registered device %s", Name);
	}

	// Generate id and update out
	Device->Id = GlbDeviceIdGen++;
	Key.Value = (int)Device->Id;
	*Id = Device->Id;

	// Allocate our own copy of the device
	CopyDevice = (MCoreDevice_t*)malloc(sizeof(MCoreDevice_t));
	memcpy(CopyDevice, Device, sizeof(MCoreDevice_t));

	// Add to list
	ListAppend(__GlbDevices, ListCreateNode(Key, Key, CopyDevice));

	// Now, we want to try to find a driver
	// for the new device
	if (Flags & __DEVICEMANAGER_REGISTER_LOADDRIVER) {
		return InstallDriver(CopyDevice);
	}
	
	// Done with task
	return OsNoError;
}

/* RegisterContract
 * Registers the given contact with the device-manager to let
 * the manager know we are handling this device, and what kind
 * of functionality the device supports */
OsStatus_t
RegisterContract(
	_In_ MContract_t *Contract,
	_Out_ UUId_t *Id)
{
	// Variables
	MContract_t *CopyContract = NULL;
	DataKey_t Key;

	// Trace
	TRACE("Registered driver for device %u: %s", 
		Contract->DeviceId, &Contract->Name[0]);

	// Lookup device
	Key.Value = (int)Contract->DeviceId;

	// Sanitize device
	if (ListGetDataByKey(__GlbDevices, Key, 0) == NULL) {
		ERROR("Device id %u was not registered with the device manager",
			Contract->DeviceId);
		return OsError;
	}

	// Generate a new id
	*Id = GlbDriverIdGen++;

	// Update contract id
	Contract->ContractId = *Id;
	Key.Value = (int)*Id;

	// Allocate our own copy of the contract
	CopyContract = (MContract_t*)malloc(sizeof(MContract_t));
	memcpy(CopyContract, Contract, sizeof(MContract_t));

	// Add to list
	ListAppend(__GlbContracts, ListCreateNode(Key, Key, CopyContract));

	// Done
	return OsNoError;
}

/* HandleQuery
 * Handles the generic query function, by resolving
 * the correct driver and asking for data */
OsStatus_t 
QueryContract(_In_ MContractType_t Type, 
			  _In_ int Function,
			  _In_Opt_ __CONST void *Arg0,
			  _In_Opt_ size_t Length0,
			  _In_Opt_ __CONST void *Arg1,
			  _In_Opt_ size_t Length1,
			  _In_Opt_ __CONST void *Arg2,
			  _In_Opt_ size_t Length2,
			  _Out_Opt_ __CONST void *ResultBuffer,
			  _In_Opt_ size_t ResultLength)
{
	// Iterate driver list and find a contract that
	// matches the request
	foreach(cNode, __GlbContracts) {
		MContract_t *Contract = (MContract_t*)cNode->Data;
		if (Contract->Type == Type) {
			return QueryDriver(Contract, Function, 
				Arg0, Length0, Arg1, Length1, Arg2, Length2,
				ResultBuffer, ResultLength);
		}
	}
	return OsError;
}
