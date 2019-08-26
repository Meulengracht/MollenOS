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
 * Contract Definitions & Structures (Storage Contract)
 * - This header describes the base contract-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __DDK_CONTRACT_STORAGE_H__
#define __DDK_CONTRACT_STORAGE_H__

#include <ddk/contracts/base.h>
#include <ddk/driver.h>
#include <ddk/ddkdefs.h>
#include <os/ipc.h>

/* Storage device query functions that must be implemented
 * by the storage driver - those can then be used by this interface */
#define __STORAGE_QUERY_STAT                (int)0
#define __STORAGE_TRANSFER                  (int)1

#define __STORAGE_OPERATION_READ            0x00000001
#define __STORAGE_OPERATION_WRITE           0x00000002

PACKED_TYPESTRUCT(StorageDescriptor, {
    UUId_t   Device;
    UUId_t   Driver;
    Flags_t  Flags;
    char     Model[64];
    char     Serial[32];
    size_t   SectorSize;
    uint64_t SectorCount;
    size_t   SectorsPerCylinder;
    size_t   LUNCount;
});

PACKED_TYPESTRUCT(StorageOperation, {
    int      Direction;
    uint64_t AbsoluteSector;
    UUId_t   BufferHandle;
    size_t   BufferOffset;
    size_t   SectorCount;
});

PACKED_TYPESTRUCT(StorageOperationResult, {
    OsStatus_t Status;
    size_t     SectorsTransferred;
});

/**
 * StorageQuery
 * * This queries the storage device interface for information and geometry stats
 */
SERVICEAPI OsStatus_t SERVICEABI
StorageQuery(
    _In_  UUId_t                StorageDeviceId,
    _In_  UUId_t                InterfaceId,
    _Out_ StorageDescriptor_t** Descriptor)
{
    IpcMessage_t Message;
    OsStatus_t   Status;
    
    IpcInitialize(&Message);
    
    IPC_SET_TYPED(&Message, 0, __DRIVER_QUERYCONTRACT);
    IPC_SET_TYPED(&Message, 1, __STORAGE_QUERY_STAT);
    IPC_SET_TYPED(&Message, 2, ContractStorage);
    IPC_SET_TYPED(&Message, 3, StorageDeviceId);
    
    Status = IpcInvoke(InterfaceId, &Message, 0, 0, (void**)Descriptor);
    return Status;
}

/**
 * StorageTransfer 
 * * Sends a transfer request to the given storage-medium, and attempts to
 * * transfer the number of bytes requested into the given buffer at the absolute sector given 
 * @param Direction [In] __STORAGE_OPERATION_READ or __STORAGE_OPERATION_WRITE
 */
SERVICEAPI OsStatus_t SERVICEABI
StorageTransfer(
    _In_  UUId_t    StorageDeviceId,
    _In_  UUId_t    InterfaceId,
    _In_  int       Direction,
    _In_  uint64_t  Sector,
    _In_  UUId_t    BufferHandle,
    _In_  size_t    BufferOffset,
    _In_  size_t    SectorCount,
    _Out_ size_t*   SectorsTransferred)
{
    IpcMessage_t              Message;
    StorageOperation_t        Operation;
    StorageOperationResult_t* Result;
    OsStatus_t                Status;
    
    // Initialize operation details
    Operation.Direction      = Direction;
    Operation.AbsoluteSector = Sector;
    Operation.BufferHandle   = BufferHandle;
    Operation.BufferOffset   = BufferOffset;
    Operation.SectorCount    = SectorCount;
    
    IpcInitialize(&Message);
    
    IPC_SET_TYPED(&Message, 0, __DRIVER_QUERYCONTRACT);
    IPC_SET_TYPED(&Message, 1, __STORAGE_TRANSFER);
    IPC_SET_TYPED(&Message, 2, ContractStorage);
    IPC_SET_TYPED(&Message, 3, StorageDeviceId);
    
    IpcSetUntypedArgument(&Message, 0, &Operation, sizeof(StorageOperation_t));
    
    Status = IpcInvoke(InterfaceId, &Message, 0, 0, (void**)&Result);
    if (Status != OsSuccess) {
        return Status;
    }
    
    *SectorsTransferred = Result->SectorsTransferred;
    return Result->Status;
}

#endif //!__DDK_CONTRACT_STORAGE_H__
