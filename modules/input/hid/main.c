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
 * MollenOS MCore - Mass Storage Device Driver (Generic)
 */
//#define __TRACE

/* Includes 
 * - System */
#include <os/contracts/storage.h>
#include <os/mollenos.h>
#include <os/utils.h>
#include "hid.h"

/* Includes
 * - Library */
#include <ds/collection.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* Globals
 * State-tracking variables */
static Collection_t *GlbHidDevices = NULL;

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
    // We don't use all params
    _CRT_UNUSED(Arg2);
    return HidInterrupt((HidDevice_t*)InterruptData, 
        (UsbTransferStatus_t)Arg0, Arg1);
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
OsStatus_t
OnLoad(void)
{
	// Initialize state for this driver
    GlbHidDevices = CollectionCreate(KeyInteger);
    return UsbInitialize();
}

/* OnUnload
 * This is called when the driver is being unloaded
 * and should free all resources allocated by the system */
OsStatus_t
OnUnload(void)
{
	// Iterate registered controllers
	foreach(cNode, GlbHidDevices) {
		HidDeviceDestroy((HidDevice_t*)cNode->Data);
	}

	// Data is now cleaned up, destroy list
    CollectionDestroy(GlbHidDevices);
    return UsbCleanup();
}

/* OnRegister
 * Is called when the device-manager registers a new
 * instance of this driver for the given device */
OsStatus_t
OnRegister(
    _In_ MCoreDevice_t *Device)
{
	// Variables
	HidDevice_t *HidDevice = NULL;
	DataKey_t Key;
	
	// Register the new controller
	HidDevice = HidDeviceCreate((MCoreUsbDevice_t*)Device);

	// Sanitize
	if (HidDevice == NULL) {
		return OsError;
	}

	// Use the device-id as key
	Key.Value = (int)Device->Id;

	// Append the controller to our list
	CollectionAppend(GlbHidDevices, CollectionCreateNode(Key, HidDevice));

	// Done - no error
	return OsSuccess;
}

/* OnUnregister
 * Is called when the device-manager wants to unload
 * an instance of this driver from the system */
OsStatus_t
OnUnregister(
    _In_ MCoreDevice_t *Device)
{
	// Variables
	HidDevice_t *HidDevice = NULL;
	DataKey_t Key;

	// Set the key to the id of the device to find
	// the bound controller
	Key.Value = (int)Device->Id;

	// Lookup controller
	HidDevice = (HidDevice_t*)
		CollectionGetDataByKey(GlbHidDevices, Key, 0);

	// Sanitize lookup
	if (HidDevice == NULL) {
		return OsError;
	}

	// Remove node from list
	CollectionRemoveByKey(GlbHidDevices, Key);

	// Destroy it
	return HidDeviceDestroy(HidDevice);
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
    _CRT_UNUSED(QueryType);
    _CRT_UNUSED(QueryFunction);
    _CRT_UNUSED(Arg0);
    _CRT_UNUSED(Arg1);
    _CRT_UNUSED(Arg2);
    _CRT_UNUSED(Queryee);
    _CRT_UNUSED(ResponsePort);
    return OsError;
}
