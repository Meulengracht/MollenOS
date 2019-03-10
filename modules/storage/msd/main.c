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

#include <ddk/contracts/storage.h>
#include <os/mollenos.h>
#include <ddk/utils.h>
#include "msd.h"
#include <ds/collection.h>
#include <string.h>
#include <stdlib.h>

static Collection_t *GlbMsdDevices = NULL;

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
OsStatus_t
OnLoad(void)
{
    // Initialize state for this driver
    GlbMsdDevices = CollectionCreate(KeyId);
    return UsbInitialize();
}

/* OnUnload
 * This is called when the driver is being unloaded
 * and should free all resources allocated by the system */
OsStatus_t
OnUnload(void)
{
    // Iterate registered controllers
    foreach(cNode, GlbMsdDevices) {
        MsdDeviceDestroy((MsdDevice_t*)cNode->Data);
    }

    // Data is now cleaned up, destroy list
    CollectionDestroy(GlbMsdDevices);
    return UsbCleanup();
}

/* OnRegister
 * Is called when the device-manager registers a new
 * instance of this driver for the given device */
OsStatus_t
OnRegister(
    _In_ MCoreDevice_t *Device)
{
    MsdDevice_t *MsdDevice = NULL;
    DataKey_t Key = { .Value.Id = Device->Id };
    
    // Register the new controller
    MsdDevice = MsdDeviceCreate((MCoreUsbDevice_t*)Device);

    // Sanitize
    if (MsdDevice == NULL) {
        return OsError;
    }

    CollectionAppend(GlbMsdDevices, CollectionCreateNode(Key, MsdDevice));
    return OsSuccess;
}

/* OnUnregister
 * Is called when the device-manager wants to unload
 * an instance of this driver from the system */
OsStatus_t
OnUnregister(
    _In_ MCoreDevice_t *Device)
{
    MsdDevice_t *MsdDevice = NULL;
    DataKey_t Key = { .Value.Id = Device->Id };

    // Lookup controller
    MsdDevice = (MsdDevice_t*)
        CollectionGetDataByKey(GlbMsdDevices, Key, 0);

    // Sanitize lookup
    if (MsdDevice == NULL) {
        return OsError;
    }

    // Remove node from list
    CollectionRemoveByKey(GlbMsdDevices, Key);

    // Destroy it
    return MsdDeviceDestroy(MsdDevice);
}

/* OnQuery
 * Occurs when an external process or server quries
 * this driver for data, this will correspond to the query
 * function that is defined in the contract */
OsStatus_t 
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

    // Debug
    TRACE("MSD.OnQuery(Function %i)", QueryFunction);

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
            StorageDescriptor_t     NullDescriptor;
            MsdDevice_t *Device     = NULL;
            DataKey_t Key = { .Value.Id = Arg0->Data.Value };
            Device      = (MsdDevice_t*)CollectionGetDataByKey(GlbMsdDevices, Key, 0);

            // Write the descriptor back
            if (Device != NULL) {
                return RPCRespond(Address, (void*)&Device->Descriptor, sizeof(StorageDescriptor_t));
            }
            else {
                memset((void*)&NullDescriptor, 0, sizeof(StorageDescriptor_t));
                return RPCRespond(Address, (void*)&NullDescriptor, sizeof(StorageDescriptor_t));
            }
        } break;

        // Read or write sectors from a disk identifier
        // They have same parameters with different direction
        case __STORAGE_QUERY_WRITE:
        case __STORAGE_QUERY_READ: {
            // Get parameters
            StorageOperation_t *Operation = (StorageOperation_t*)Arg1->Data.Buffer;
            MsdDevice_t *Device = NULL;
            DataKey_t Key = { .Value.Id = Arg0->Data.Value };
            Device = (MsdDevice_t*)CollectionGetDataByKey(GlbMsdDevices, Key, 0);

            // Determine the kind of operation
            if (Device != NULL
                && Operation->Direction == __STORAGE_OPERATION_READ) {
                if (MsdReadSectors(Device, Operation->AbsSector, Operation->PhysicalBuffer, 
                    Operation->SectorCount * Device->Descriptor.SectorSize, NULL) != OsSuccess) {
                    OsStatus_t Result = OsError;
                    return RPCRespond(Address, (void*)&Result, sizeof(OsStatus_t));
                }
                else {
                    OsStatus_t Result = OsSuccess;
                    return RPCRespond(Address, (void*)&Result, sizeof(OsStatus_t));
                }
            }
            else if (Device != NULL
                && Operation->Direction == __STORAGE_OPERATION_WRITE) {
                if (MsdWriteSectors(Device, Operation->AbsSector, Operation->PhysicalBuffer, 
                    Operation->SectorCount * Device->Descriptor.SectorSize, NULL) != OsSuccess) {
                    OsStatus_t Result = OsError;
                    return RPCRespond(Address, (void*)&Result, sizeof(OsStatus_t));
                }
                else {
                    OsStatus_t Result = OsSuccess;
                    return RPCRespond(Address, (void*)&Result, sizeof(OsStatus_t));
                }
            }
            else {
                OsStatus_t Result = OsError;
                return RPCRespond(Address, (void*)&Result, sizeof(OsStatus_t));
            }
        } break;

        // Other cases not supported
        default: {
            return OsError;
        }
    }
}
