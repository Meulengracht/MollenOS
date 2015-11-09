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
* MollenOS MCore - Virtual FileSystem
*/

/* Includes */
#include <Vfs/Vfs.h>
#include <Heap.h>
#include <List.h>
#include <stdio.h>
#include <string.h>

/* FileSystems */
#include <FileSystems/Mfs.h>

/* Globals */
list_t *GlbFileSystems = NULL;
uint32_t GlbFileSystemId = 0;

/* Initialize Vfs */
void VfsInit(void)
{
	/* Create lists */
	GlbFileSystems = list_create(LIST_SAFE);
	GlbFileSystemId = 0;
}

/* Partition table parser */
int VfsParsePartitionTable(MCoreStorageDevice_t *Disk, uint64_t SectorBase, uint64_t SectorCount)
{
	/* Allocate structures */
	void *TmpBuffer = (void*)kmalloc(Disk->SectorSize);
	MCoreMasterBootRecord_t *Mbr = NULL;
	int PartitionCount = 0;
	int i;

	/* Read sector */
	if (Disk->Read(Disk->DiskData, SectorBase, TmpBuffer, Disk->SectorSize) < 0)
	{
		/* Error */
		printf("VFS_REGISTERDISK: Error reading from disk\n");
		kfree(TmpBuffer);
		return 0;
	}

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
			Fs->Disk = Disk;
			Fs->FsData = NULL;
			Fs->SectorStart = SectorBase + Mbr->Partitions[i].LbaSector;
			Fs->SectorCount = Mbr->Partitions[i].LbaSize;

			/* Check extended partitions first */
			if (Mbr->Partitions[i].Type == 0x05)
			{
				/* Extended - CHS */
			}
			else if (Mbr->Partitions[i].Type == 0x0F
				|| Mbr->Partitions[i].Type == 0xCF)
			{
				/* Extended - LBA */
				PartitionCount += VfsParsePartitionTable(Disk,
					SectorBase + Mbr->Partitions[i].LbaSector, Mbr->Partitions[i].LbaSize);
			}
			else if (Mbr->Partitions[i].Type == 0xEE)
			{
				/* GPT Formatted */
			}

			/* Check MFS */
			else if (Mbr->Partitions[i].Type == 0x61)
			{
				/* MFS 1 */
				MfsInit(Fs);
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
			if (Fs->FsData != NULL)
			{
				/* Ready the buffer */
				char IdentBuffer[8];
				memset(IdentBuffer, 0, 8);

				/* Copy the storage ident over */
				strcpy(IdentBuffer, "St");
				itoa(GlbFileSystemId, (IdentBuffer + 2), 10);

				/* Construct the identifier */
				Fs->Identifier = strdup((const char *)&IdentBuffer);

				/* Setup last */
				Fs->Lock = MutexCreate();

				/* Add to list */
				list_append(GlbFileSystems, list_create_node(GlbFileSystemId, Fs));

				/* Increament */
				GlbFileSystemId++;
			}
			else
				kfree(Fs);
		}
	}

	/* Done */
	kfree(TmpBuffer);
	return PartitionCount;
}

/* Registers a disk with the VFS 
 * and parses all possible partiions */
void VfsRegisterDisk(MCoreStorageDevice_t *Disk)
{
	/* Sanity */
	if (!VfsParsePartitionTable(Disk, 0, Disk->SectorCount))
	{
		/* Only one global partition 
		 * parse FS type from it */

		/* Allocate a filesystem structure */
		MCoreFileSystem_t *Fs = (MCoreFileSystem_t*)kmalloc(sizeof(MCoreFileSystem_t));
		Fs->Disk = Disk;
		Fs->FsData = NULL;
		Fs->SectorStart = 0;
		Fs->SectorCount = Disk->SectorCount;

		/* Now we have to detect the type of filesystem used
		 * normally two types is used for full-partition 
		 * MFS and FAT */
		OsResult_t FsRes = MfsInit(Fs);

		if (FsRes != OsOk)
			; //FatInit()

		/* Lastly */
		if (FsRes == OsOk)
		{
			/* Ready the buffer */
			char IdentBuffer[8];
			memset(IdentBuffer, 0, 8);

			/* Copy the storage ident over */
			strcpy(IdentBuffer, "St");
			itoa(GlbFileSystemId, (IdentBuffer + 2), 10);

			/* Construct the identifier */
			Fs->Identifier = strdup((const char *)&IdentBuffer);

			/* Setup last */
			Fs->Lock = MutexCreate();
			
			/* Add to list */
			list_append(GlbFileSystems, list_create_node(GlbFileSystemId, Fs));

			/* Increament */
			GlbFileSystemId++;
		}
		else
			kfree(Fs);
	}
}

/* Unregisters a disk and all registered fs's 
 * on disk TODO (Cleanup of Nodes) */
void VfsUnregisterDisk(MCoreStorageDevice_t *Disk, uint32_t Forced)
{
	/* Iterate fs's on this disk and cleanup */
	foreach(FsNode, GlbFileSystems)
	{
		/* Cast */
		MCoreFileSystem_t *Fs = (MCoreFileSystem_t*)FsNode->data;

		/* Sanity */
		if (Fs->Disk == Disk)
		{
			/* Ok, this FS is linked to disk, destroy it */
			if (Fs->Destory(FsNode->data, Forced) != OsOk)
				printf("VfsUnregisterDisk:: Failed to destroy filesystem\n");

			/* Free */
			kfree(Fs->Identifier);
			MutexDestruct(Fs->Lock);
			kfree(Fs);
		}
	}
}