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

/* Includes
 * - System */
#include <os/driver/contracts/base.h>
#include <os/driver/driver.h>
#include <os/mollenos.h>
#include <ds/list.h>
#include <bus.h>

/* Includes
 * - C-Library */
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <ctype.h>

/* Globals */
List_t *GlbDeviceList = NULL;
List_t *GlbDriverList = NULL;
UUId_t GlbDeviceIdGen = 0, GlbDriverIdGen = 0;
int GlbInitialized = 0;
int GlbRun = 0;

/* OnLoad
 * The entry-point of a server, this is called
 * as soon as the server is loaded in the system */
OsStatus_t OnLoad(void)
{
	/* Setup list */
	GlbDeviceList = ListCreate(KeyInteger, LIST_NORMAL);
	GlbDriverList = ListCreate(KeyInteger, LIST_NORMAL);

	/* Init variables */
	GlbDeviceIdGen = 0;
	GlbDriverIdGen = 0;
	GlbInitialized = 1;
	GlbRun = 1;

	/* Register us with server manager */
	RegisterServer(__DEVICEMANAGER_TARGET);

	/* Enumerate bus controllers/devices */
	BusEnumerate();

	/* No error! */
	return OsNoError;
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
	/* Which function is called? */
	switch (Message->Function)
	{
		/* Handles registration of a new device 
		 * and store it with a custom version of
		 * our own MCoreDevice */
		case __DEVICEMANAGER_REGISTERDEVICE: {
			/* Variables for result */
			UUId_t Result;

			/* Evaluate request, but don't free
			* the allocated device storage, we need it */
			Message->Arguments[0].Type = ARGUMENT_NOTUSED;
			Result = RegisterDevice((UUId_t)Message->Arguments[0].Data.Value,
				(MCoreDevice_t*)Message->Arguments[1].Data.Buffer, NULL,
				(Flags_t)Message->Arguments[2].Data.Value);

			/* Write the result back to the caller */
			PipeSend(Message->Sender, Message->ResponsePort,
				&Result, sizeof(UUId_t));
		} break;

		/* Unregisters a device from the system, and 
		 * signals all drivers that are attached to 
		 * un-attach */
		case __DEVICEMANAGER_UNREGISTERDEVICE: {

		} break;

		/* Queries device information and returns
		 * information about the device and the drivers
		 * attached */
		case __DEVICEMANAGER_QUERYDEVICE: {

		} break;

		/* What do? */
		case __DEVICEMANAGER_IOCTLDEVICE: {

		} break;

		/* Registers a driver for the given device 
		 * We then store what contracts are related to 
		 * which devices in order to keep track */
		case __DEVICEMANAGER_REGISTERCONTRACT: {
			/* Variables for result */
			MContract_t *Contract = (MContract_t*)
				Message->Arguments[0].Data.Buffer;
			UUId_t Result = 1;

			/* Update sender in contract */
			Contract->DriverId = Message->Sender;

			/* Evaluate request, but don't free
			 * the allocated contract storage, we need it */
			Message->Arguments[0].Type = ARGUMENT_NOTUSED;
			Result = RegisterContract(Contract);

			/* Write the result back to the caller */
			PipeSend(Message->Sender, Message->ResponsePort,
				&Result, sizeof(UUId_t));
		} break;

		/* For now this function is un-implemented */
		case __DEVICEMANAGER_UNREGISTERCONTRACT: {
			/* Not-Implemented */
		} break;

		/* Query a contract for information 
		 * This usually redirects a message to
		 * the corresponding driver */
		case __DEVICEMANAGER_QUERYCONTRACT: {
			/* Variables for result */
			void *ResponseBuffer = malloc(Message->Result.Length);

			/* Run the query */
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

			free(ResponseBuffer);
		} break;

		default: {
		} break;
	}

	/* Done! */
	return OsNoError;
}

/* Device Registering
 * Allows registering of a new device in the
 * device-manager, and automatically queries
 * for a driver for the new device */
UUId_t
RegisterDevice(
	_In_ UUId_t Parent,
	_In_ MCoreDevice_t *Device, 
	_In_ __CONST char *Name,
	_In_ Flags_t Flags)
{
	/* Variables */
	UUId_t DeviceId = GlbDeviceIdGen++;
	DataKey_t Key;

	// Not sure what to do with this rn
	_CRT_UNUSED(Parent);

	/* Update name and print debug information */
	if (Name != NULL) {
		memcpy(&Device->Name[0], Name, strlen(Name));
		//MollenOSSystemLog("Registered device %s", Name);
	}

	/* Update id, add to list */
	Key.Value = DeviceId;
	Device->Id = DeviceId;
	ListAppend(GlbDeviceList, ListCreateNode(Key, Key, Device));

	/* Now, we want to try to find a driver
	 * for the new device */
	if (Flags & __DEVICEMANAGER_REGISTER_LOADDRIVER) {
		if (InstallDriver(Device) == OsError) {
		}
	}
	
	/* Done with processing of the new device */
	return DeviceId;
}

/* RegisterContract
 * Registers the given contact with the device-manager to let
 * the manager know we are handling this device, and what kind
 * of functionality the device supports */
UUId_t RegisterContract(MContract_t *Contract)
{
	/* Variables */	
	UUId_t ContractId = GlbDriverIdGen++;
	DataKey_t Key;

	/* Debug name */
	MollenOSSystemLog("Registered driver for device %u: %s", 
		Contract->DeviceId, &Contract->Name[0]);

	/* Update id, add to list */
	Contract->ContractId = ContractId;
	Key.Value = ContractId;
	ListAppend(GlbDriverList, ListCreateNode(Key, Key, Contract));

	/* Done with processing of the new driver */
	return ContractId;
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
	/* Iterate driver nodes */
	foreach(cNode, GlbDriverList) {
		MContract_t *Contract = (MContract_t*)cNode->Data;
		if (Contract->Type == Type) {
			return QueryDriver(Contract, Function, 
				Arg0, Length0, Arg1, Length1, Arg2, Length2,
				ResultBuffer, ResultLength);
		}
	}

	/* Done, if we reach here no fun */
	return OsError;
}
