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

#include <ddk/contracts/storage.h>
#include <os/mollenos.h>
#include <ddk/utils.h>
#include "msd.h"
#include <ds/collection.h>
#include <string.h>
#include <stdlib.h>

static Collection_t *GlbMsdDevices = NULL;

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

OsStatus_t
OnLoad(void)
{
    // Initialize state for this driver
    GlbMsdDevices = CollectionCreate(KeyId);
    return UsbInitialize();
}

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

OsStatus_t
OnUnregister(
    _In_ MCoreDevice_t *Device)
{
    MsdDevice_t *MsdDevice;
    DataKey_t Key = { .Value.Id = Device->Id };

    // Lookup controller
    MsdDevice = (MsdDevice_t*)
        CollectionGetDataByKey(GlbMsdDevices, Key, 0);

    // Sanitize lookup
    if (MsdDevice == NULL) {
        return OsError;
    }

    CollectionRemoveByKey(GlbMsdDevices, Key);
    return MsdDeviceDestroy(MsdDevice);
}

OsStatus_t 
OnQuery(
	_In_     MContractType_t        QueryType, 
	_In_     int                    QueryFunction, 
	_In_Opt_ MRemoteCallArgument_t* Arg0,
	_In_Opt_ MRemoteCallArgument_t* Arg1,
	_In_Opt_ MRemoteCallArgument_t* Arg2,
    _In_     MRemoteCallAddress_t*  Address)
{
    _CRT_UNUSED(Arg2);
    
    TRACE("MSD.OnQuery(Function %i)", QueryFunction);
    if (QueryType != ContractStorage) {
        return OsError;
    }

    switch (QueryFunction) {
        case __STORAGE_QUERY_STAT: {
            StorageDescriptor_t NullDescriptor = { 0 };
            MsdDevice_t*        Device         = NULL;
            DataKey_t           Key            = { .Value.Id = Arg0->Data.Value };
            
            Device = (MsdDevice_t*)CollectionGetDataByKey(GlbMsdDevices, Key, 0);
            if (Device != NULL) {
                return RPCRespond(Address, (void*)&Device->Descriptor, sizeof(StorageDescriptor_t));
            }
            else {
                return RPCRespond(Address, (void*)&NullDescriptor, sizeof(StorageDescriptor_t));
            }
        } break;

        // Read or write sectors from a disk identifier
        // They have same parameters with different direction
        case __STORAGE_TRANSFER: {
            StorageOperation_t*      Operation = (StorageOperation_t*)Arg1->Data.Buffer;
            DataKey_t                Key       = { .Value.Id = Arg0->Data.Value };
            StorageOperationResult_t Result    = { .Status = OsInvalidParameters };
            MsdDevice_t*             Device;
            
            Device = (MsdDevice_t*)CollectionGetDataByKey(GlbMsdDevices, Key, 0);
            if (Device == NULL) {
                return RPCRespond(Address, (void*)&Result, sizeof(StorageOperationResult_t));
            }

            // Determine the kind of operation
            if (Operation->Direction == __STORAGE_OPERATION_READ) {
                Result.Status = MsdReadSectors(Device, Operation->AbsoluteSector, Operation->BufferHandle, 
                    Operation->BufferOffset, Operation->SectorCount, &Result.SectorsTransferred);
            }
            else if (Operation->Direction == __STORAGE_OPERATION_WRITE) {
                Result.Status = MsdWriteSectors(Device, Operation->AbsoluteSector, Operation->BufferHandle, 
                    Operation->BufferOffset, Operation->SectorCount, &Result.SectorsTransferred);
            }
            return RPCRespond(Address, (void*)&Result, sizeof(StorageOperationResult_t));
        } break;

        // Other cases not supported
        default: {
            return OsError;
        }
    }
}
