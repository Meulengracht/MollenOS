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
	/* Get drive metrics */
	uint32_t BucketSize = 0;
	uint32_t ReservedSectors = 0;
	uint32_t BucketMapSize = 0;
	uint32_t BucketBitMapSize = 0;
	uint64_t BucketBitmapSector = 0;
	uint64_t BucketMapSector = 0;
	uint64_t Buckets = 0;
	uint64_t DriveSizeBytes = Disk->SectorCount * Disk->SectorSize;
	uint64_t GigaByte = (1024 * 1024 * 1024);

	/* Determine bucket size 
	 * if <1gb = 4 Kb (8 sectors) 
	 * If <8gb = 8 Kb (16 sectors)
	 * If <32gb = 16 Kb (32 sectors) 
	 * If > 64gb = 32 Kb (64 sectors)
	 * If > 512gb = 64 Kb (128 sectors) */
	if (DriveSizeBytes >= (512 * GigaByte))
		BucketSize = 128;
	else if (DriveSizeBytes >= (64 * GigaByte))
		BucketSize = 64;
	else if (DriveSizeBytes <= GigaByte)
		BucketSize = 8;
	else if (DriveSizeBytes <= (8 * GigaByte))
		BucketSize = 16;
	else
		BucketSize = 32;

	/* Get size of stage2-loader */

	/* Setup Bucket-map 
	 * SectorCount / BucketSize
	 * Fill with 0
	 * Position at end of drive */
	Buckets = Disk->SectorCount / BucketSize;
	BucketMapSize = Buckets * 4;
	BucketMapSector = (Disk->SectorCount - (BucketMapSize / Disk->SectorSize));

	/* Setup bucket-bitmap 
	 * preeceeds the bucket-map 
	 * contains allocation status */
	BucketBitMapSize = Buckets / 8; 
	BucketBitmapSector = BucketMapSector - (BucketBitMapSize / Disk->SectorSize) - 1;

	/* Setup bucket-master & mirror 
	 * Mirror will be preceeding the bucket-map 
	 * Original will be following the reserved sectors */


	/* Write BootSector last with all params */
}


/* Load Mfs Driver */
OsResult_t MfsInit(MCoreFileSystem_t *Fs)
{
	MfsBootRecord_t *BootRecord = NULL;

	/* Load bootsector */
}