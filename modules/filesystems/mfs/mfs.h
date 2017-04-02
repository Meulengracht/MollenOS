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
 * MollenOS - General File System (MFS) Driver
 *  - Contains the implementation of the MFS driver for mollenos
 *  - Missing features:
 *    - Journaling
 *    - Encryptiong (AES)
 *    - Buckets should be 64 bit? (bucket-map entries would be 96/128 bits)
 *	  - Should tables be B+ Trees instead of linked?
 */

#ifndef _MFS_H_
#define _MFS_H_

/* Includes
 * - System */
#include <os/driver/contracts/filesystem.h>

/* Includes
 * - Library */
#include <os/osdefs.h>
#include <ds/mstring.h>

/* Definitions */
#define MFS_END_OF_CHAIN	0xFFFFFFFF

/* MFS Update Entry Action Codes */
#define MFS_ACTION_UPDATE	0x0
#define MFS_ACTION_CREATE	0x1
#define MFS_ACTION_DELETE	0x2

/* The MFS-MBR */
PACKED_TYPESTRUCT(BootRecord, {
	uint8_t					JumpCode[3];

	// Header
	uint32_t				Magic;
	uint8_t					Version;
	uint8_t					Flags;

	// Disk Information
	uint8_t					MediaType;
	uint16_t				SectorSize;
	uint16_t				SectorsPerTrack;
	uint16_t				HeadsPerCylinder;
	uint64_t				SectorCount;
	
	// MFS Information
	uint16_t				ReservedSectors;
	uint16_t				SectorsPerBucket;
	uint64_t				MasterBucketSector;
	uint64_t				MasterBucketMirror;

	uint8_t					BootCode[460];	//512 - 48
});

/* MFS Magic Value 
 * The signature value that must be present in BootRecord::Magic */
#define MFS_BOOTRECORD_MAGIC			0x3153464D // 1SFM

/* MFS Boot-Record flags
 * The possible values that can be present in BootRecord::Flags */
#define MFS_BOOTRECORD_SYSDRIVE			0x1
#define MFS_BOOTRECORD_DIRTY			0x2
#define MFS_BOOTRECORD_ENCRYPTED		0x4

/* The master-record structure
 * Exists two places on disk to have a backup
 * and it contains extended information related
 * to the mfs-partition */
PACKED_TYPESTRUCT(MasterRecord, {
	uint32_t				Magic;
	uint32_t				Flags;
	uint8_t					PartitionName[64];

	uint32_t				FreeBucket;	// Pointer to first free index
	uint32_t				RootIndex;		// Pointer to root directory
	uint32_t				BadBucketIndex;	// Pointer to list of bad buckets
	uint32_t				JournalIndex; // Pointer to journal file

	uint64_t				BucketMapSector;// Start sector of bucket-map
	uint64_t				BucketMapSize;	// Size of bucket map
});

/* The file-time structure
 * Keeps track of the last time records were modified */
PACKED_TYPESTRUCT(DateTimeRecord, {
	// Date Information
	uint16_t				Year;
	uint8_t					Month;
	uint8_t					Day;

	// Time Information
	uint8_t					Hour;
	uint8_t					Minute;
	uint8_t					Second;
	uint8_t					Reserved;
});

/* The file-record structure
 * Describes a record contained in a directory 
 * which can consist of multiple types, with the common types being
 * both directories and files, and file-links */
PACKED_TYPESTRUCT(FileRecord, {
	uint16_t				Status;		 // 0x0 - Record Status
	uint16_t				Flags;		 // 0x2 - Record Flags
	uint32_t				StartBucket; // 0x4 - First data bucket
	uint32_t				StartLength; // 0x8 - Length of first data bucket

	// DateTime Records (8 bytes each, 64 bit)
	DateTimeRecord_t		CreatedAt;	 // 0xC
	DateTimeRecord_t		ModifiedAt;	 // 0x14
	DateTimeRecord_t		AccessedAt;  // 0x1C
	
	uint64_t				Size;		   // 0x24 - Actual size
	uint64_t				AllocatedSize; // 0x2C - Allocated size on disk

	uint8_t					Name[460];		 // 0x34
	uint8_t					Integrated[512]; // 0x200
});

/* MFS FileRecord-Status Definitions
 * Contains constants and bitfield definitions for FileRecord::Status */
#define MFS_FILERECORD_ENDOFTABLE		0x0
#define MFS_FILERECORD_INUSE			0x1
#define MFS_FILERECORD_DELETED			0x2

/* MFS FileRecord-Flags Definitions
 * Contains constants and bitfield definitions for FileRecord::Flags */
#define MFS_FILERECORD_FILE				0x0		
#define MFS_FILERECORD_DIRECTORY		0x1
#define MFS_FILERECORD_LINK				0x2
#define MFS_FILERECORD_TYPE(Flags)		(Flags & 0x3)

#define MFS_FILERECORD_SECURITY			0x4		// User must possess the right key to unlock
#define MFS_FILERECORD_SYSTEM			0x8		// Readable, nothing else
#define MFS_FILERECORD_HIDDEN			0x10	// Don't show
#define MFS_FILERECORD_INLINE			0x20	// Data is inlined
#define MFS_FILERECORD_CHAINED			0x40	// Means all buckets are adjacent
#define MFS_FILERECORD_LOCKED			0x80	// File is deep-locked

/* FileSystem File Data */
#pragma pack(push, 1)
typedef struct _MfsFile
{
	/* Information */
	MString_t *Name;
	
	uint16_t Flags;

	uint32_t DataBucket;
	uint32_t InitialBucketLength;
	
	uint64_t Size;
	uint64_t AllocatedSize;

	/* Location */
	uint32_t DirBucket;
	uint32_t DirOffset;

} MfsFile_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct _MfsFileInstance
{
	/* Information */
	uint32_t DataBucketPosition;
	uint32_t DataBucketLength;

	/* Variable bucket sizes
	 * is a pain in the butt */
	uint64_t BucketByteBoundary;

	/* Bucket Buffer 
	 * Temporary buffer used for 
	 * for read/write operations */
	uint8_t *BucketBuffer;

} MfsFileInstance_t;
#pragma pack(pop)

/* FileSystem Data */
typedef struct _MfsInstance {
	/* Identifier */
	char *VolumeLabel;

	/* Mb Positions */
	uint64_t MbSector;
	uint64_t MbMirrorSector;

	/* Information */
	size_t BucketSize;
	Flags_t Flags;
	int Version;

	uint64_t BucketCount;
	uint64_t BucketMapSize;
	uint64_t BucketMapSector;
	uint64_t BucketsPerSector;

	/* For easier restructuring */
	uint32_t RootIndex;
	uint32_t FreeIndex;
	uint32_t BadIndex;
	uint32_t MbFlags;

	/* Bucket Buffer */
	void *BucketBuffer;
	size_t BucketBufferOffset;

} MfsInstance_t;

#endif //!_MFS_H_
