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

/* MFS Entry Status Codes */
#define MFS_STATUS_END		0x0
#define MFS_STATUS_OK		0x1
#define MFS_STATUS_DELETED	0x2

/* MFS Entry Flags */
#define MFS_FILE			0x1
#define MFS_SECURITY		0x2		/* User must possess the right key to unlock */
#define MFS_DIRECTORY		0x4
#define MFS_SYSTEM			0x8		/* Readable, nothing else */
#define MFS_HIDDEN			0x10	/* Don't show */
#define MFS_LINK			0x20	/* Link to another file */
#define MFS_INLINE			0x40	/* Data is inlined */
#define MFS_CHAINED			0x80	/* Means all buckets are adjacent */
#define MFS_LOCKED			0x100	/* File is deep-locked */

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
	uint8_t					BootLabel[8];		// Note: remove this

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

	uint32_t				FreeBucket;	// Pointer to first free index
	uint32_t				RootIndex;		// Pointer to root directory
	uint32_t				BadBucketIndex;	// Pointer to list of bad buckets
	uint32_t				JournalIndex; // Pointer to journal file

	uint64_t				BucketMapSector;// Start sector of bucket-map
	uint64_t				BucketMapSize;	// Size of bucket map

	// Note, add this
	//uint8_t PartitionName[64]
});

/* The MFT-Entry
 * 1024 Bytes */
PACKED_TYPESTRUCT(FileRecord, {
	/* Status - 0x0 */
	uint16_t Status;

	/* Type - 0x2 */
	uint16_t Flags;

	/* Index - 0x4 */
	uint32_t StartBucket;
	uint32_t StartLength;

	/* Stats - 0xC */
	uint64_t CreatedTime;
	uint64_t CreatedDate;  /* 0x14 */

	uint64_t ModifiedTime; /* 0x1C */
	uint64_t ModifedDate;  /* 0x24 */

	uint64_t ReadTime;	   /* 0x2C */
	uint64_t ReadDate;	   /* 0x34 */
	
	/* More interesting */
	uint64_t Size;		   /* 0x3C */
	uint64_t AllocatedSize; /* 0x44 */

	/* Name Block 0x4C */
	uint8_t Name[436];

	/* Opt Data Block */
	uint8_t Data[512];

});

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
#pragma pack(push, 1)
typedef struct _MfsData
{
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

} MfsData_t;
#pragma pack(pop)

#endif //!_MFS_H_
