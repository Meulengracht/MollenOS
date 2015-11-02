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

/* Globals */
list_t *GlbFileSystems = NULL;

/* Initialize Vfs */
void VfsInit(void)
{
	/* Create lists */
	GlbFileSystems = list_create(LIST_SAFE);
}

/* Registers a disk with the VFS 
 * and parses all possible partiions */
void VfsRegisterDisk(MCoreStorageDevice_t *Disk)
{
	/* Allocate structures */
	void *TmpBuffer = (void*)kmalloc(Disk->SectorSize);
	MCoreMasterBootRecord_t *Mbr = NULL;
	uint32_t ValidPT = 0;
	int i;

	/* Read sector 0 */
	if (Disk->Read(Disk->DiskData, 0, TmpBuffer, Disk->SectorSize) < 0)
	{
		/* Error */
		printf("VFS_REGISTERDISK: Error reading from disk\n");
		kfree(TmpBuffer);
		return;
	}

	/* Cast to MBR */
	Mbr = (MCoreMasterBootRecord_t*)TmpBuffer;

	/* Valid partition table? */
	for (i = 0; i < 4; i++)
	{
		/* Is it an active partition? */
		if (Mbr->Partitions[i].Status == PARTITION_ACTIVE)
		{

			/* Yay */
			ValidPT = 1;
		}
	}

	/* Sanity */
	if (!ValidPT)
	{
		/* Only one global partition 
		 * parse FS type from it */
	}

	/* Done, Cleanup */
	kfree(TmpBuffer);
}

/* Unregisters a disk and all registered fs's 
 * on disk */
void VfsUnregisterDisk(MCoreStorageDevice_t *Disk)
{
	/* Iterate fs's on this disk and cleanup */
	_CRT_UNUSED(Disk);
}