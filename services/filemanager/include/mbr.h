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
 * MollenOS MCore - MBR Definitions & Structures
 * - This header describes the base mbr-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _DISK_LAYOUT_MBR_H_
#define _DISK_LAYOUT_MBR_H_

/* Includes 
 * - System */
#include <os/contracts/filesystem.h>
#include <os/buffer.h>

/* Includes
 * - Library */
#include <os/osdefs.h>

/* MBR Definitions 
 * Primarily magic constants and flags for headers */
#define MBR_PARTITION_INACTIVE		0x00
#define MBR_PARTITION_ACTIVE		0x80

#define MBR_PARTITION_EMPTY			0x00
#define MBR_PARTITION_EXTENDED		0x05
#define MBR_PARTITION_EXTENDED2		0x0F

/* MBR Parition Entry Structure
 * Contains the structure of a mbr parition
 * that is contained in the MBR sector, up to
 * 4 partitions can be defined at a time */
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

/* MBR Header Structure
 * Describes the MBR header that must always
 * be present on a MBR formatted disk at LBA 0 */
PACKED_TYPESTRUCT(MasterBootRecord, {
	uint8_t					BootCode[446];
	MbrPartitionEntry_t		Partitions[4];
	uint8_t					BootSignature[2];
});

/* MbrEnumerate 
 * Enumerates a given disk with MBR data layout 
 * and automatically creates new filesystem objects */
__EXTERN OsStatus_t MbrEnumerate(FileSystemDisk_t *Disk, BufferObject_t *Buffer);

#endif //!_DISK_LAYOUT_MBR_H_
