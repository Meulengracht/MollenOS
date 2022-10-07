/**
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * GPT Definitions & Structures
 * - This header describes the base gpt-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _DISK_LAYOUT_GPT_H_
#define _DISK_LAYOUT_GPT_H_

#include <os/osdefs.h>
#include "storage.h"

#define GPT_SIGNATURE "EFI PART"
#define GPT_REVISION  0x00010000

#define GPT_ENTRY_RESERVED	0x0000000000000001 // Platform required (required by the computer to function properly)
#define GPT_ENTRY_EFIIGNORE	0x0000000000000002
#define GPT_ENTRY_BOOTABLE	0x0000000000000004

/**
 * GPT Header Structure
 * Describes the GPT header that must always be present on a GPT formatted disk at LBA 1
 */
typedef struct GptHeader {
	uint8_t	 Signature[8];
	uint32_t Revision;
	uint32_t HeaderSize;
	uint32_t HeaderCRC32;		    // CRC32 of header (offset +0 up to header size), with this field zeroed during calculation
	uint32_t Reserved;
	uint64_t CurrentHeaderLBA;	    // Location of this header (LBA 1)
	uint64_t BackupHeaderLBA;
	uint64_t FirstLBA;
	uint64_t LastLBA;
	uint8_t	 DiskGUID[16];
	uint64_t PartitionTableLBA;	    // Always 2 for primary copy
	uint32_t PartitionCount;
	uint32_t PartitionEntrySize;	// Usually 80h or 128
	uint32_t PartitionTableCRC32;
	uint8_t	 SectorSpace[420];	    // 512-sizeof(Header)
} GptHeader_t;

/**
 * GPT Entry Structure
 * Describes the GPT entry that the partition table consists of. The location is always at
 * LBA2 on disk and is up to 128 partitions
 */
typedef struct GptPartitionEntry {
	uint8_t	 PartitionTypeGUID[16];
	uint8_t	 PartitionGUID[16];
	uint64_t StartLBA;
	uint64_t EndLBA;			// Including this sector, eg: 0-755
	uint64_t Attributes;
	uint8_t  Name[72];	    // Partition name (36 UTF-16LE code units)
} GptPartitionEntry_t;

/**
 * @brief Tries to parse the storage as GPT partitioned.
 *
 * @param storage      The storage device to parse.
 * @param bufferHandle A buffer handle that can be used to read from the disk.
 * @param buffer       The usermapped buffer handle.
 * @return             Status of the parsing. Returns non-OsOK if the storage device is not GPT partitioned.
 */
extern oserr_t
GptEnumerate(
        _In_ struct VFSStorage* storage,
        _In_ uuid_t             bufferHandle,
        _In_ void*              buffer);

#endif //!_DISK_LAYOUT_GPT_H_
