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
#include <MollenOS.h>
#include <Vfs/Vfs.h>
#include <stdint.h>
#include <crtdefs.h>
#include <stddef.h>

/* Definitions */
#define MFS_MAGIC			0x3153464D		/* 1FSM */

#define MFS_END_OF_CHAIN	0xFFFFFFFF

/* MFS Entry Flags */
#define MFS_SECURITY		0x2
#define MFS_DIRECTORY		0x4
#define MFS_SYSTEM			0x8
#define MFS_HIDDEN			0x10
#define MFS_LINK			0x20

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

} MfsMasterBucket_t;
#pragma pack(pop)

/* The MFT-Entry
 * 1024 Bytes */
typedef struct _MfsTableEntry
{
	/* Status */
	uint16_t Status;

	/* Type */
	uint16_t Flags;

	/* Index */
	uint32_t StartBucket;

	/* Stats */
	uint64_t CreatedTime;
	uint64_t CreatedDate;

	uint64_t ModifiedTime;
	uint64_t ModifedDate;

	uint64_t ReadTime;
	uint64_t ReadDate;
	
	/* More interesting */
	uint64_t Size;
	uint64_t AllocatedSize;

	/* Name Block */
	uint8_t Name[400];

	/* Security Block */
	uint8_t SecurityBlock[64];

	/* Opt Data Block */
	uint8_t Data[480];

} MfsTableEntry_t;

/* Format */
_CRT_EXTERN void MfsFormatDrive(MCoreStorageDevice_t *Disk);

/* Initialize Fs */
_CRT_EXTERN OsResult_t MfsInit(MCoreFileSystem_t *Fs);

#endif //!_MFS_H_