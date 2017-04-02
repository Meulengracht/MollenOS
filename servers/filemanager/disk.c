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
#define __TRACE

/* Includes 
 * - System */
#include <os/driver/contracts/filesystem.h>
#include <os/driver/file.h>
#include <os/process.h>
#include <os/utils.h>
#include "include/vfs.h"

/* Includes
 * - C-Library */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Globals
 * to keep track of state */
static int GlbInitHasRun = 0;

/* VfsResolveQueueEvent
 * Sends the event to ourself that we are ready to
 * execute the resolver queue */
OsStatus_t VfsResolveQueueEvent(void)
{
	/* Variables */
	MRemoteCall_t Rpc;

	/* Initialize the request */
	RPCInitialize(&Rpc, __FILEMANAGER_INTERFACE_VERSION,
		PIPE_RPCOUT, __FILEMANAGER_RESOLVEQUEUE);
	return RPCEvent(&Rpc, __FILEMANAGER_TARGET);
}

/* VfsResolveQueueExecute
 * Resolves all remaining filesystems that have been
 * waiting in the resolver-queue */
OsStatus_t VfsResolveQueueExecute(void)
{
	/* Iterate nodes */
	foreach(fNode, VfsGetResolverQueue()) {
		FileSystem_t *Fs = (FileSystem_t*)fNode->Data;
		DataKey_t Key;
		
		/* Try to resolve it now */
		Fs->Module = VfsResolveFileSystem(Fs);
		Key.Value = (int)Fs->Descriptor.Disk.Device;

		/* Sanitize the module - must exist */
		if (Fs->Module == NULL) {
			MStringDestroy(Fs->Identifier);
			VfsIdentifierFree(&Fs->Descriptor.Disk, Fs->Id);
			free(Fs);
			continue;
		}

		/* Run initializor function */
		if (Fs->Module->Initialize(&Fs->Descriptor) != OsNoError) {
			MStringDestroy(Fs->Identifier);
			VfsIdentifierFree(&Fs->Descriptor.Disk, Fs->Id);
			free(Fs);
			continue;
		}

		/* Add to list, by using the disk id as identifier */
		ListAppend(VfsGetFileSystems(), ListCreateNode(Key, Key, Fs));
	}

	/* Done! */
	return OsNoError;
}

/* DiskRegisterFileSystem 
 * Registers a new filesystem of the given type, on
 * the given disk with the given position on the disk 
 * and assigns it an identifier */
OsStatus_t DiskRegisterFileSystem(FileSystemDisk_t *Disk,
	uint64_t Sector, uint64_t SectorCount, FileSystemType_t Type) 
{
	// Variables
	FileSystem_t *Fs = NULL;
	char IdentBuffer[8];
	DataKey_t Key;

	// Trace
	TRACE("DiskRegisterFileSystem(Sector %u, Size %u, Type %u)",
		LODWORD(Sector), LODWORD(SectorCount), Type);

	// Allocate a new disk id
	UUId_t Id = VfsIdentifierAllocate(Disk);
	Key.Value = (int)Disk->Device;

	// Prep the buffer so we can build
	// a new fs-identifier
	memset(IdentBuffer, 0, 8);

	// Copy the storage ident over 
	// We use "st" for hard media, and "rm" for removables
	strcpy(IdentBuffer, (Disk->Flags & __DISK_REMOVABLE) ? "rm" : "st");
	itoa((int)Id, (IdentBuffer + 2), 10);

	// Allocate a new copy of the fs-structure
	Fs = (FileSystem_t*)malloc(sizeof(FileSystem_t));
	
	// Initialize the structure
	Fs->Id = Id;
	Fs->Type = Type;
	Fs->Identifier = MStringCreate(&IdentBuffer, StrASCII);
	Fs->Descriptor.Flags = 0;
	Fs->Descriptor.SectorStart = Sector;
	Fs->Descriptor.SectorCount = SectorCount;
	memcpy(&Fs->Descriptor.Disk, Disk, sizeof(FileSystemDisk_t));

	// Resolve the module from the filesystem type 
	// - Now there is a special case, if no FS are
	//   registered and init hasn't run, and it's not MFS
	//   then we add to a resolve-wait queue
	if (!GlbInitHasRun && Fs->Type != FSMFS) {
		ListAppend(VfsGetResolverQueue(), ListCreateNode(Key, Key, Fs));
	}
	else {
		TRACE("Resolving filesystem");
		Fs->Module = VfsResolveFileSystem(Fs);

		// Sanitize the module - must exist
		if (Fs->Module == NULL) {
			ERROR("Filesystem driver did not exist");
			MStringDestroy(Fs->Identifier);
			VfsIdentifierFree(&Fs->Descriptor.Disk, Fs->Id);
			free(Fs);
			return OsError;
		}

		// Run initializor function
		if (Fs->Module->Initialize(&Fs->Descriptor) != OsNoError) {
			ERROR("Filesystem driver failed to initialize");
			MStringDestroy(Fs->Identifier);
			VfsIdentifierFree(&Fs->Descriptor.Disk, Fs->Id);
			free(Fs);
			return OsError;
		}

		// Add to list, by using the disk id as identifier
		ListAppend(VfsGetFileSystems(), ListCreateNode(Key, Key, Fs));

		// Start init?
		if (!GlbInitHasRun)
		{
			// Create a path from the identifier and hardcoded path
			MString_t *Path = MStringCreate((void*)MStringRaw(Fs->Identifier), StrUTF8);
			MStringAppendCharacters(Path, __FILEMANAGER_INITPROCESS, StrUTF8);

			// Spawn the process 
			// If it was succesfully spawned we can start
			// resolving the previous file-systems
			if (ProcessSpawn(MStringRaw(Path), NULL) != UUID_INVALID) {
				Fs->Descriptor.Flags |= __FILESYSTEM_BOOT;
				VfsResolveQueueEvent();
				GlbInitHasRun = 1;
			}

			// Cleanup
			MStringDestroy(Path);
		}
	}

	// Done
	return OsNoError;
}

