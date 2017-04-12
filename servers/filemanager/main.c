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
#include <os/driver/file.h>
#include <os/utils.h>
#include "include/vfs.h"
#include <ds/list.h>

/* Includes
 * - C-Library */
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <ctype.h>

/* Globals */
static List_t *GlbResolveQueue = NULL;
static List_t *GlbFileSystems = NULL;
static List_t *GlbOpenHandles = NULL;
static List_t *GlbOpenFiles = NULL;
static List_t *GlbModules = NULL;
static List_t *GlbDisks = NULL;
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

/* VfsGetDisks
 * Retrieves a list of all the currently registered
 * disks, provides access for manipulation */
List_t *VfsGetDisks(void)
{
	return GlbDisks;
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
	// Initialize lists
	GlbResolveQueue = ListCreate(KeyInteger, LIST_NORMAL);
	GlbFileSystems = ListCreate(KeyInteger, LIST_NORMAL);
	GlbOpenHandles = ListCreate(KeyInteger, LIST_NORMAL);
	GlbOpenFiles = ListCreate(KeyInteger, LIST_NORMAL);
	GlbModules = ListCreate(KeyInteger, LIST_NORMAL);
	GlbDisks = ListCreate(KeyInteger, LIST_NORMAL);

	// Initialize variables
	memset(&GlbDiskIds[0], 0, sizeof(int) * __FILEMANAGER_MAXDISKS);
	GlbFileSystemId = 0;
	GlbFileId = 0;
	GlbInitialized = 1;

	// Register us with server manager
	RegisterServer(__FILEMANAGER_TARGET);

	// Done
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
	// Variables
	OsStatus_t Result = OsNoError;

	// Which function is called?
	switch (Message->Function)
	{
		// Handles registration of a new disk 
		// and and parses the disk-system for a MBR
		// or a GPT table 
		case __FILEMANAGER_REGISTERDISK: {
			TRACE("Filemanager.OnEvent RegisterDisk");
			Result = RegisterDisk(Message->Sender,
				(UUId_t)Message->Arguments[0].Data.Value,
				(Flags_t)Message->Arguments[1].Data.Value);
		} break;

		// Unregisters a disk from the system and
		// handles cleanup of all attached filesystems
		case __FILEMANAGER_UNREGISTERDISK: {
			TRACE("Filemanager.OnEvent UnregisterDisk");
			Result = UnregisterDisk(
				(UUId_t)Message->Arguments[0].Data.Value,
				(Flags_t)Message->Arguments[1].Data.Value);
		} break;

		// TODO
		case __FILEMANAGER_QUERYDISKS: {
			TRACE("Filemanager.OnEvent QueryDisk");
		} break;

		/* Resolves all stored filesystems that
		 * has been waiting for boot-partition to be loaded */
		case __FILEMANAGER_RESOLVEQUEUE: {
			TRACE("Filemanager.OnEvent ResolveQueue");
			Result = VfsResolveQueueExecute();
		} break;

		/* Opens or creates the given file path based on
		 * the given <Access> and <Options> flags. */
		case __FILEMANAGER_OPENFILE: {
			OpenFilePackage_t Package;
			TRACE("Filemanager.OnEvent OpenFile");
			Package.Code = OpenFile(Message->Sender,
				(__CONST char*)Message->Arguments[0].Data.Buffer,
				(Flags_t)Message->Arguments[1].Data.Value,
				(Flags_t)Message->Arguments[2].Data.Value,
				&Package.Handle);
			Result = RPCRespond(Message, (__CONST void*)&Package,
				sizeof(OpenFilePackage_t));
		} break;

		/* Closes the given file-handle, but does not necessarily
		 * close the link to the file. */
		case __FILEMANAGER_CLOSEFILE: {
			FileSystemCode_t Code = CloseFile(Message->Sender,
				(UUId_t)Message->Arguments[0].Data.Value);
			TRACE("Filemanager.OnEvent CloseFile");
			Result = RPCRespond(Message,
				(__CONST void*)&Code, sizeof(FileSystemCode_t));
		} break;

		/* Deletes the given file associated with the filehandle
		 * the caller must make sure there is no other references
		 * to the file - otherwise delete fails */
		case __FILEMANAGER_DELETEFILE: {
			FileSystemCode_t Code = DeleteFile(Message->Sender,
				(__CONST char*)Message->Arguments[0].Data.Buffer);
			TRACE("Filemanager.OnEvent DeleteFile");
			Result = RPCRespond(Message,
				(__CONST void*)&Code, sizeof(FileSystemCode_t));
		} break;

		/* Reads the requested number of bytes into the given buffer
		 * from the current position in the file-handle */
		case __FILEMANAGER_READFILE: {
			RWFilePackage_t Package;
			TRACE("Filemanager.OnEvent ReadFile");
			Package.Code = ReadFile(Message->Sender,
				(UUId_t)Message->Arguments[0].Data.Value,
				(BufferObject_t*)Message->Arguments[1].Data.Buffer,
				&Package.Index,
				&Package.ActualSize);
			Result = RPCRespond(Message, (__CONST void*)&Package,
				sizeof(RWFilePackage_t));
		} break;

		/* Writes the requested number of bytes from the given buffer
		 * into the current position in the file-handle */
		case __FILEMANAGER_WRITEFILE: {
			RWFilePackage_t Package;
			TRACE("Filemanager.OnEvent WriteFile");
			Package.Code = WriteFile(Message->Sender,
				(UUId_t)Message->Arguments[0].Data.Value,
				(BufferObject_t*)Message->Arguments[1].Data.Buffer,
				&Package.ActualSize);
			Result = RPCRespond(Message, (__CONST void*)&Package,
				sizeof(RWFilePackage_t));
		} break;

		/* Sets the file-pointer for the given handle to the
		 * values given, the position is absolute and must
		 * be within range of the file size */
		case __FILEMANAGER_SEEKFILE: {
			FileSystemCode_t Code = SeekFile(Message->Sender,
				(UUId_t)Message->Arguments[0].Data.Value,
				(uint32_t)Message->Arguments[1].Data.Value,
				(uint32_t)Message->Arguments[2].Data.Value);
			TRACE("Filemanager.OnEvent SeekFile");
			Result = RPCRespond(Message,
				(__CONST void*)&Code, sizeof(FileSystemCode_t));
		} break;

		/* Flushes the internal file buffers and ensures there are
		 * no pending file operations for the given file handle */
		case __FILEMANAGER_FLUSHFILE: {
			FileSystemCode_t Code = FlushFile(Message->Sender,
				(UUId_t)Message->Arguments[0].Data.Value);
			TRACE("Filemanager.OnEvent FlushFile");
			Result = RPCRespond(Message,
				(__CONST void*)&Code, sizeof(FileSystemCode_t));
		} break;

		/* Moves or copies a given file path to the destination path
		 * this can also be used for renamining if the dest/source paths
		 * match (except for filename/directoryname) */
		case __FILEMANAGER_MOVEFILE: {
			FileSystemCode_t Code = MoveFile(Message->Sender,
				(__CONST char*)Message->Arguments[0].Data.Buffer,
				(__CONST char*)Message->Arguments[1].Data.Buffer,
				(int)Message->Arguments[2].Data.Value);
			TRACE("Filemanager.OnEvent MoveFile");
			Result = RPCRespond(Message,
				(__CONST void*)&Code, sizeof(FileSystemCode_t));
		} break;

		/* Queries the current file position that the given handle
		 * is at, it returns as two separate unsigned values, the upper
		 * value is optional and should only be checked for large files */
		case __FILEMANAGER_GETPOSITION: {
			QueryFileValuePackage_t Package;
			TRACE("Filemanager.OnEvent GetPosition");
			Package.Code = GetFilePosition(Message->Sender,
				(UUId_t)Message->Arguments[0].Data.Value,
				&Package);
			Result = RPCRespond(Message, (__CONST void*)&Package,
				sizeof(QueryFileValuePackage_t));
		} break;

		/* Queries the current file options and file access flags
		 * for the given file handle */
		case __FILEMANAGER_GETOPTIONS: {
			QueryFileOptionsPackage_t Package;
			TRACE("Filemanager.OnEvent GetOptions");
			Package.Code = GetFileOptions(Message->Sender,
				(UUId_t)Message->Arguments[0].Data.Value,
				&Package);
			Result = RPCRespond(Message, (__CONST void*)&Package,
				sizeof(QueryFileOptionsPackage_t));
		} break;

		/* Attempts to modify the current option and or access flags
		 * for the given file handle as specified by <Options> and <Access> */
		case __FILEMANAGER_SETOPTIONS: {
			Result = SetFileOptions(Message->Sender,
				(UUId_t)Message->Arguments[0].Data.Value,
				(Flags_t)Message->Arguments[1].Data.Value,
				(Flags_t)Message->Arguments[2].Data.Value);
			TRACE("Filemanager.OnEvent SetOptions");
			Result = RPCRespond(Message, 
				(__CONST void*)&Result, sizeof(OsStatus_t));
		} break;

		/* Queries the current file size that the given handle
		 * has, it returns as two separate unsigned values, the upper
		 * value is optional and should only be checked for large files */
		case __FILEMANAGER_GETSIZE: {
			QueryFileValuePackage_t Package;
			TRACE("Filemanager.OnEvent GetSize");
			Package.Code = GetFileSize(Message->Sender,
				(UUId_t)Message->Arguments[0].Data.Value,
				&Package);
			Result = RPCRespond(Message, (__CONST void*)&Package,
				sizeof(QueryFileValuePackage_t));
		} break;

		/* Resolves a special environment path for
		 * the given the process and it returns it
		 * as a buffer in the pipe */
		case __FILEMANAGER_PATHRESOLVE: {
			MString_t *Resolved = PathResolveEnvironment(
				(EnvironmentPath_t)Message->Arguments[0].Data.Value);
			TRACE("Filemanager.OnEvent ResolvePath");
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
			TRACE("Filemanager.OnEvent CanonicalizePath");
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
