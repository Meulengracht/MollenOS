/**
 * MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 * Mass Storage Device Driver (Generic)
 */
//#define __TRACE

#include <ddk/utils.h>
#include <ds/collection.h>
#include <os/mollenos.h>
#include "hid.h"
#include <string.h>
#include <stdlib.h>

static Collection_t *GlbHidDevices = NULL;

void GetModuleIdentifiers(unsigned int* vendorId, unsigned int* deviceId,
    unsigned int* class, unsigned int* subClass)
{
    *vendorId = 0;
    *deviceId = 0;
    *class    = 0xCABB;
    *subClass = 0x30000;
}

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

/* OnLoad
 * The entry-point of a driver, this is called
 * as soon as the driver is loaded in the system */
OsStatus_t
OnLoad(void)
{
	// Initialize state for this driver
    GlbHidDevices = CollectionCreate(KeyId);
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
    _In_ Device_t *Device)
{
	HidDevice_t*    HidDevice = NULL;
	DataKey_t       Key = { .Value.Id = Device->Id };
	
	// Register the new controller
	HidDevice = HidDeviceCreate((UsbDevice_t*)Device);

	// Sanitize
	if (HidDevice == NULL) {
		return OsError;
	}
	CollectionAppend(GlbHidDevices, CollectionCreateNode(Key, HidDevice));
	return OsSuccess;
}

/* OnUnregister
 * Is called when the device-manager wants to unload
 * an instance of this driver from the system */
OsStatus_t
OnUnregister(
    _In_ Device_t *Device)
{
	HidDevice_t*    HidDevice = NULL;
	DataKey_t       Key = { .Value.Id = Device->Id };

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