/* RegisterDisk
 * Registers a disk with the file-manager and it will
 * automatically be parsed (MBR, GPT, etc), and all filesystems
 * on the disk will be brought online */
OsStatus_t RegisterDisk(UUId_t Driver, UUId_t Device, Flags_t Flags)
{
	// Variables
	FileSystemDisk_t *Disk = NULL;
	DataKey_t Key;

	// Trace 
	TRACE("RegisterDisk(Driver %u, Device %u, Flags 0x%x)",
		Driver, Device, Flags);

	// Allocate a new instance of a disk descriptor 
	// to store data and store initial data
	Disk = (FileSystemDisk_t*)malloc(sizeof(FileSystemDisk_t));
	Disk->Driver = Driver;
	Disk->Device = Device;
	Disk->Flags = Flags;
	Key.Value = (int)Device;

	// Query disk for general device information and 
	// information about geometry
	if (DiskQuery(Driver, Device, &Disk->Descriptor) != OsNoError) {
		free(Disk);
		return OsError;
	}

	// Add the registered disk to the list of disks
	ListAppend(VfsGetDisks(), ListCreateNode(Key, Key, Disk));

	// Detect the disk layout, and if it fails
	// try to detect which kind of filesystem is present
	return DiskDetectLayout(Disk);
}

/* UnregisterDisk
 * Unregisters a disk from the system, and brings any filesystems
 * registered on this disk offline */
OsStatus_t UnregisterDisk(UUId_t Device, Flags_t Flags)
{
	/* Variables for iteration */
	FileSystemDisk_t *Disk = NULL;
	ListNode_t *lNode = NULL;
	DataKey_t Key;

	/* Setup pre-stuff */
	Key.Value = (int)Device;
	lNode = ListGetNodeByKey(VfsGetFileSystems(), Key, 0);

	/* Keep iterating untill no more FS's are present on disk */
	while (lNode != NULL) {
		FileSystem_t *Fs = (FileSystem_t*)lNode->Data;

		/* Close all open files that relate to this filesystem */
		// TODO

		/* Call destroy handler for that FS */
		if (Fs->Module->Destroy(&Fs->Descriptor, Flags) != OsNoError) {
			// What do?
		}

		/* Reduce the number of references to that module */
		Fs->Module->References--;

		/* Sanitize the module references 
		 * If there are no more refs then cleanup module */
		if (Fs->Module->References <= 0) {
			// Unload? Or keep loaded?
		}

		/* Cleanup resources allocated by the filesystem */
		VfsIdentifierFree(&Fs->Descriptor.Disk, Fs->Id);
		MStringDestroy(Fs->Identifier);
		free(Fs);

		/* Remove it from list */
		ListRemoveByNode(VfsGetFileSystems(), lNode);

		/* Get next */
		lNode = ListGetNodeByKey(VfsGetFileSystems(), Key, 0);
	}

	/* Remove the disk from the list of disks */
	Disk = ListGetDataByKey(VfsGetDisks(), Key, 0);
	ListRemoveByKey(VfsGetDisks(), Key);
	free(Disk);

	/* Done! */
	return OsNoError;
}
