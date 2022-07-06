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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS MCore - MBR Definitions & Structures
 * - This header describes the base mbr-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _DISK_LAYOUT_MBR_H_
#define _DISK_LAYOUT_MBR_H_

#include <os/osdefs.h>
#include <ddk/filesystem.h>
#include "storage.h"

/* MBR Definitions 
 * Primarily magic constants and flags for headers */
#define MBR_PARTITION_INACTIVE		0x00
#define MBR_PARTITION_ACTIVE		0x80

#define MBR_PARTITION_EMPTY			0x00
#define MBR_PARTITION_EXTENDED		0x05
#define MBR_PARTITION_EXTENDED2		0x0F

/**
 * MBR Parition Entry Structure
 * Contains the structure of a mbr parition that is contained in the MBR sector, up to
 * 4 partitions can be defined at a time
 */
PACKED_TYPESTRUCT(MbrPartitionEntry, {
	uint8_t					Status;
	uint8_t					StartHead;
	uint8_t					StartSector;
	uint8_t					StartCylinder;
	uint8_t					Type;
	uint8_t					EndHead;
	uint8_t					EndSector;
	uint8_t					EndCylinder;
	uint32_t				LbaSector;
	uint32_t				LbaSize;
});

/**
 * MBR Header Structure
 * Describes the MBR header that must always be present on a MBR formatted disk at LBA 0
 */
PACKED_TYPESTRUCT(MasterBootRecord, {
	uint8_t					BootCode[446];
	MbrPartitionEntry_t		Partitions[4];
	uint8_t					BootSignature[2];
});

/**
 * @brief Tries to parse the storage as MBR partitioned.
 *
 * @param storage      The storage device to parse.
 * @param bufferHandle A buffer handle that can be used to read from the disk.
 * @param buffer       The usermapped buffer handle.
 * @return             Status of the parsing. Returns non-OsOK if the storage device is not MBR partitioned.
 */
extern oscode_t
MbrEnumerate(
        _In_ FileSystemStorage_t* storage,
        _In_ UUId_t               bufferHandle,
        _In_ void*                buffer);

#endif //!_DISK_LAYOUT_MBR_H_
