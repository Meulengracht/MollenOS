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
 * MollenOS - File Manager Service
 * - Handles all file related services and disk services
 */

/* Includes
 * - System */
#include "../include/vfs.h"
#include "../include/mbr.h"

/* VfsParsePartitionTable 
 * - Partition table parser function for disks 
 *   and parses only MBR, not GPT */
OsStatus_t MbrEnumeratePartitions(UUId_t DiskId, uint64_t SectorBase,
	uint64_t SectorCount, size_t SectorSize)
{
	/* Allocate structures */
	void *TmpBuffer = (void*)kmalloc(SectorSize);
//	MCoreModule_t *Module = NULL;
	MCoreMasterBootRecord_t *Mbr = NULL;
	MCoreDeviceRequest_t Request;
	int PartitionCount = 0;
	int i;

	/* Null out request */
	memset(&Request, 0, sizeof(MCoreDeviceRequest_t));

	/* Read sector */
	Request.Base.Type = RequestRead;
	Request.DeviceId = DiskId;
	Request.SectorLBA = SectorBase;
	Request.Buffer = (uint8_t*)TmpBuffer;
	Request.Length = SectorSize;

	/* Create & Wait */
	//DmCreateRequest(&Request);
	//DmWaitRequest(&Request, 0);

	/* Sanity */
	if (Request.Base.State != EventOk)
	{
		/* Error */
		LogFatal("VFSM", "REGISTERDISK: Error reading from disk - 0x%x\n", Request.ErrType);
		kfree(TmpBuffer);
		return 0;
	}

	/* We don't need sector count here */
	_CRT_UNUSED(SectorCount);

	/* Cast */
	Mbr = (MCoreMasterBootRecord_t*)TmpBuffer;

	/* Valid partition table? */
	for (i = 0; i < 4; i++)
	{
		/* Is it an active partition? */
		if (Mbr->Partitions[i].Status == PARTITION_ACTIVE)
		{
			/* Inc */
			PartitionCount++;

			/* Allocate a filesystem structure */
			MCoreFileSystem_t *Fs = (MCoreFileSystem_t*)kmalloc(sizeof(MCoreFileSystem_t));
			Fs->State = VfsStateInit;
			Fs->DiskId = DiskId;
			Fs->ExtendedData = NULL;
			Fs->SectorStart = SectorBase + Mbr->Partitions[i].LbaSector;
			Fs->SectorCount = Mbr->Partitions[i].LbaSize;
			Fs->Id = GlbFileSystemId;
			Fs->SectorSize = SectorSize;

			/* Check extended partitions first */
			if (Mbr->Partitions[i].Type == 0x05)
			{
				/* Extended - CHS */
			}
			else if (Mbr->Partitions[i].Type == 0x0F
				|| Mbr->Partitions[i].Type == 0xCF)
			{
				/* Extended - LBA */
				PartitionCount += VfsParsePartitionTable(DiskId,
					SectorBase + Mbr->Partitions[i].LbaSector, Mbr->Partitions[i].LbaSize, SectorSize);
			}
			else if (Mbr->Partitions[i].Type == 0xEE)
			{
				/* GPT Formatted */
			}

			/* Check MFS */
			else if (Mbr->Partitions[i].Type == 0x61)
			{
				/* MFS 1 */
				//TODO
				//Module = ModuleFindGeneric(MODULE_FILESYSTEM, FILESYSTEM_MFS);

				/* Load */
//				if (Module != NULL)
//					ModuleLoad(Module, Fs);
			}

			/* Check FAT */
			else if (Mbr->Partitions[i].Type == 0x1
				|| Mbr->Partitions[i].Type == 0x6
				|| Mbr->Partitions[i].Type == 0x8 /* Might be FAT16 */
				|| Mbr->Partitions[i].Type == 0x11 /* Might be FAT16 */
				|| Mbr->Partitions[i].Type == 0x14 /* Might be FAT16 */
				|| Mbr->Partitions[i].Type == 0x24 /* Might be FAT16 */
				|| Mbr->Partitions[i].Type == 0x56 /* Might be FAT16 */
				|| Mbr->Partitions[i].Type == 0x8D
				|| Mbr->Partitions[i].Type == 0xAA
				|| Mbr->Partitions[i].Type == 0xC1
				|| Mbr->Partitions[i].Type == 0xD1
				|| Mbr->Partitions[i].Type == 0xE1
				|| Mbr->Partitions[i].Type == 0xE5
				|| Mbr->Partitions[i].Type == 0xEF
				|| Mbr->Partitions[i].Type == 0xF2)
			{
				/* Fat-12 */
			}
			else if (Mbr->Partitions[i].Type == 0x4
				|| Mbr->Partitions[i].Type == 0x6
				|| Mbr->Partitions[i].Type == 0xE
				|| Mbr->Partitions[i].Type == 0x16
				|| Mbr->Partitions[i].Type == 0x1E
				|| Mbr->Partitions[i].Type == 0x90
				|| Mbr->Partitions[i].Type == 0x92
				|| Mbr->Partitions[i].Type == 0x9A
				|| Mbr->Partitions[i].Type == 0xC4
				|| Mbr->Partitions[i].Type == 0xC6
				|| Mbr->Partitions[i].Type == 0xCE
				|| Mbr->Partitions[i].Type == 0xD4
				|| Mbr->Partitions[i].Type == 0xD6)
			{
				/* Fat16 */
			}
			else if (Mbr->Partitions[i].Type == 0x0B /* CHS */
				|| Mbr->Partitions[i].Type == 0x0C /* LBA */
				|| Mbr->Partitions[i].Type == 0x27
				|| Mbr->Partitions[i].Type == 0xCB
				|| Mbr->Partitions[i].Type == 0xCC)
			{
				/* Fat32 */
			}

			/* Lastly */
			if (Fs->State == VfsStateActive)
				VfsInstallFileSystem(Fs);
			else
				kfree(Fs);
		}
	}

	/* Done */
	kfree(TmpBuffer);
	return PartitionCount;
}


/* MbrEnumerate 
 * Enumerates a given disk with MBR data layout 
 * and automatically creates new filesystem objects */
OsStatus_t MbrEnumerate(FileSystemDisk_t * Disk, BufferObject_t * Buffer)
{
	return OsNoError;
}
