/* MollenOS
*
* Copyright 2011 - 2016, Philip Meulengracht
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
* MollenOS MCore - Disk Partition structures
*/
#ifndef _MCORE_VFS_PARTITION_H_
#define _MCORE_VFS_PARTITION_H_

/* Includes */
#include <crtdefs.h>
#include <stdint.h>

/* Definitions */
#define PARTITION_INACTIVE		0x00
#define PARTITION_ACTIVE		0x80


/* Types */
#define PARTITION_EMPTY			0x00
#define PARTITION_EXTENDED		0x05
#define PARTITION_EXTENDED2		0x0F

/* Structures */
#pragma pack(push, 1)
typedef struct _MCorePartitionEntry
{
	/* Status - Bit 7 is set if bootable */
	uint8_t Status;

	/* CHS Details */
	uint8_t StartHead;
	uint8_t StartSector;
	uint8_t StartCylinder;

	/* Partition Type */
	uint8_t Type;

	/* CHS Details */
	uint8_t EndHead;
	uint8_t EndSector;
	uint8_t EndCylinder;

	/* Lba */
	uint32_t LbaSector;
	uint32_t LbaSize;

} MCorePartitionEntry_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct _MCoreMasterBootRecord
{
	/* Boot Code */
	uint8_t BootCode[446];

	/* Partition Table */
	MCorePartitionEntry_t Partitions[4];

	/* Boot Signature */
	uint8_t BootSignature[2];

} MCoreMasterBootRecord_t;
#pragma pack(pop)

/* Prototypes */

#endif //!_MCORE_VFS_H_