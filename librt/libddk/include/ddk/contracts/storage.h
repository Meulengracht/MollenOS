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

#ifndef _CONTRACT_STORAGE_INTERFACE_H_
#define _CONTRACT_STORAGE_INTERFACE_H_

#include <ddk/driver.h>
#include <ddk/ddkdefs.h>

/* Storage device query functions that must be implemented
 * by the storage driver - those can then be used by this interface */
#define __STORAGE_QUERY_STAT                IPC_DECL_FUNCTION(0)
#define __STORAGE_TRANSFER                  IPC_DECL_FUNCTION(1)

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
    _Out_ StorageDescriptor_t*  Descriptor)
{
    MContract_t Contract;

    // Initialise contract details
    Contract.DriverId   = InterfaceId;
    Contract.Type       = ContractStorage;
    Contract.Version    = 1;
    
    // Perform the device query
    return QueryDriver(&Contract, __STORAGE_QUERY_STAT,
        &StorageDeviceId, sizeof(UUId_t), NULL, 0, NULL, 0, 
        Descriptor, sizeof(StorageDescriptor_t));
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
    MContract_t              Contract;
    StorageOperation_t       Operation;
    StorageOperationResult_t Result;
    OsStatus_t               Status;

    // Initialise contract details
    Contract.DriverId       = InterfaceId;
    Contract.Type           = ContractStorage;
    Contract.Version        = 1;

    // Initialize operation details
    Operation.Direction      = Direction;
    Operation.AbsoluteSector = Sector;
    Operation.BufferHandle   = BufferHandle;
    Operation.BufferOffset   = BufferOffset;
    Operation.SectorCount    = SectorCount;
    
    // Perform the query
    Status = QueryDriver(&Contract, __STORAGE_TRANSFER,
        &StorageDeviceId, sizeof(UUId_t), 
        &Operation, sizeof(StorageOperation_t), NULL, 0, 
        &Result, sizeof(StorageOperationResult_t));
    if (Status != OsSuccess) {
        return Status;
    }
    
    *SectorsTransferred = Result.SectorsTransferred;
    return Result.Status;
}

#endif //!_CONTRACT_STORAGE_INTERFACE_H_
