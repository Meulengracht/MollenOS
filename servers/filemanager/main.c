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
#include <os/driver/file.h>
#include "include/vfs.h"
#include <ds/list.h>

/* Includes
 * - C-Library */
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <ctype.h>

/* Globals */
List_t *GlbResolveQueue = NULL;
List_t *GlbFileSystems = NULL;
List_t *GlbOpenHandles = NULL;
List_t *GlbOpenFiles = NULL;
List_t *GlbModules = NULL;
UUId_t GlbFileSystemId = 0;
UUId_t GlbFileId = 0;
int GlbInitialized = 0;

/* The disk id array, contains id's in the
 * range of __FILEMANAGER_MAXDISKS/2 as half
 * of them are reserved for rm/st */
int GlbDiskIds[__FILEMANAGER_MAXDISKS];

/* VfsGetOpenFiles / VfsGetOpenHandles
 * Retrieves the list of open files /handles and allows
 * access and manipulation of the list */
List_t *VfsGetOpenFiles(void)
{
	return GlbOpenFiles;
}

/* VfsGetOpenFiles / VfsGetOpenHandles
 * Retrieves the list of open files /handles and allows
 * access and manipulation of the list */
List_t *VfsGetOpenHandles(void)
{
	return GlbOpenHandles;
}

/* VfsGetModules
 * Retrieves a list of all the currently loaded
 * modules, provides access for manipulation */
List_t *VfsGetModules(void)
{
	return GlbModules;
}

/* VfsGetFileSystems
 * Retrieves a list of all the current filesystems
 * and provides access for manipulation */
List_t *VfsGetFileSystems(void)
{
	return GlbFileSystems;
}

/* VfsGetResolverQueue
 * Retrieves a list of all the current filesystems
 * that needs to be resolved, and is scheduled */
List_t *VfsGetResolverQueue(void)
{
	return GlbResolveQueue;
}

/* VfsIdentifierFileGet
 * Retrieves a new identifier for a file-handle that
 * is system-wide unique */
UUId_t VfsIdentifierFileGet(void)
{
	return GlbFileId++;
}

/* VfsIdentifierAllocate 
 * Allocates a free identifier index for the
 * given disk, it varies based upon disk type */
UUId_t VfsIdentifierAllocate(FileSystemDisk_t *Disk)
{
	/* Start out by determing start index */
	int ArrayStartIndex = 0, ArrayEndIndex = __FILEMANAGER_MAXDISKS / 2, i;
	if (Disk->Flags & __DISK_REMOVABLE) {
		ArrayStartIndex = __FILEMANAGER_MAXDISKS / 2;
		ArrayEndIndex = __FILEMANAGER_MAXDISKS;
	}

	/* Now iterate the range for the type of disk */
	for (i = ArrayStartIndex; i < ArrayEndIndex; i++) {
		if (GlbDiskIds[i] == 0) {
			GlbDiskIds[i] = 1;
			return (UUId_t)(i - ArrayStartIndex);
		}
	}

	return UUID_INVALID;
}

/* VfsIdentifierFree 
 * Frees a given identifier index */
OsStatus_t VfsIdentifierFree(FileSystemDisk_t *Disk, UUId_t Id)
{
	int ArrayIndex = (int)Id;
	if (Disk->Flags & __DISK_REMOVABLE) {
		ArrayIndex += __FILEMANAGER_MAXDISKS / 2;
	}
	if (ArrayIndex < __FILEMANAGER_MAXDISKS) {
		GlbDiskIds[ArrayIndex] = 0;
		return OsNoError;
	}
	else {
		return OsError;
	}
}

/* OnLoad
 * The entry-point of a server, this is called
 * as soon as the server is loaded in the system */
OsStatus_t OnLoad(void)
{
	/* Setup list */
	GlbResolveQueue = ListCreate(KeyInteger, LIST_NORMAL);
	GlbFileSystems = ListCreate(KeyInteger, LIST_NORMAL);
	GlbOpenHandles = ListCreate(KeyInteger, LIST_NORMAL);
	GlbOpenFiles = ListCreate(KeyInteger, LIST_NORMAL);
	GlbModules = ListCreate(KeyInteger, LIST_NORMAL);

	/* Init variables */
	memset(&GlbDiskIds[0], 0, sizeof(int) * __FILEMANAGER_MAXDISKS);
	GlbFileSystemId = 0;
	GlbFileId = 0;
	GlbInitialized = 1;

	/* Register us with server manager */
	RegisterServer(__FILEMANAGER_TARGET);

	/* No error! */
	return OsNoError;
}

/* OnUnload
 * This is called when the server is being unloaded
 * and should free all resources allocated by the system */
OsStatus_t OnUnload(void)
{
	return OsNoError;
}

/* OnEvent
 * This is called when the server recieved an external evnet
 * and should handle the given event*/
OsStatus_t OnEvent(MRemoteCall_t *Message)
{
	/* Variables */
	OsStatus_t Result = OsNoError;

	/* Which function is called? */
	switch (Message->Function)
	{
		/* Handles registration of a new disk 
		 * and and parses the disk-system for a MBR
		 * or a GPT table */
		case __FILEMANAGER_REGISTERDISK: {
			Result = RegisterDisk((UUId_t)Message->Arguments[0].Data.Value,
				(UUId_t)Message->Arguments[1].Data.Value,
				(Flags_t)Message->Arguments[2].Data.Value);
		} break;

		/* Unregisters a disk from the system and
		 * handles cleanup of all attached filesystems */
		case __FILEMANAGER_UNREGISTERDISK: {
			Result = UnregisterDisk((UUId_t)Message->Arguments[0].Data.Value,
				(Flags_t)Message->Arguments[1].Data.Value);
		} break;

		/* Resolves all stored filesystems that
		 * has been waiting for boot-partition to be loaded */
		case __FILEMANAGER_RESOLVEQUEUE: {
			Result = VfsResolveQueueExecute();
		} break;





		/* Resolves a special environment path for
		 * the given the process and it returns it
		 * as a buffer in the pipe */
		case __FILEMANAGER_PATHRESOLVE: {
			MString_t *Resolved = PathResolveEnvironment(
				(EnvironmentPath_t)Message->Arguments[0].Data.Value);
			if (Resolved != NULL) {
				Result = RPCRespond(Message, MStringRaw(Resolved), 
					strlen(MStringRaw(Resolved)));
			}
			else {
				Result = RPCRespond(Message, NULL, sizeof(void*));
			}
		} break;

		/* Resolves and combines the environment path together
		 * and returns the newly concenated string */
		case __FILEMANAGER_PATHCANONICALIZE: {
			MString_t *Resolved = PathCanonicalize(
				(EnvironmentPath_t)Message->Arguments[0].Data.Value,
				Message->Arguments[1].Data.Buffer);
			if (Resolved != NULL) {
				Result = RPCRespond(Message, MStringRaw(Resolved),
					strlen(MStringRaw(Resolved)));
			}
			else {
				Result = RPCRespond(Message, NULL, sizeof(void*));
			}
		} break;

		default: {
		} break;
	}

	/* Done! */
	return Result;
}
