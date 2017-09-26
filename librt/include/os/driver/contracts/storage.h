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
 * MollenOS MCore - Contract Definitions & Structures (Storage Contract)
 * - This header describes the base contract-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _CONTRACT_STORAGE_INTERFACE_H_
#define _CONTRACT_STORAGE_INTERFACE_H_

/* Includes 
 * - System */
#include <os/driver/driver.h>
#include <os/osdefs.h>

/* Storage device query functions that must be implemented
 * by the storage driver - those can then be used by this interface */
#define __STORAGE_QUERY_STAT				IPC_DECL_FUNCTION(0)
#define __STORAGE_QUERY_READ				IPC_DECL_FUNCTION(1)
#define __STORAGE_QUERY_WRITE				IPC_DECL_FUNCTION(2)

#define __STORAGE_OPERATION_READ			0x00000001
#define __STORAGE_OPERATION_WRITE			0x00000002

/* The Storage descriptor structure 
 * contains geometric and generic information
 * about the given storage-medium */
PACKED_TYPESTRUCT(StorageDescriptor, {
	UUId_t				Device;
	UUId_t				Driver;
	Flags_t				Flags;
	char				Model[64];
	char				Serial[32];
	size_t				SectorSize;
    uint64_t			SectorCount;
    size_t              SectorsPerCylinder;
    size_t              LUNCount;
});

/* The storage operation structure 
 * contains information related to storage-mediums operations
 * like read and write */
PACKED_TYPESTRUCT(StorageOperation, {
	int					Direction;
	uint64_t			AbsSector;
	uintptr_t			PhysicalBuffer;
	size_t				SectorCount;
});

/* StorageQuery
 * This queries the storage contract for data
 * and must be implemented by all contracts that
 * implement the storage interface */
SERVICEAPI
OsStatus_t
SERVICEABI
StorageQuery(
	_In_ UUId_t Driver, 
	_In_ UUId_t StorageDevice,
	_Out_ StorageDescriptor_t *Descriptor)
{
	/* Variables */
	MContract_t Contract;

	/* Setup contract stuff for request */
	Contract.DriverId = Driver;
	Contract.Type = ContractStorage;
	Contract.Version = __DEVICEMANAGER_INTERFACE_VERSION;

	/* Query the driver directly */
	return QueryDriver(&Contract, __STORAGE_QUERY_STAT,
		&StorageDevice, sizeof(UUId_t), NULL, 0, NULL, 0, 
		Descriptor, sizeof(StorageDescriptor_t));
}

/* StorageRead 
 * Sends a read request to the given storage-medium, and attempts to
 * read the number of bytes requested into the given buffer 
 * at the absolute sector given 
 * @PhysicalAddress - Must be the contigious physical address
 *                    buffer to read data into */
SERVICEAPI
OsStatus_t
SERVICEABI
StorageRead(
	_In_ UUId_t Driver, 
	_In_ UUId_t StorageDevice,
	_In_ uint64_t Sector, 
	_Out_ uintptr_t PhysicalAddress, 
	_In_ size_t SectorCount)
{
	/* Variables */
	MContract_t Contract;
	StorageOperation_t Operation;
	OsStatus_t Result = OsSuccess;

	/* Setup contract stuff for request */
	Contract.DriverId = Driver;
	Contract.Type = ContractStorage;
	Contract.Version = __DEVICEMANAGER_INTERFACE_VERSION;

	/* Initialize the operation */
	Operation.Direction = __STORAGE_OPERATION_READ;
	Operation.AbsSector = Sector;
	Operation.PhysicalBuffer = PhysicalAddress;
	Operation.SectorCount = SectorCount;

	/* Query the driver directly */
	QueryDriver(&Contract, __STORAGE_QUERY_READ,
		&StorageDevice, sizeof(UUId_t), &Operation, sizeof(StorageOperation_t),
		NULL, 0, &Result, sizeof(OsStatus_t));
	return Result;
}

/* StorageWrite
 * Sends a write request to the given storage-medium, and attempts to
 * write the number of bytes requested from the given buffer
 * at the absolute sector given. 
 * @PhysicalAddress - Must be the contigious physical address
 *                    buffer that contains the data to write */
SERVICEAPI
OsStatus_t
SERVICEABI
StorageWrite(
	_In_ UUId_t Driver,
	_In_ UUId_t StorageDevice,
	_In_ uint64_t Sector, 
	_Out_ uintptr_t PhysicalAddress,
	_In_ size_t SectorCount)
{
	/* Variables */
	MContract_t Contract;
	StorageOperation_t Operation;
	OsStatus_t Result = OsSuccess;

	/* Setup contract stuff for request */
	Contract.DriverId = Driver;
	Contract.Type = ContractStorage;
	Contract.Version = __DEVICEMANAGER_INTERFACE_VERSION;

	/* Initialize the operation */
	Operation.Direction = __STORAGE_OPERATION_WRITE;
	Operation.AbsSector = Sector;
	Operation.PhysicalBuffer = PhysicalAddress;
	Operation.SectorCount = SectorCount;

	/* Query the driver directly */
	QueryDriver(&Contract, __STORAGE_QUERY_WRITE,
		&StorageDevice, sizeof(UUId_t), &Operation, sizeof(StorageOperation_t),
		NULL, 0, &Result, sizeof(OsStatus_t));
	return Result;
}

#endif //!_CONTRACT_STORAGE_INTERFACE_H_
