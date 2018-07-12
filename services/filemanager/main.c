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
//#define __TRACE

#include <ds/collection.h>
#include <os/file.h>
#include <os/utils.h>
#include "include/vfs.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Events function string table */
static const char *FunctionNames[] = {
    "RegisterDisk",
    "UnregisterDisk",
    "QueryDisks",
    "QueryDisk",
    "QueryDiskByPath",
    "QueryDiskByHandle",
    "OpenFile",
    "CloseFile",
    "ReadFile",
    "WriteFile",
    "SeekFile",
    "FlushFile",
    "MoveFile",
    "GetFilePosition",
    "GetFileOptions",
    "SetFileOptions",
    "GetFileSize",
    "GetFilePath",
    "GetFileStatsByPath",
    "GetFileStatsByHandle",
    "DeletePath",
    "OpenDirectory",
    "CloseDirectory",
    "ReadDirectory",
    "SeekDirectory",
    "ResolvePath",
    "NormalizePath"
};

// Static storage for the filemanager
static int 			DiskTable[__FILEMANAGER_MAXDISKS] = { 0 };
static Collection_t ResolveQueue	= COLLECTION_INIT(KeyInteger);
static Collection_t FileSystems 	= COLLECTION_INIT(KeyInteger);
static Collection_t OpenHandles 	= COLLECTION_INIT(KeyInteger);
static Collection_t OpenFiles   	= COLLECTION_INIT(KeyInteger);
static Collection_t Modules     	= COLLECTION_INIT(KeyInteger);
static Collection_t Disks       	= COLLECTION_INIT(KeyInteger);

static UUId_t 		FileSystemIdGenerator 	= 0;
static UUId_t 		FileIdGenerator         = 0;

/* VfsGetOpenFiles / VfsGetOpenHandles
 * Retrieves the list of open files /handles and allows access and manipulation of the list */
Collection_t*
VfsGetOpenFiles(void) {
	return &OpenFiles;
}

/* VfsGetOpenFiles / VfsGetOpenHandles
 * Retrieves the list of open files /handles and allows access and manipulation of the list */
Collection_t*
VfsGetOpenHandles(void) {
	return &OpenHandles;
}

/* VfsGetModules
 * Retrieves a list of all the currently loaded modules, provides access for manipulation */
Collection_t*
VfsGetModules(void) {
	return &Modules;
}

/* VfsGetDisks
 * Retrieves a list of all the currently registered disks, provides access for manipulation */
Collection_t*
VfsGetDisks(void) {
	return &Disks;
}

/* VfsGetFileSystems
 * Retrieves a list of all the current filesystems and provides access for manipulation */
Collection_t*
VfsGetFileSystems(void) {
	return &FileSystems;
}

/* VfsGetResolverQueue
 * Retrieves a list of all the current filesystems that needs to be resolved, and is scheduled */
Collection_t*
VfsGetResolverQueue(void) {
	return &ResolveQueue;
}

/* VfsIdentifierFileGet
 * Retrieves a new identifier for a file-handle that is system-wide unique */
UUId_t
VfsIdentifierFileGet(void) {
	return FileIdGenerator++;
}

/* VfsIdentifierAllocate 
 * Allocates a free identifier index for the given disk, it varies based upon disk type */
UUId_t
VfsIdentifierAllocate(
    _In_ FileSystemDisk_t *Disk)
{
    // Variables
    int ArrayStartIndex = 0;
    int ArrayEndIndex   = 0;
    int i;

	// Start out by determing start index
	ArrayEndIndex = __FILEMANAGER_MAXDISKS / 2;
	if (Disk->Flags & __DISK_REMOVABLE) {
		ArrayStartIndex = __FILEMANAGER_MAXDISKS / 2;
		ArrayEndIndex = __FILEMANAGER_MAXDISKS;
	}

	// Now iterate the range for the type of disk
	for (i = ArrayStartIndex; i < ArrayEndIndex; i++) {
		if (DiskTable[i] == 0) {
			DiskTable[i] = 1;
			return (UUId_t)(i - ArrayStartIndex);
		}
	}
	return UUID_INVALID;
}

/* VfsIdentifierFree 
 * Frees a given disk identifier index */
OsStatus_t
VfsIdentifierFree(
    _In_ FileSystemDisk_t   *Disk,
    _In_ UUId_t              Id)
{
	int ArrayIndex = (int)Id;
	if (Disk->Flags & __DISK_REMOVABLE) {
		ArrayIndex += __FILEMANAGER_MAXDISKS / 2;
	}
	if (ArrayIndex < __FILEMANAGER_MAXDISKS) {
		DiskTable[ArrayIndex] = 0;
		return OsSuccess;
	}
	else {
		return OsError;
	}
}

/* OnLoad
 * The entry-point of a server, this is called
 * as soon as the server is loaded in the system */
OsStatus_t
OnLoad(void)
{
	// Register us with os
	return RegisterService(__FILEMANAGER_TARGET);
}

