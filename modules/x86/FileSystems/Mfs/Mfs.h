/* MollenOS
*
* Copyright 2011 - 2014, Philip Meulengracht
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
* MollenOS MCore - MollenOS File System
*/
#ifndef _MFS_H_
#define _MFS_H_

/* Includes */
#include <Vfs\Vfs.h>
#include <MString.h>
#include <stdint.h>
#include <crtdefs.h>
#include <stddef.h>

/* Definitions */
#define MFS_MAGIC			0x3153464D		/* 1FSM */

#define MFS_OSDRIVE			0x1

#define MFS_END_OF_CHAIN	0xFFFFFFFF

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
#pragma pack(push, 1)
typedef struct _MfsBootRecord
{
	/* Jump Code */
	uint8_t JumpCode[3];

	/* Information */
	uint32_t Magic;
	uint8_t Version;
	uint8_t Flags;

	/* Disk Stats */
	uint8_t MediaType;
	uint16_t SectorSize;
	uint16_t SectorsPerTrack;
	uint16_t HeadsPerCylinder;
	uint64_t SectorCount;
	
	/* Mfs Stats */
	uint16_t ReservedSectors;
	uint16_t SectorsPerBucket;
	uint64_t MasterBucketSector;
	uint64_t MasterBucketMirror;

	/* String */
	uint8_t BootLabel[8];

	//512 - 48
	uint8_t BootCode[460];

} MfsBootRecord_t;
#pragma pack(pop)

/* The Master Bucket */
#pragma pack(push, 1)
typedef struct _MfsMasterBucket
{
	/* Magic */
	uint32_t Magic;

	/* Flags */
	uint32_t Flags;

	/* Pointer to first free index */
	uint32_t FreeBucket;

	/* Pointer to root directory */
	uint32_t RootIndex;

	/* List of bad buckets */
	uint32_t BadBucketIndex;

	/* Journal Index */
	uint32_t JournalIndex;

	/* Bucket Map Sector & Size */
	uint64_t BucketMapSector;
	uint64_t BucketMapSize;

} MfsMasterBucket_t;
#pragma pack(pop)

/* The MFT-Entry
 * 1024 Bytes */
#pragma pack(push, 1)
typedef struct _MfsTableEntry
{
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

} MfsTableEntry_t;
#pragma pack(pop)

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
	uint32_t DataBucketPosition;
	uint32_t DataBucketLength;
	uint32_t DirBucket;
	uint32_t DirOffset;

	/* Status */
	VfsErrorCode_t Status;

} MfsFile_t;
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
	uint32_t BucketSize;
	uint32_t Flags;
	uint32_t Version;

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
	uint32_t BucketBufferOffset;

} MfsData_t;
#pragma pack(pop)

#endif //!_MFS_H_