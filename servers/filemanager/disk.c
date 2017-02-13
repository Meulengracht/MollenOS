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
#include <os/driver/contracts/filesystem.h>
#include <os/mollenos.h>
#include "include/vfs.h"

/* Includes
 * - C-Library */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Globals
 * to keep track of state */
int GlbInitHasRun = 0;

/* DiskRegisterFileSystem 
 * Registers a new filesystem of the given type, on
 * the given disk with the given position on the disk 
 * and assigns it an identifier */
OsStatus_t DiskRegisterFileSystem(FileSystemDisk_t *Disk,
	uint64_t Sector, uint64_t SectorCount, FileSystemType_t Type) 
{
	/* Ready the buffer */
	DataKey_t Key;
	char IdentBuffer[8];
	memset(IdentBuffer, 0, 8);

	/* Copy the storage ident over 
	 * We use St for hard media, and Rm for removables */
	strcpy(IdentBuffer, "St");
	itoa(GlbFileSystemId, (IdentBuffer + 2), 10);

	/* Construct the identifier */
	Fs->Identifier = MStringCreate(&IdentBuffer, StrASCII);

	/* Add to list */
	Key.Value = (int)Fs->DiskId;
	ListAppend(VfsGetFileSystems(), ListCreateNode(Key, Key, Fs));

	/* Increament */
	GlbFileSystemId++;

	/* Start init? */
	if ((Fs->Flags & VFS_MAIN_DRIVE)
		&& !GlbInitHasRun)
	{
		/* Create a path from the identifier and hardcoded path */
		MString_t *Path = MStringCreate((void*)MStringRaw(Fs->Identifier), StrUTF8);
		MStringAppendCharacters(Path, FILESYSTEM_INIT, StrUTF8);

		/* Spawn the process */
		if (ProcessSpawn(MStringRaw(Path), NULL) != UUID_INVALID) {
			GlbInitHasRun = 1;
		}
	}
}

/* RegisterDisk
 * Registers a disk with the file-manager and it will
 * automatically be parsed (MBR, GPT, etc), and all filesystems
 * on the disk will be brought online */
OsStatus_t RegisterDisk(UUId_t Driver, UUId_t Device, Flags_t Flags)
{
	/* Variables */
	FileSystemDisk_t *Disk = NULL;

	/* Allocate a new instance of a disk descriptor 
	 * to store data and store initial data */
	Disk = (FileSystemDisk_t*)malloc(sizeof(FileSystemDisk_t));
	Disk->Driver = Driver;
	Disk->Device = Device;
	Disk->Flags = Flags;

	/* Query disk for general device information and 
	 * information about geometry */
	if (DiskQuery(Driver, Device, &Disk->Descriptor) != OsNoError) {
		free(Disk);
		return OsError;
	}

	/* Detect the disk layout, and if it fails
	 * try to detect which kind of filesystem is present */
	return DiskDetectLayout(Disk);
}

/* UnregisterDisk
 * Unregisters a disk from the system, and brings any filesystems
 * registered on this disk offline */
OsStatus_t UnregisterDisk(UUId_t Device, Flags_t Flags)
{
	/* Variables for iteration */
	ListNode_t *lNode = NULL;
	DataKey_t Key;

	/* Setup pre-stuff */
	Key.Value = (int)Device;
	lNode = ListGetNodeByKey(VfsGetFileSystems(), Key, 0);

	/* Keep iterating untill no more FS's are present on disk */
	while (lNode != NULL) {
		FileSystem_t *Fs = (FileSystem_t*)lNode->Data;

		/* Close all open files that relate to this filesystem */


		/* Call destroy handler for that FS */
		if (Fs->Module->Destroy(&Fs->Descriptor, Flags) != OsNoError) {

		}

		/* Reduce the number of references to that module */
		Fs->Module->References--;

		/* Sanitize the module references 
		 * If there are no more refs then cleanup module */
		if (Fs->Module->References <= 0) {

		}

		/* Cleanup resources allocated by the filesystem */
		MStringDestroy(Fs->Identifier);
		free(Fs);

		/* Remove it from list */
		ListRemoveByNode(VfsGetFileSystems(), lNode);

		/* Get next */
		lNode = ListGetNodeByKey(VfsGetFileSystems(), Key, 0);
	}

	/* Done! */
	return OsNoError;
}
