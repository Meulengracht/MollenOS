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

#include "include/vfs.h"
#include <os/services/storage.h>
#include <ddk/utils.h>

#include <ds/collection.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef __TRACE
static const char *FunctionNames[] = {
    "RegisterStorage",
    "UnregisterStorage",
    "QueryStorages",
    "QueryStorage",
    "QueryStorageByPath",
    "QueryStorageByHandle",
    "QueryStorageFileSystems",
    "QueryFileSystemByPath",
    "QueryFileSystemByHandle",
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
    "ResolvePath",
    "NormalizePath"
};
#endif

// Static storage for the filemanager
static int          DiskTable[__FILEMANAGER_MAXDISKS] = { 0 };
static Collection_t ResolveQueue    = COLLECTION_INIT(KeyId);
static Collection_t FileSystems     = COLLECTION_INIT(KeyId);
static Collection_t OpenHandles     = COLLECTION_INIT(KeyId);
static Collection_t OpenFiles       = COLLECTION_INIT(KeyId);
static Collection_t Modules         = COLLECTION_INIT(KeyId);
static Collection_t Disks           = COLLECTION_INIT(KeyId);

//static UUId_t FileSystemIdGenerator = 0;
static UUId_t FileIdGenerator = 0;

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
    int ArrayStartIndex = 0;
    int ArrayEndIndex   = 0;
    int i;

    // Start out by determing start index
    ArrayEndIndex = __FILEMANAGER_MAXDISKS / 2;
    if (Disk->Flags & __STORAGE_REMOVABLE) {
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
    if (Disk->Flags & __STORAGE_REMOVABLE) {
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

OsStatus_t
OnLoad(
    _In_ char** ServicePathOut)
{
    *ServicePathOut = SERVICE_FILE_PATH;
    return OsSuccess;
}

OsStatus_t
OnUnload(void)
{
    return OsSuccess;
}

/* OnEvent
 * This is called when the server recieved an external evnet
 * and should handle the given event*/
OsStatus_t
OnEvent(
    _In_ IpcMessage_t* Message)
{
    OsStatus_t Result = OsSuccess;
    
    TRACE("Filemanager.OnEvent(%i) %s", IPC_GET_TYPED(Message, 0), 
        FunctionNames[IPC_GET_TYPED(Message, 0)]);
    
    switch (IPC_GET_TYPED(Message, 0)) {
        // Handles registration of a new disk 
        // and and parses the disk-system for a MBR
        // or a GPT table 
        case __FILEMANAGER_REGISTERSTORAGE: {
            Result = VfsRegisterDisk(Message->Sender, //IPC_GET_TYPED(Message, 1), <-- ProcessId
                (UUId_t)IPC_GET_TYPED(Message, 2),
                (Flags_t)IPC_GET_TYPED(Message, 3));
        } break;

        // Unregisters a disk from the system and
        // handles cleanup of all attached filesystems
        case __FILEMANAGER_UNREGISTERSTORAGE: {
            Result = VfsUnregisterDisk(
                (UUId_t)IPC_GET_TYPED(Message, 2),
                (Flags_t)IPC_GET_TYPED(Message, 3));
        } break;

        // TODO:
        case __FILEMANAGER_QUERYSTORAGES: {
        } break;
        case __FILEMANAGER_QUERYSTORAGE: {
        } break;
        case __FILEMANAGER_QUERYSTORAGEBYPATH: {
        } break;
        case __FILEMANAGER_QUERYSTORAGEBYHANDLE: {
        } break;
        case __FILEMANAGER_QUERY_STORAGE_FILESYSTEMS: {
        } break;
        case __FILEMANAGER_QUERY_FILESYSTEM_BY_PATH: {
        } break;
        case __FILEMANAGER_QUERY_FILESYSTEM_BY_HANDLE: {
        } break;

        // Opens or creates the given file path based on
        // the given <Access> and <Options> flags.
        case __FILEMANAGER_OPEN: {
            OpenFilePackage_t Package = { 0 };
            
            Package.Code = VfsOpenEntry(IPC_GET_TYPED(Message, 1), 
                IPC_GET_STRING(Message, 0), (Flags_t)IPC_GET_TYPED(Message, 2), 
                (Flags_t)IPC_GET_TYPED(Message, 3), &Package.Handle);
            
            Result = IpcReply(Message, &Package, sizeof(OpenFilePackage_t));
        } break;

        // Closes the given file-handle, bust does not necessarily
        // close the link to the file.
        case __FILEMANAGER_CLOSE: {
            FileSystemCode_t Code = VfsCloseEntry(IPC_GET_TYPED(Message, 1), 
                (UUId_t)IPC_GET_TYPED(Message, 2));
            
            Result = IpcReply(Message, &Code, sizeof(FileSystemCode_t));
        } break;

        // Reads the requested number of bytes into the given buffer
        // from the current position in the file-handle
        case __FILEMANAGER_READ: {
            RWFilePackage_t Package = { 0 };
            
            UUId_t Requester    = (UUId_t)IPC_GET_TYPED(Message, 1);
            UUId_t Handle       = (UUId_t)IPC_GET_TYPED(Message, 2);
            UUId_t BufferHandle = (UUId_t)IPC_GET_TYPED(Message, 3);
            size_t Offset       = IPC_GET_TYPED(Message, 4);
            size_t Length       = *((size_t*)IPC_GET_UNTYPED(Message, 0));
            
            Package.Code = VfsReadEntry(Requester, Handle, BufferHandle, 
                Offset, Length, &Package.ActualSize);
            Result = IpcReply(Message, &Package, sizeof(RWFilePackage_t));
        } break;

        // Writes the requested number of bytes from the given buffer
        // into the current position in the file-handle
        case __FILEMANAGER_WRITE: {
            RWFilePackage_t Package = { 0 };
            
            UUId_t Requester    = (UUId_t)IPC_GET_TYPED(Message, 1);
            UUId_t Handle       = (UUId_t)IPC_GET_TYPED(Message, 2);
            UUId_t BufferHandle = (UUId_t)IPC_GET_TYPED(Message, 3);
            size_t Offset       = IPC_GET_TYPED(Message, 4);
            size_t Length       = *((size_t*)IPC_GET_UNTYPED(Message, 0));
            
            Package.Code = VsfWriteEntry(Requester, Handle, BufferHandle, 
                Offset, Length, &Package.ActualSize);
            Result = IpcReply(Message, &Package, sizeof(RWFilePackage_t));
        } break;

        // Sets the file-pointer for the given handle to the
        // values given, the position is absolute and must
        // be within range of the file size
        case __FILEMANAGER_SEEK: {
            FileSystemCode_t Code = VfsSeekInEntry(IPC_GET_TYPED(Message, 1), (UUId_t)IPC_GET_TYPED(Message, 2),
                (uint32_t)IPC_GET_TYPED(Message, 3), (uint32_t)IPC_GET_TYPED(Message, 4));
            
            Result = IpcReply(Message, &Code, sizeof(FileSystemCode_t));
        } break;

        // Flushes the internal file buffers and ensures there are
        // no pending file operations for the given file handle
        case __FILEMANAGER_FLUSH: {
            FileSystemCode_t Code = VfsFlushFile(IPC_GET_TYPED(Message, 1), 
                (UUId_t)IPC_GET_TYPED(Message, 2));
            
            Result = IpcReply(Message, &Code, sizeof(FileSystemCode_t));
        } break;

        // Moves or copies a given file path to the destination path
        // this can also be used for renamining if the dest/source paths
        // match (except for filename/directoryname)
        case __FILEMANAGER_MOVE: {
            FileSystemCode_t Code = VfsMoveEntry(IPC_GET_TYPED(Message, 1), IPC_GET_STRING(Message, 0),
                IPC_GET_STRING(Message, 1), (int)IPC_GET_TYPED(Message, 2));
            
            Result = IpcReply(Message, &Code, sizeof(FileSystemCode_t));
        } break;

        // Queries the current file position that the given handle
        // is at, it returns as two separate unsigned values, the upper
        // value is optional and should only be checked for large files
        case __FILEMANAGER_GETPOSITION: {
            QueryFileValuePackage_t Package = { 0 };
            
            Result = VfsGetEntryPosition(IPC_GET_TYPED(Message, 1), (UUId_t)IPC_GET_TYPED(Message, 2),
                &Package);
            
            Result = IpcReply(Message, &Package, sizeof(QueryFileValuePackage_t));
        } break;

        // Queries the current file options and file access flags
        // for the given file handle
        case __FILEMANAGER_GETOPTIONS: {
            QueryFileOptionsPackage_t Package = { 0 };
            
            Result = VfsGetEntryOptions(IPC_GET_TYPED(Message, 1), (UUId_t)IPC_GET_TYPED(Message, 2),
                &Package);
            
            Result = IpcReply(Message, &Package, sizeof(QueryFileOptionsPackage_t));
        } break;

        // Attempts to modify the current option and or access flags
        // for the given file handle as specified by <Options> and <Access>
        case __FILEMANAGER_SETOPTIONS: {
            Result = VfsSetEntryOptions(IPC_GET_TYPED(Message, 1), (UUId_t)IPC_GET_TYPED(Message, 2),
                (Flags_t)IPC_GET_TYPED(Message, 3), (Flags_t)IPC_GET_TYPED(Message, 4));
            
            Result = IpcReply(Message, &Result, sizeof(OsStatus_t));
        } break;

        // Queries the current file size that the given handle
        // has, it returns as two separate unsigned values, the upper
        // value is optional and should only be checked for large files
        case __FILEMANAGER_GETSIZE: {
            QueryFileValuePackage_t Package;
            
            Result = VfsGetEntrySize(IPC_GET_TYPED(Message, 1), (UUId_t)IPC_GET_TYPED(Message, 2),
                &Package);
            
            Result = IpcReply(Message, &Package, sizeof(QueryFileValuePackage_t));
        } break;

        // Retrieve the full canonical path of the given file-handle.
        case __FILEMANAGER_GETPATH: {
            MString_t *FilePath = NULL;
            
            Result = VfsGetEntryPath(IPC_GET_TYPED(Message, 1), (UUId_t)IPC_GET_TYPED(Message, 2), 
                &FilePath);
            if (Result != OsSuccess) {
                Result = IpcReply(Message, FilePath, sizeof(MString_t*));
            }
            else {
                Result = IpcReply(Message, (void*)MStringRaw(FilePath), MStringSize(FilePath) + 1);
            }
        } break;

        // Queries information about a file-system entry through its full path
        case __FILEMANAGER_GETSTATSBYPATH: {
            QueryFileStatsPackage_t StatsPackage = { 0 };
            
            StatsPackage.Code = VfsQueryEntryPath(IPC_GET_TYPED(Message, 1), IPC_GET_STRING(Message, 0), 
                &StatsPackage.Descriptor);
            
            Result = IpcReply(Message, &StatsPackage, sizeof(QueryFileStatsPackage_t));
        } break;
        
        // Queries information about a file-system entry through its handle
        case __FILEMANAGER_GETSTATSBYHANDLE: {
            QueryFileStatsPackage_t StatsPackage = { 0 };
            
            StatsPackage.Code = VfsQueryEntryHandle(IPC_GET_TYPED(Message, 1), (UUId_t)IPC_GET_TYPED(Message, 2), 
                &StatsPackage.Descriptor);
            
            Result = IpcReply(Message, &StatsPackage, sizeof(QueryFileStatsPackage_t));
        } break;

        // Deletes the given path, the path can both be file or directory.
        case __FILEMANAGER_DELETEPATH: {
            FileSystemCode_t Code = VfsDeletePath(IPC_GET_TYPED(Message, 1),
                IPC_GET_STRING(Message, 0), (Flags_t)IPC_GET_TYPED(Message, 2));
            
            Result = IpcReply(Message, &Code, sizeof(FileSystemCode_t));
        } break;

        // Resolves a special environment path for the given the process and it returns it
        // as a buffer in the pipe
        case __FILEMANAGER_PATHRESOLVE: {
            MString_t *Resolved = VfsPathResolveEnvironment((EnvironmentPath_t)IPC_GET_TYPED(Message, 2));
            char Null           = '\0';
            
            if (Resolved != NULL) {
                Result = IpcReply(Message, (void*)MStringRaw(Resolved), MStringSize(Resolved) + 1);
                MStringDestroy(Resolved);
            }
            else {
                Result = IpcReply(Message, &Null, 1);
            }
        } break;

        // Resolves and combines the environment path together and returns the newly concenated string
        case __FILEMANAGER_PATHCANONICALIZE: {
            MString_t* Resolved = VfsPathCanonicalize(IPC_GET_STRING(Message, 0));
            char       Null     = '\0';
            
            if (Resolved != NULL) {
                Result = IpcReply(Message, (void*)MStringRaw(Resolved), MStringSize(Resolved) + 1);
                MStringDestroy(Resolved);
            }
            else {
                Result = IpcReply(Message, &Null, 1);
            }
        } break;

        default: {
        } break;
    }
    return Result;
}
