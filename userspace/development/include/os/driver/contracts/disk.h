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

/* The disk descriptor structure 
 * contains geometric and generic information
 * about the given disk */
typedef struct _DiskDescriptor {
	UUId_t				Device;
	UUId_t				Driver;
	Flags_t				Flags;
	char				Manufactor[1];
	char				Model[1];
	size_t				SectorSize;
	uint64_t			SectorCount;
} DiskDescriptor_t;

/* DiskQuery
 * This queries the disk contract for data
 * and must be implemented by all contracts that
 * implement the disk interface */
__DEVAPI OsStatus_t DiskQuery(UUId_t Disk, UUId_t Driver, 
	DiskDescriptor_t *Descriptor)
{
	/* Variables */
	MRemoteCall_t Request;
	MContractType_t Type = ContractDisk;
	int TargetFunction, Function = __DISK_QUERY_STAT;
	UUId_t Target;

	/* Determine some direct driver variables */
	if (Driver == UUID_INVALID) {
		TargetFunction = __DEVICEMANAGER_QUERYCONTRACT;
		Target = __DEVICEMANAGER_TARGET;
	}
	else {
		TargetFunction = __DRIVER_QUERY;
		Target = Driver;
	}

	/* Initialize RPC request */
	RPCInitialize(&Request, __DEVICEMANAGER_INTERFACE_VERSION,
		PIPE_DEFAULT, TargetFunction);
	RPCSetArgument(&Request, 0, (const void*)&Type, sizeof(MContractType_t));
	RPCSetArgument(&Request, 1, (const void*)&Function, sizeof(int));
	RPCSetResult(&Request, Descriptor, sizeof(DiskDescriptor_t));
	return RPCEvaluate(&Request, Target);
}

/* DiskRead 
 * Sends a read request to the given disk, and attempts to
 * read the number of bytes requested into the given buffer */


/* DiskWrite */

#endif //!_CONTRACT_DISK_INTERFACE_H_
