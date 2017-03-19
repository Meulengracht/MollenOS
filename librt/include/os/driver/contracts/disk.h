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
 * MollenOS MCore - Contract Definitions & Structures (Disk Contract)
 * - This header describes the base contract-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _CONTRACT_DISK_INTERFACE_H_
#define _CONTRACT_DISK_INTERFACE_H_

/* Includes 
 * - System */
#include <os/driver/driver.h>
#include <os/osdefs.h>

/* Disk device query functions that must be implemented
 * by the disk driver - those can then be used by this interface */
#define __DISK_QUERY_STAT				IPC_DECL_FUNCTION(0)
#define __DISK_QUERY_READ				IPC_DECL_FUNCTION(1)
#define __DISK_QUERY_WRITE				IPC_DECL_FUNCTION(2)

#define __DISK_OPERATION_READ			0x00000001
#define __DISK_OPERATION_WRITE			0x00000002

/* The disk descriptor structure 
 * contains geometric and generic information
 * about the given disk */
PACKED_TYPESTRUCT(DiskDescriptor, {
	UUId_t				Device;
	UUId_t				Driver;
	Flags_t				Flags;
	char				Manufactor[32];
	char				Model[64];
	size_t				SectorSize;
	uint64_t			SectorCount;
});

/* The disk operation structure 
 * contains information related to disk operations
 * like read and write */
PACKED_TYPESTRUCT(DiskOperation, {
	int					Direction;
	uint64_t			AbsSector;
	uintptr_t			PhysicalBuffer;
	size_t				SectorCount;
});

/* DiskQuery
 * This queries the disk contract for data
 * and must be implemented by all contracts that
 * implement the disk interface */
SERVICEAPI
OsStatus_t
SERVICEABI
DiskQuery(
	_In_ UUId_t Driver, 
	_In_ UUId_t Disk,
	_Out_ DiskDescriptor_t *Descriptor)
{
	/* Variables */
	MContract_t Contract;

	/* Setup contract stuff for request */
	Contract.DriverId = Driver;
	Contract.Type = ContractDisk;
	Contract.Version = __DEVICEMANAGER_INTERFACE_VERSION;

	/* Query the driver directly */
	return QueryDriver(&Contract, __DISK_QUERY_STAT,
		&Disk, sizeof(UUId_t), NULL, 0, NULL, 0, 
		Descriptor, sizeof(DiskDescriptor_t));
}

/* DiskRead 
 * Sends a read request to the given disk, and attempts to
 * read the number of bytes requested into the given buffer 
 * at the absolute sector given 
 * @PhysicalAddress - Must be the contigious physical address
 *                    buffer to read data into */
SERVICEAPI
OsStatus_t
SERVICEABI
DiskRead(
	_In_ UUId_t Driver, 
	_In_ UUId_t Disk,
	_In_ uint64_t Sector, 
	_Out_ uintptr_t PhysicalAddress, 
	_In_ size_t SectorCount)
{
	/* Variables */
	MContract_t Contract;
	DiskOperation_t Operation;
	OsStatus_t Result;

	/* Setup contract stuff for request */
	Contract.DriverId = Driver;
	Contract.Type = ContractDisk;
	Contract.Version = __DEVICEMANAGER_INTERFACE_VERSION;

	/* Initialize the operation */
	Operation.Direction = __DISK_OPERATION_READ;
	Operation.AbsSector = Sector;
	Operation.PhysicalBuffer = PhysicalAddress;
	Operation.SectorCount = SectorCount;

	/* Query the driver directly */
	QueryDriver(&Contract, __DISK_QUERY_READ,
		&Disk, sizeof(UUId_t), &Operation, sizeof(DiskOperation_t),
		NULL, 0, &Result, sizeof(OsStatus_t));
	return Result;
}

/* DiskWrite
 * Sends a write request to the given disk, and attempts to
 * write the number of bytes requested from the given buffer
 * at the absolute sector given. 
 * @PhysicalAddress - Must be the contigious physical address
 *                    buffer that contains the data to write */
SERVICEAPI
OsStatus_t
SERVICEABI
DiskWrite(
	_In_ UUId_t Driver,
	_In_ UUId_t Disk,
	_In_ uint64_t Sector, 
	_Out_ uintptr_t PhysicalAddress,
	_In_ size_t SectorCount)
{
	/* Variables */
	MContract_t Contract;
	DiskOperation_t Operation;
	OsStatus_t Result;

	/* Setup contract stuff for request */
	Contract.DriverId = Driver;
	Contract.Type = ContractDisk;
	Contract.Version = __DEVICEMANAGER_INTERFACE_VERSION;

	/* Initialize the operation */
	Operation.Direction = __DISK_OPERATION_WRITE;
	Operation.AbsSector = Sector;
	Operation.PhysicalBuffer = PhysicalAddress;
	Operation.SectorCount = SectorCount;

	/* Query the driver directly */
	QueryDriver(&Contract, __DISK_QUERY_WRITE,
		&Disk, sizeof(UUId_t), &Operation, sizeof(DiskOperation_t),
		NULL, 0, &Result, sizeof(OsStatus_t));
	return Result;
}

#endif //!_CONTRACT_DISK_INTERFACE_H_