/* OnUnload
 * This is called when the server is being unloaded
 * and should free all resources allocated by the system */
OsStatus_t
OnUnload(void) {
	return OsSuccess;
}

/* OnEvent
 * This is called when the server recieved an external evnet
 * and should handle the given event*/
OsStatus_t
OnEvent(
    _In_ MRemoteCall_t *Message)
{
	// Variables
	OsStatus_t Result = OsSuccess;
    if (Message->Function != __FILEMANAGER_RESOLVEQUEUE) {
        TRACE("Filemanager.OnEvent(%i) %s", Message->Function, FunctionNames[Message->Function]);
    }
    
	switch (Message->Function) {
		// Resolves all stored filesystems that
		// has been waiting for boot-partition to be loaded
		case __FILEMANAGER_RESOLVEQUEUE: {
			Result = VfsResolveQueueExecute();
		} break;

		// Handles registration of a new disk 
		// and and parses the disk-system for a MBR
		// or a GPT table 
		case __FILEMANAGER_REGISTERDISK: {
			Result = VfsRegisterDisk(Message->From.Process,
				(UUId_t)Message->Arguments[0].Data.Value,
				(Flags_t)Message->Arguments[1].Data.Value);
		} break;

		// Unregisters a disk from the system and
		// handles cleanup of all attached filesystems
		case __FILEMANAGER_UNREGISTERDISK: {
			Result = VfsUnregisterDisk(
				(UUId_t)Message->Arguments[0].Data.Value,
				(Flags_t)Message->Arguments[1].Data.Value);
		} break;

		// @todo
		case __FILEMANAGER_QUERYDISKS: {
		} break;
        case __FILEMANAGER_QUERYDISK: {
        } break;
        case __FILEMANAGER_QUERYDISKBYPATH: {
        } break;
        case __FILEMANAGER_QUERYDISKBYHANDLE: {
        } break;

		// Opens or creates the given file path based on
		// the given <Access> and <Options> flags.
		case __FILEMANAGER_OPENFILE: {
			OpenFilePackage_t Package;
			Package.Code    = VfsOpenFile(Message->From.Process,
				RPCGetStringArgument(Message, 0),
				(Flags_t)Message->Arguments[1].Data.Value,
				(Flags_t)Message->Arguments[2].Data.Value,
				&Package.Handle);
			Result = RPCRespond(&Message->From, (const void*)&Package, sizeof(OpenFilePackage_t));
		} break;

		// Closes the given file-handle, but does not necessarily
		// close the link to the file.
		case __FILEMANAGER_CLOSEFILE: {
			FileSystemCode_t Code = FsOk;
            Code   = VfsCloseFile(Message->From.Process, 
                (UUId_t)Message->Arguments[0].Data.Value);
			Result = RPCRespond(&Message->From, (const void*)&Code, sizeof(FileSystemCode_t));
		} break;

		// Reads the requested number of bytes into the given buffer
		// from the current position in the file-handle
		case __FILEMANAGER_READFILE: {
			RWFilePackage_t Package;
			Package.Code = VfsReadFile(Message->From.Process,
				(UUId_t)Message->Arguments[0].Data.Value,
				(UUId_t)Message->Arguments[1].Data.Value,
				Message->Arguments[2].Data.Value,
				&Package.Index,
				&Package.ActualSize);
			Result = RPCRespond(&Message->From, (const void*)&Package, sizeof(RWFilePackage_t));
		} break;

		// Writes the requested number of bytes from the given buffer
		// into the current position in the file-handle
		case __FILEMANAGER_WRITEFILE: {
			RWFilePackage_t Package;
			Package.Code = VfsWriteFile(Message->From.Process,
				(UUId_t)Message->Arguments[0].Data.Value,
				(UUId_t)Message->Arguments[1].Data.Value,
				Message->Arguments[2].Data.Value,
				&Package.ActualSize);
			Result = RPCRespond(&Message->From, (const void*)&Package, sizeof(RWFilePackage_t));
		} break;

		// Sets the file-pointer for the given handle to the
		// values given, the position is absolute and must
		// be within range of the file size
		case __FILEMANAGER_SEEKFILE: {
			FileSystemCode_t Code = VfsSeekFile(Message->From.Process,
				(UUId_t)Message->Arguments[0].Data.Value,
				(uint32_t)Message->Arguments[1].Data.Value,
				(uint32_t)Message->Arguments[2].Data.Value);
			Result = RPCRespond(&Message->From, (const void*)&Code, sizeof(FileSystemCode_t));
		} break;

		// Flushes the internal file buffers and ensures there are
		// no pending file operations for the given file handle
		case __FILEMANAGER_FLUSHFILE: {
			FileSystemCode_t Code = VfsFlushFile(Message->From.Process,
				(UUId_t)Message->Arguments[0].Data.Value);
			Result = RPCRespond(&Message->From, (const void*)&Code, sizeof(FileSystemCode_t));
		} break;

	    // Moves or copies a given file path to the destination path
	    // this can also be used for renamining if the dest/source paths
	    // match (except for filename/directoryname)
		case __FILEMANAGER_MOVEFILE: {
			FileSystemCode_t Code = VfsMoveFile(Message->From.Process,
				RPCGetStringArgument(Message, 0),
				RPCGetStringArgument(Message, 1),
				(int)Message->Arguments[2].Data.Value);
			Result = RPCRespond(&Message->From, (const void*)&Code, sizeof(FileSystemCode_t));
		} break;

		// Queries the current file position that the given handle
		// is at, it returns as two separate unsigned values, the upper
		// value is optional and should only be checked for large files
		case __FILEMANAGER_GETPOSITION: {
			QueryFileValuePackage_t Package;
			Result = VfsGetFilePosition(Message->From.Process,
				(UUId_t)Message->Arguments[0].Data.Value,
				&Package);
			Result = RPCRespond(&Message->From, (const void*)&Package, sizeof(QueryFileValuePackage_t));
		} break;

		// Queries the current file options and file access flags
		// for the given file handle
		case __FILEMANAGER_GETOPTIONS: {
			QueryFileOptionsPackage_t Package;
			Result = VfsGetFileOptions(Message->From.Process,
				(UUId_t)Message->Arguments[0].Data.Value,
				&Package);
			Result = RPCRespond(&Message->From, (const void*)&Package, sizeof(QueryFileOptionsPackage_t));
		} break;

		// Attempts to modify the current option and or access flags
		// for the given file handle as specified by <Options> and <Access>
		case __FILEMANAGER_SETOPTIONS: {
			Result = VfsSetFileOptions(Message->From.Process,
				(UUId_t)Message->Arguments[0].Data.Value,
				(Flags_t)Message->Arguments[1].Data.Value,
				(Flags_t)Message->Arguments[2].Data.Value);
			Result = RPCRespond(&Message->From, (const void*)&Result, sizeof(OsStatus_t));
		} break;

		// Queries the current file size that the given handle
		// has, it returns as two separate unsigned values, the upper
		// value is optional and should only be checked for large files
		case __FILEMANAGER_GETSIZE: {
			QueryFileValuePackage_t Package;
			Result = VfsGetFileSize(Message->From.Process,
				(UUId_t)Message->Arguments[0].Data.Value,
				&Package);
			Result = RPCRespond(&Message->From, (const void*)&Package, sizeof(QueryFileValuePackage_t));
		} break;

        // Retrieve the full canonical path of the given file-handle.
        case __FILEMANAGER_GETPATH: {
            MString_t *FilePath = NULL;
            if (VfsGetFilePath(Message->From.Process, (UUId_t)Message->Arguments[0].Data.Value, &FilePath) != OsSuccess) {
                Result = RPCRespond(&Message->From, FilePath, sizeof(MString_t*));
            }
            else {
                Result = RPCRespond(&Message->From, MStringRaw(FilePath), MStringSize(FilePath));
            }
        } break;

        // @todo
        case __FILEMANAGER_GETSTATSBYPATH: {
        } break;
        case __FILEMANAGER_GETSTATSBYHANDLE: {
        } break;

		// Deletes the given path, the path can both be file or directory.
		case __FILEMANAGER_DELETEPATH: {
			FileSystemCode_t Code = VfsDeletePath(Message->From.Process,
				RPCGetStringArgument(Message, 0), (Flags_t)Message->Arguments[1].Data.Value);
			Result = RPCRespond(&Message->From, (const void*)&Code, sizeof(FileSystemCode_t));
		} break;

        // @todo
        case __FILEMANAGER_OPENDIRECTORY: {
        } break;
        case __FILEMANAGER_CLOSEDIRECTORY: {
        } break;
        case __FILEMANAGER_READDIRECTORY: {
        } break;
        case __FILEMANAGER_SEEKDIRECTORY: {
        } break;

		// Resolves a special environment path for
		// the given the process and it returns it
		// as a buffer in the pipe
		case __FILEMANAGER_PATHRESOLVE: {
			MString_t *Resolved = VfsPathResolveEnvironment((EnvironmentPath_t)Message->Arguments[0].Data.Value);
			if (Resolved != NULL) {
				Result = RPCRespond(&Message->From, MStringRaw(Resolved), MStringSize(Resolved));
                free(Resolved);
			}
			else {
				Result = RPCRespond(&Message->From, Resolved, sizeof(void*));
			}
		} break;

		// Resolves and combines the environment path together
		// and returns the newly concenated string
		case __FILEMANAGER_PATHCANONICALIZE: {
			MString_t *Resolved = VfsPathCanonicalize(RPCGetStringArgument(Message, 0));
			if (Resolved != NULL) {
				Result = RPCRespond(&Message->From, MStringRaw(Resolved), MStringSize(Resolved));
                free(Resolved);
			}
			else {
				Result = RPCRespond(&Message->From, Resolved, sizeof(void*));
			}
		} break;

		default: {
		} break;
	}
	return Result;
}
