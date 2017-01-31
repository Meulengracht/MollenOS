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
#include <os/driver/contracts/base.h>
#include <os/osdefs.h>

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

	/* Initialize RPC request */
	RPCInitialize(&Request, __DEVICEMANAGER_INTERFACE_VERSION,
		PIPE_DEFAULT, __DEVICEMANAGER_QUERYCONTRACT);

	RPCSetArgument(&Request, 0, (const void*)&Type, sizeof(MContractType_t));
	RPCSetResult(&Request, time, sizeof(struct tm));
	return RPCEvaluate(&Request, __DEVICEMANAGER_TARGET);
}

/* DiskRead */

/* DiskWrite */

#endif //!_CONTRACT_DISK_INTERFACE_H_
