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

/* Includes */
#include <FileSystems/Mfs.h>

/* Formats a drive with MollenOS FileSystem */
void MfsFormatDrive(MCoreStorageDevice_t *Disk)
{
	/* Get mfs metrics */
	MfsMasterBucket_t MasterBucket;
	uint32_t BucketSize = 0;
	uint32_t ReservedSectors = 0;
	
	uint32_t BucketMapSize = 0;
	uint64_t BucketMapSector = 0;
	uint64_t Buckets = 0;
	uint32_t ReservedBuckets = 0;
	
	uint64_t DriveSizeBytes = Disk->SectorCount * Disk->SectorSize;
	uint64_t GigaByte = (1024 * 1024 * 1024);

	/* Determine bucket size 
	 * if <1gb = 1 Kb (2 sectors) 
	 * If <64gb = 4 Kb (8 sectors)
	 * If >64gb = 8 Kb (16 sectors)
	 * If >512gb = 16 Kb (32 sectors) */
	if (DriveSizeBytes >= (512 * GigaByte))
		BucketSize = 32;
	else if (DriveSizeBytes >= (64 * GigaByte))
		BucketSize = 16;
	else if (DriveSizeBytes <= GigaByte)
		BucketSize = 2;
	else
		BucketSize = 8;

	/* Get size of stage2-loader */

	/* Setup Bucket-list
	 * SectorCount / BucketSize
	 * Each bucket must point to the next, 
	 * untill we reach the end of buckets
	 * Position at end of drive */
	Buckets = Disk->SectorCount / BucketSize;
	BucketMapSize = Buckets * 4; /* One bucket descriptor is 4 bytes */
	BucketMapSector = (Disk->SectorCount - (BucketMapSize / Disk->SectorSize));
	ReservedBuckets = ((BucketMapSize / Disk->SectorSize) + 1) / BucketSize;
	
	/* Setup bucket-master & mirror 
	 * Mirror will be preceeding the bucket-map 
	 * Original will be following the reserved sectors */
	MasterBucket.Magic = MFS_MAGIC;

	/* Setup BootSector */

	/* Write BootSector */
}


/* Load Mfs Driver */
OsResult_t MfsInit(MCoreFileSystem_t *Fs)
{
	MfsBootRecord_t *BootRecord = NULL;

	/* Load bootsector */
}