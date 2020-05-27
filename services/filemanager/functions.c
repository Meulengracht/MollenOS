/**
 * MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 * File Manager Service
 * - Handles all file related services and disk services
 */
//#define __TRACE

#include <ctype.h>
#include <ddk/utils.h>
#include "include/vfs.h"
#include <os/mollenos.h>
#include <os/dmabuf.h>
#include <os/types/file.h>
#include <os/process.h>
#include <stdlib.h>
#include <string.h>

#include "svc_file_protocol_server.h"

extern MString_t* VfsPathCanonicalize(const char* Path);

static OsStatus_t Flush(UUId_t processId, UUId_t handle);

int
VfsEntryIsFile(
    _In_ FileSystemEntry_t* Entry)
{
    return (Entry->Descriptor.Flags & FILE_FLAG_DIRECTORY) == 0 ? 1 : 0;
}

FileSystem_t*
VfsGetFileSystemFromPath(
    _In_  MString_t*  Path,
    _Out_ MString_t** SubPath)
{
    CollectionItem_t* Node;
    MString_t* Identifier;
    int Index;

    // To open a new file we need to find the correct
    // filesystem identifier and seperate it from it's absolute path
    Index = MStringFind(Path, ':', 0);
    if (Index == MSTRING_NOT_FOUND) {
        return NULL;
    }

    Identifier  = MStringSubString(Path, 0, Index);
    *SubPath    = MStringSubString(Path, Index + 2, -1);

    // Iterate all the filesystems and find the one
    // that matches
    _foreach(Node, VfsGetFileSystems()) {
        FileSystem_t *Filesystem = (FileSystem_t*)Node->Data;
        if (MStringCompare(Identifier, Filesystem->Identifier, 1)) {
            MStringDestroy(Identifier);
            return Filesystem;
        }
    }
    MStringDestroy(Identifier);
    MStringDestroy(*SubPath);
    return NULL;
}

/* VfsIsHandleValid
 * Checks for both owner permission and verification of the handle. */
OsStatus_t
VfsIsHandleValid(
    _In_  UUId_t                    processId,
    _In_  UUId_t                    handle,
    _In_  unsigned int                   RequiredAccess,
    _Out_ FileSystemEntryHandle_t** entryHandle)
{
    CollectionItem_t *Node;
    DataKey_t key = { .Value.Id = handle };
    Node       = CollectionGetNodeByKey(VfsGetOpenHandles(), key, 0);
    if (Node == NULL) {
        ERROR("Invalid handle given for file");
        return OsInvalidParameters;
    }

    *entryHandle = (FileSystemEntryHandle_t*)Node->Data;
    if ((*entryHandle)->Owner != processId) {
        ERROR("Owner of the handle did not match the requester. Access Denied.");
        return OsInvalidPermissions;
    }

    if ((*entryHandle)->Entry->IsLocked != UUID_INVALID && (*entryHandle)->Entry->IsLocked != processId) {
        ERROR("Entry is locked and lock is not held by requester. Access Denied.");
        return OsInvalidPermissions;
    }

    if (RequiredAccess != 0 && ((*entryHandle)->Access & RequiredAccess) != RequiredAccess) {
        ERROR("handle was not opened with the required access parameter. Access Denied.");
        return OsInvalidPermissions;
    }
    return OsSuccess;
}

/* VfsOpenHandleInternal
 * Internal helper for instantiating the entry handle
 * this does not take care of anything else than opening the handle */
OsStatus_t 
VfsOpenHandleInternal(
    _In_  FileSystemEntry_t*        Entry,
    _Out_ FileSystemEntryHandle_t** handle)
{
    FileSystem_t *Filesystem = (FileSystem_t*)Entry->System;
    OsStatus_t status;

    TRACE("VfsOpenHandleInternal()");

    status = Filesystem->Module->OpenHandle(&Filesystem->Descriptor, Entry, handle);
    if (status != OsSuccess) {
        ERROR("Failed to initiate a new entry-handle, code %i", status);
        return status;
    }

    (*handle)->LastOperation       = __FILE_OPERATION_NONE;
    (*handle)->OutBuffer           = NULL;
    (*handle)->OutBufferPosition   = 0;
    (*handle)->Position            = 0;
    (*handle)->Entry               = Entry;

    // handle file specific options
    if (VfsEntryIsFile(Entry)) {
        // Initialise buffering as long as the file
        // handle is not opened as volatile
        if (!((*handle)->Options & __FILE_VOLATILE)) {
            (*handle)->OutBuffer = malloc(Filesystem->Descriptor.Disk.Descriptor.SectorSize);
            memset((*handle)->OutBuffer, 0, Filesystem->Descriptor.Disk.Descriptor.SectorSize);
        }

        // Now comes the step where we handle options 
        // - but only options that are handle-specific
        if ((*handle)->Options & __FILE_APPEND) {
            status = Filesystem->Module->SeekInEntry(&Filesystem->Descriptor, (*handle), Entry->Descriptor.Size.QuadPart);
        }
    }

    // Entry locked for access?
    if ((*handle)->Access & __FILE_WRITE_ACCESS && !((*handle)->Access & __FILE_WRITE_SHARE)) {
        Entry->IsLocked = (*handle)->Owner;
    }
    return status;
}

/* VfsVerifyAccessToPath
 * Verifies the requested user has access to the path. */
OsStatus_t
VfsVerifyAccessToPath(
    _In_  MString_t*                Path,
    _In_  unsigned int                   Options,
    _In_  unsigned int                   Access,
    _Out_ FileSystemEntry_t**       ExistingEntry)
{
    CollectionItem_t* Node;
    int PathHash = (int)MStringHash(Path);

    _foreach(Node, VfsGetOpenFiles()) {
        FileSystemEntry_t *Entry = (FileSystemEntry_t*)Node->Data;
        // If our requested mode is exclusive, then we must verify
        // none in our sub-path is opened in exclusive
        if (Access & __FILE_WRITE_ACCESS && !(Access & __FILE_WRITE_SHARE) &&
            Node->Key.Value.Id != PathHash) {
            // Check if <Entry> contains the entirety of <Path>, if it does then deny
            // the request as we try to open a higher-level entry in exclusive mode
            if (MStringCompare(Entry->Path, Path, 0) != MSTRING_NO_MATCH) {
                ERROR("Entry is blocked from exclusive access, access denied.");
                return OsInvalidPermissions;
            }
        }

        // Have we found the existing already opened file?
        if (Node->Key.Value.Id == PathHash) {
            if (Entry->IsLocked != UUID_INVALID) {
                ERROR("File is opened in exclusive mode already, access denied.");
                return OsInvalidPermissions;
            }
            else {
                // It's important here that we check if the flag
                // __FILE_FAILONEXIST has been set, then we return
                // the appropriate code instead of opening a new handle
                if (Options & __FILE_FAILONEXIST) {
                    ERROR("File already exists - open mode specifies this to be failure.");
                    return OsExists;
                }
                *ExistingEntry = Entry;
                break;
            }
        }
    }
    return OsSuccess;
}

/* VfsOpenInternal
 * Reusable helper for the VfsOpen to open internal
 * handles and performs the interaction with fs */
OsStatus_t 
VfsOpenInternal(
    _In_  MString_t*                Path,
    _In_  unsigned int                   Options,
    _In_  unsigned int                   Access,
    _Out_ FileSystemEntryHandle_t** handle)
{
    FileSystemEntry_t* Entry   = NULL;
    MString_t*         SubPath = NULL;
    OsStatus_t         status;
    DataKey_t          key;

    TRACE("VfsOpenInternal(Path %s)", MStringRaw(Path));

    status = VfsVerifyAccessToPath(Path, Options, Access, &Entry);
    if (status == OsSuccess) {
        // Ok if it didn't exist in cache it's a new lookup
        if (Entry == NULL) {
            FileSystem_t *Filesystem    = VfsGetFileSystemFromPath(Path, &SubPath);
            int Created                 = 0;
            if (Filesystem == NULL) {
                return OsDoesNotExist;
            }

            // Let the module do the rest
            status = Filesystem->Module->OpenEntry(&Filesystem->Descriptor, SubPath, &Entry);
            if (status == OsDoesNotExist && (Options & (__FILE_CREATE | __FILE_CREATE_RECURSIVE))) {
                TRACE("File was not found, but options are to create 0x%x", Options);
                status  = Filesystem->Module->CreatePath(&Filesystem->Descriptor, SubPath, Options, &Entry);
                Created = 1;
            }

            // Sanitize the open otherwise we must cleanup
            if (status == OsSuccess) {
                // It's important here that we check if the flag
                // __FILE_FAILONEXIST has been set, then we return
                // the appropriate code instead of opening a new handle
                // Also this is ok if file was just created
                if ((Options & __FILE_FAILONEXIST) && Created == 0) {
                    ERROR("Entry already exists in path. FailOnExists has been specified.");
                    status = Filesystem->Module->CloseEntry(&Filesystem->Descriptor, Entry);
                    Entry = NULL;
                }
                else {
                    Entry->System       = (uintptr_t*)Filesystem;
                    Entry->Path         = MStringCreate((void*)MStringRaw(Path), StrUTF8);
                    Entry->Hash         = MStringHash(Path);
                    Entry->IsLocked     = UUID_INVALID;
                    Entry->References   = 0;

                    // Take care of truncation flag if file was not newly created. The entry type
                    // must equal to file otherwise we will ignore the flag
                    if ((Options & __FILE_TRUNCATE) && Created == 0 && VfsEntryIsFile(Entry)) {
                        status = Filesystem->Module->ChangeFileSize(&Filesystem->Descriptor, Entry, 0);
                    }
                    key.Value.Id = Entry->Hash;
                    CollectionAppend(VfsGetOpenFiles(), CollectionCreateNode(key, Entry));
                }
            }
            else {
                TRACE("File opening/creation failed with code: %i", status);
                Entry = NULL;
            }
            MStringDestroy(SubPath);
        }

        // Now we can open the handle
        // Open handle Internal takes care of these flags APPEND/VOLATILE/BINARY
        if (Entry != NULL) {
            status = VfsOpenHandleInternal(Entry, handle);
            if (status == OsSuccess) {
                Entry->References++;
            }
        }
    }
    return status;
}

/* VfsGuessBasePath
 * Tries to guess the base path of the relative file path in case
 * the working directory cannot be resolved. */
OsStatus_t
VfsGuessBasePath(
    _In_ const char* Path,
    _In_ char*       Result)
{
    char *dot = strrchr(Path, '.');

    TRACE("VfsGuessBasePath(%s)", Path);
    if (dot) {
        // Binaries are found in common
        if (!strcmp(dot, ".app") || !strcmp(dot, ".dll")) {
            memcpy(Result, "$bin/", 5);
        }
        // Resources are found in system folder
        else {
            memcpy(Result, "$sys/", 5);
        }
    }
    // Assume we are looking for folders in system folder
    else {
        memcpy(Result, "$sys/", 5);
    }
    TRACE("=> %s", Result);
    return OsSuccess;
}

MString_t*
VfsResolvePath(
    _In_ UUId_t      processId,
    _In_ const char* path)
{
    MString_t* resolvedPath = NULL;

    TRACE("VfsResolvePath(%s)", path);
    if (strchr(path, ':') == NULL && strchr(path, '$') == NULL) {
        char *basePath  = (char*)malloc(_MAXPATH);
        if (!basePath) {
            return NULL;
        }
        memset(basePath, 0, _MAXPATH);
        if (ProcessGetWorkingDirectory(processId, &basePath[0], _MAXPATH) == OsError) {
            if (VfsGuessBasePath(path, &basePath[0]) == OsError) {
                ERROR("Failed to guess the base path for path %s", path);
                return NULL;
            }
        }
        else {
            strcat(basePath, "/");
        }
        strcat(basePath, path);
        resolvedPath = VfsPathCanonicalize(basePath);
    }
    else {
        resolvedPath = VfsPathCanonicalize(path);
    }
    return resolvedPath;
}

static OsStatus_t
OpenFile(
    _In_  UUId_t      processId,
    _In_  const char* path, 
    _In_  unsigned int     options, 
    _In_  unsigned int     access,
    _Out_ UUId_t*     handleOut)
{
    FileSystemEntryHandle_t* entry;
    OsStatus_t               status = OsDoesNotExist;
    MString_t*               resolvedPath;
    DataKey_t                key;

    TRACE("OpenFile(Path %s, Options 0x%x, Access 0x%x)", path, options, access);
    if (path == NULL) {
        return OsInvalidParameters;
    }

    // If path is not absolute or special, we 
    // must try the working directory of caller
    resolvedPath = VfsResolvePath(processId, path);
    if (resolvedPath != NULL) {
        status = VfsOpenInternal(resolvedPath, options, access, &entry);
    }
    MStringDestroy(resolvedPath);

    // Sanitize code
    if (status != OsSuccess) {
        TRACE("Error opening entry, exited with code: %i", status);
    }
    else {
        entry->Id      = VfsIdentifierFileGet();
        entry->Owner   = processId;
        entry->Access  = access;
        entry->Options = options;
        
        key.Value.Id = entry->Id;
        CollectionAppend(VfsGetOpenHandles(), CollectionCreateNode(key, entry));
        *handleOut = entry->Id;
    }
    return status;
}

void svc_file_open_callback(struct gracht_recv_message* message, struct svc_file_open_args* args)
{
    UUId_t     handle = UUID_INVALID;
    OsStatus_t status = OpenFile(args->process_id, args->path, args->options,
        args->access, &handle);
    svc_file_open_response(message, status, handle);
}

static OsStatus_t
CloseFile(
    _In_ UUId_t processId,
    _In_ UUId_t handle)
{
    FileSystemEntryHandle_t* entryHandle;
    FileSystemEntry_t*       entry;
    OsStatus_t               status;
    CollectionItem_t*        node;
    FileSystem_t*            fileSystem;
    DataKey_t                key = { .Value.Id = handle };

    TRACE("CloseFile(handle %u)", handle);

    status = VfsIsHandleValid(processId, handle, 0, &entryHandle);
    if (status != OsSuccess) {
        return status;
    }
    
    node  = CollectionGetNodeByKey(VfsGetOpenHandles(), key, 0);
    entry = entryHandle->Entry;

    // handle file specific flags
    if (VfsEntryIsFile(entryHandle->Entry)) {
        // If there has been allocated any buffers they should
        // be flushed and cleaned up 
        if (!(entryHandle->Options & __FILE_VOLATILE)) {
            Flush(processId, handle);
            free(entryHandle->OutBuffer);
        }
    }

    // Call the filesystem close-handle to cleanup
    fileSystem = (FileSystem_t*)entryHandle->Entry->System;
    status     = fileSystem->Module->CloseHandle(&fileSystem->Descriptor, entryHandle);
    if (status != OsSuccess) {
        return status;
    }
    CollectionRemoveByNode(VfsGetOpenHandles(), node);
    free(node);

    // Take care of any entry cleanup / reduction
    entry->References--;
    if (entry->IsLocked == processId) {
        entry->IsLocked = UUID_INVALID;
    }

    // Last reference?
    // Cleanup the file in case of no refs
    if (entry->References == 0) {
        key.Value.Id = entry->Hash;
        CollectionRemoveByKey(VfsGetOpenFiles(), key);
        status = fileSystem->Module->CloseEntry(&fileSystem->Descriptor, entry);
    }
    return status;
}

void svc_file_close_callback(struct gracht_recv_message* message, struct svc_file_close_args* args)
{
    OsStatus_t status = CloseFile(args->process_id, args->handle);
    svc_file_close_response(message, status);
}

static OsStatus_t
DeletePath(
    _In_ UUId_t      processId, 
    _In_ const char* path,
    _In_ unsigned int     options)
{
    FileSystemEntryHandle_t* entryHandle;
    OsStatus_t               status;
    FileSystem_t*            fileSystem;
    MString_t*               subPath;
    MString_t *              resolvedPath;
    UUId_t                   handle;
    DataKey_t                key;

    TRACE("VfsDeletePath(Path %s, Options 0x%x)", path, Options);
    if (path == NULL) {
        return OsInvalidParameters;
    }

    // If path is not absolute or special, we should ONLY try either
    // the current working directory.
    resolvedPath = VfsResolvePath(processId, path);
    if (resolvedPath == NULL) {
        return OsDoesNotExist;
    }
    
    fileSystem = VfsGetFileSystemFromPath(resolvedPath, &subPath);
    MStringDestroy(resolvedPath);
    if (fileSystem == NULL) {
        return OsDoesNotExist;
    }

    // First step is to open the path in exclusive mode
    status = OpenFile(processId, path, __FILE_VOLATILE, __FILE_READ_ACCESS | __FILE_WRITE_ACCESS, &handle);
    if (status == OsSuccess) {
        status = VfsIsHandleValid(processId, handle, 0, &entryHandle);
        if (status != OsSuccess) {
            return status;
        }
        
        key.Value.Id = entryHandle->Entry->Hash;
        status       = fileSystem->Module->DeleteEntry(&fileSystem->Descriptor, entryHandle);
        if (status == OsSuccess) {
            // Cleanup handles and open file
            CollectionRemoveByKey(VfsGetOpenFiles(), key);
            key.Value.Id = handle;
            CollectionRemoveByKey(VfsGetOpenHandles(), key);
        }
    }
    return status;
}

void svc_file_delete_callback(struct gracht_recv_message* message, struct svc_file_delete_args* args)
{
    OsStatus_t status = DeletePath(args->process_id, args->path, args->flags);
    svc_file_delete_response(message, status);
}

static OsStatus_t
ReadFile(
    _In_  UUId_t  processId,
    _In_  UUId_t  handle,
    _In_  UUId_t  bufferHandle,
    _In_  size_t  offset,
    _In_  size_t  length,
    _Out_ size_t* bytesRead)
{
    FileSystemEntryHandle_t* entryHandle;
    OsStatus_t               status;
    FileSystem_t*            fileSystem;
    struct dma_attachment    dmaAttachment;

    TRACE("[vfs_read] pid => %u, id => %u, b_id => %u, len => %u", 
        processId, handle, bufferHandle, LODWORD(length));
    
    if (bufferHandle == UUID_INVALID || length == 0) {
        ERROR("[vfs_read] error invalid parameters, length 0 or invalid b_id");
        return OsInvalidParameters;
    }

    status = VfsIsHandleValid(processId, handle, __FILE_READ_ACCESS, &entryHandle);
    if (status != OsSuccess) {
        return status;
    }

    // Sanity -> Flush if we wrote and now read
    if ((entryHandle->LastOperation != __FILE_OPERATION_READ) && VfsEntryIsFile(entryHandle->Entry)) {
        status = Flush(processId, handle);
    }

    status = dma_attach(bufferHandle, &dmaAttachment);
    if (status != OsSuccess) {
        ERROR("[vfs_read] [dma_attach] failed: %u", status);
        return OsInvalidParameters;
    }
    
    status = dma_attachment_map(&dmaAttachment);
    if (status != OsSuccess) {
        ERROR("[vfs_read] [dma_attachment_map] failed: %u", status);
        dma_detach(&dmaAttachment);
        return OsInvalidParameters;
    }

    TRACE("[vfs_read] [module_read]");
    fileSystem   = (FileSystem_t*)entryHandle->Entry->System;
    status = fileSystem->Module->ReadEntry(&fileSystem->Descriptor, entryHandle, bufferHandle, 
        dmaAttachment.buffer, offset, length, bytesRead);
    if (status == OsSuccess) {
        entryHandle->LastOperation  = __FILE_OPERATION_READ;
        entryHandle->Position       += *bytesRead;
    }
    
    // Unregister the dma buffer
    dma_attachment_unmap(&dmaAttachment);
    dma_detach(&dmaAttachment);
    return status;
}

static OsStatus_t
WriteFile(
    _In_  UUId_t  processId,
    _In_  UUId_t  handle,
    _In_  UUId_t  bufferHandle,
    _In_  size_t  offset,
    _In_  size_t  length,
    _Out_ size_t* bytesWritten)
{
    FileSystemEntryHandle_t* entryHandle;
    OsStatus_t               status;
    FileSystem_t*            fileSystem;
    struct dma_attachment    dmaAttachment;

    TRACE("[vfs_write] pid => %u, id => %u, b_id => %u", processId, handle, bufferHandle);

    if (bufferHandle == UUID_INVALID || length == 0) {
        ERROR("[vfs_write] error invalid parameters, length 0 or invalid b_id");
        return OsInvalidParameters;
    }

    status = VfsIsHandleValid(processId, handle, __FILE_WRITE_ACCESS, &entryHandle);
    if (status != OsSuccess) {
        return status;
    }

    // Sanity -> Clear read buffer if we are writing
    if ((entryHandle->LastOperation != __FILE_OPERATION_WRITE) && VfsEntryIsFile(entryHandle->Entry)) {
        status = Flush(processId, handle);
    }

    status = dma_attach(bufferHandle, &dmaAttachment);
    if (status != OsSuccess) {
        ERROR("[vfs_write] [dma_attach] failed: %u", status);
        return OsInvalidParameters;
    }
    
    status = dma_attachment_map(&dmaAttachment);
    if (status != OsSuccess) {
        ERROR("[vfs_write] [dma_attachment_map] failed: %u", status);
        dma_detach(&dmaAttachment);
        return OsInvalidParameters;
    }

    fileSystem   = (FileSystem_t*)entryHandle->Entry->System;
    status = fileSystem->Module->WriteEntry(&fileSystem->Descriptor, entryHandle, bufferHandle,
        dmaAttachment.buffer, offset, length, bytesWritten);
    if (status == OsSuccess) {
        entryHandle->LastOperation  = __FILE_OPERATION_WRITE;
        entryHandle->Position       += *bytesWritten;
        if (entryHandle->Position > entryHandle->Entry->Descriptor.Size.QuadPart) {
            entryHandle->Entry->Descriptor.Size.QuadPart = entryHandle->Position;
        }
    }
    
    // Unregister the dma buffer
    dma_attachment_unmap(&dmaAttachment);
    dma_detach(&dmaAttachment);
    return status;
}

void svc_file_transfer_async_callback(struct gracht_recv_message* message, struct svc_file_transfer_async_args* args)
{
    
}

void svc_file_transfer_callback(struct gracht_recv_message* message, struct svc_file_transfer_args* args)
{
    size_t     bytesTransferred;
    OsStatus_t status;
    
    if (args->direction == 0) {
        status = ReadFile(args->process_id, args->handle, args->buffer_handle,
            args->buffer_offset, args->length, &bytesTransferred);
    }
    else {
        status = WriteFile(args->process_id, args->handle, args->buffer_handle,
            args->buffer_offset, args->length, &bytesTransferred);
    }
    
    svc_file_transfer_response(message, status, bytesTransferred);
}

static OsStatus_t
Seek(
    _In_ UUId_t   processId,
    _In_ UUId_t   handle, 
    _In_ uint32_t seekLo, 
    _In_ uint32_t seekHi)
{
    FileSystemEntryHandle_t* entryHandle = NULL;
    OsStatus_t               status;
    FileSystem_t*            fileSystem;

    // Combine two u32 to form one big u64 
    // This is just the declaration
    union {
        struct {
            uint32_t Lo;
            uint32_t Hi;
        } Parts;
        uint64_t Full;
    } seekOffsetAbs;
    seekOffsetAbs.Parts.Lo = seekLo;
    seekOffsetAbs.Parts.Hi = seekHi;

    TRACE("Seek(handle %u, seekLo 0x%x, seekHi 0x%x)", handle, seekLo, seekHi);

    status = VfsIsHandleValid(processId, handle, 0, &entryHandle);
    if (status != OsSuccess) {
        return status;
    }

    // Flush buffers before seeking
    if (!(entryHandle->Options & __FILE_VOLATILE) && VfsEntryIsFile(entryHandle->Entry)) {
        status = Flush(processId, handle);
        if (status != OsSuccess) {
            TRACE("Failed to flush file before seek");
            return status;
        }
    }

    // Perform the seek on a file-system level
    fileSystem = (FileSystem_t*)entryHandle->Entry->System;
    status     = fileSystem->Module->SeekInEntry(&fileSystem->Descriptor, entryHandle, seekOffsetAbs.Full);
    if (status == OsSuccess) {
        entryHandle->LastOperation      = __FILE_OPERATION_NONE;
        entryHandle->OutBufferPosition  = 0;
    }
    return status;
}

void svc_file_seek_callback(struct gracht_recv_message* message, struct svc_file_seek_args* args)
{
    OsStatus_t status = Seek(args->process_id, args->handle, args->seek_lo, args->seek_hi);
    svc_file_seek_response(message, status);
}

static OsStatus_t
Flush(
    _In_ UUId_t processId, 
    _In_ UUId_t handle)
{
    FileSystemEntryHandle_t* entryHandle = NULL;
    OsStatus_t               status;
    //FileSystem_t *fileSystem;

    status = VfsIsHandleValid(processId, handle, 0, &entryHandle);
    if (status != OsSuccess) {
        return status;
    }

    // If no buffering enabled skip, or if not a file skip
    if ((entryHandle->Options & __FILE_VOLATILE) || !VfsEntryIsFile(entryHandle->Entry)) {
        return OsSuccess;
    }

    // Empty output buffer 
    // - But sanitize the buffers first
    if (entryHandle->OutBuffer != NULL && entryHandle->OutBufferPosition != 0) {
        size_t BytesWritten = 0;
#if 0
        fileSystem = (FileSystem_t*)entryHandle->File->System;
        status     = fileSystem->Module->WriteFile(&fileSystem->Descriptor, entryHandle, NULL, &BytesWritten);
#endif
        if (BytesWritten != entryHandle->OutBufferPosition) {
            return OsDeviceError;
        }
    }
    return status;
}


void svc_file_flush_callback(struct gracht_recv_message* message, struct svc_file_flush_args* args)
{
    OsStatus_t status = Flush(args->process_id, args->handle);
    svc_file_flush_response(message, status);
}

static OsStatus_t
Move(
    _In_ UUId_t      processId,
    _In_ const char* source, 
    _In_ const char* destination,
    _In_ int         copy)
{
    // @todo implement using existing fs functions
    _CRT_UNUSED(processId);
    _CRT_UNUSED(source);
    _CRT_UNUSED(destination);
    _CRT_UNUSED(copy);
    return OsNotSupported;
}

void svc_file_move_callback(struct gracht_recv_message* message, struct svc_file_move_args* args)
{
    OsStatus_t status = Move(args->process_id, args->from, args->to, args->copy);
    svc_file_move_response(message, status);
}

static OsStatus_t
GetPosition(
    _In_  UUId_t           processId,
    _In_  UUId_t           handle,
    _Out_ LargeUInteger_t* Position)
{
    FileSystemEntryHandle_t* entryHandle = NULL;
    OsStatus_t               status;

    status = VfsIsHandleValid(processId, handle, 0, &entryHandle);
    if (status != OsSuccess) {
        return status;
    }

    Position->QuadPart = entryHandle->Position;
    return status;
}

void svc_file_get_position_callback(struct gracht_recv_message* message, struct svc_file_get_position_args* args)
{
    LargeUInteger_t position;
    OsStatus_t      status = GetPosition(args->process_id, args->handle, &position);
    svc_file_get_position_response(message, status, position.u.LowPart, position.u.HighPart);
}

static OsStatus_t
GetOptions(
    _In_  UUId_t        processId,
    _In_  UUId_t        handle,
    _Out_ unsigned int* options,
    _Out_ unsigned int* access)
{
    FileSystemEntryHandle_t* entryHandle = NULL;
    OsStatus_t               status;

    status = VfsIsHandleValid(processId, handle, 0, &entryHandle);
    if (status != OsSuccess) {
        return status;
    }

    *options = entryHandle->Options;
    *access  = entryHandle->Access;
    return status;
}

void svc_file_get_options_callback(struct gracht_recv_message* message, struct svc_file_get_options_args* args)
{
    unsigned int options, access;
    OsStatus_t   status = GetOptions(args->process_id, args->handle, &options, &access);
    svc_file_get_position_response(message, status, options, access);
}

static OsStatus_t
SetOptions(
    _In_ UUId_t  processId,
    _In_ UUId_t  handle,
    _In_ unsigned int options,
    _In_ unsigned int access)
{
    FileSystemEntryHandle_t* entryHandle = NULL;
    OsStatus_t               status;

    status = VfsIsHandleValid(processId, handle, 0, &entryHandle);
    if (status != OsSuccess) {
        return status;
    }

    entryHandle->Options = options;
    entryHandle->Access  = access;
    return status;
}

void svc_file_set_options_callback(struct gracht_recv_message* message, struct svc_file_set_options_args* args)
{
    OsStatus_t status = SetOptions(args->process_id, args->handle, args->options, args->access);
    svc_file_set_options_response(message, status);
}

OsStatus_t
GetSize(
    _In_  UUId_t           processId,
    _In_  UUId_t           handle,
    _Out_ LargeUInteger_t* Size)
{
    FileSystemEntryHandle_t *entryHandle = NULL;
    OsStatus_t status;

    status = VfsIsHandleValid(processId, handle, 0, &entryHandle);
    if (status != OsSuccess) {
        return status;
    }
    
    Size->QuadPart = entryHandle->Entry->Descriptor.Size.QuadPart;
    return status;
}

void svc_file_get_size_callback(struct gracht_recv_message* message, struct svc_file_get_size_args* args)
{
    LargeUInteger_t size;
    OsStatus_t      status = GetSize(args->process_id, args->handle, &size);
    svc_file_get_size_response(message, status, size.u.LowPart, size.u.HighPart);
}

/* VfsGetEntryPath 
 * Queries the full path of a file-entry that the given handle
 * has, it returns it as a UTF8 string with max length of _MAXPATH */
static OsStatus_t
GetAbsolutePathOfHandle(
    _In_  UUId_t      processId,
    _In_  UUId_t      handle,
    _Out_ MString_t** path)
{
    FileSystemEntryHandle_t* entryHandle = NULL;
    OsStatus_t               status;

    status = VfsIsHandleValid(processId, handle, 0, &entryHandle);
    if (status != OsSuccess) {
        return OsError;
    }
    
    *path = entryHandle->Entry->Path;
    return OsSuccess;
}

void svc_file_get_path_callback(struct gracht_recv_message* message, struct svc_file_get_path_args* args)
{
    MString_t* path;
    OsStatus_t status = GetAbsolutePathOfHandle(args->process_id, args->handle, &path);
    if (status == OsSuccess) {
        svc_file_get_path_response(message, status, MStringRaw(path));
    }
    else {
        svc_file_get_path_response(message, status, "");
    }
}

OsStatus_t
StatFromHandle(
    _In_ UUId_t              processId,
    _In_ UUId_t              handle,
    _In_ OsFileDescriptor_t* descriptor)
{
    FileSystemEntryHandle_t* entryHandle = NULL;
    OsStatus_t               status;

    status = VfsIsHandleValid(processId, handle, 0, &entryHandle);
    if (status == OsSuccess) {
        memcpy((void*)descriptor, (const void*)&entryHandle->Entry->Descriptor, sizeof(OsFileDescriptor_t));
    }
    return status;
}

void svc_file_fstat_callback(struct gracht_recv_message* message, struct svc_file_fstat_args* args)
{
    OsFileDescriptor_t descriptor;
    OsStatus_t         status = StatFromHandle(args->process_id, args->handle, &descriptor);
    svc_file_fstat_response(message, status, &descriptor);
}

OsStatus_t
StatFromPath(
    _In_ UUId_t              processId,
    _In_ const char*         path,
    _In_ OsFileDescriptor_t* descriptor)
{
    OsStatus_t status;
    UUId_t     handle;

    status = OpenFile(processId, path, 0, __FILE_READ_ACCESS | __FILE_READ_SHARE, &handle);
    if (status == OsSuccess) {
        status = StatFromHandle(processId, handle, descriptor);
        status = CloseFile(processId, handle);
    }
    return status;
}

void svc_file_fstat_from_path_callback(struct gracht_recv_message* message, struct svc_file_fstat_from_path_args* args)
{
    OsFileDescriptor_t descriptor;
    OsStatus_t         status = StatFromPath(args->process_id, args->path, &descriptor);
    svc_file_fstat_from_path_response(message, status, &descriptor);
}

void svc_file_fsstat_callback(struct gracht_recv_message* message, struct svc_file_fsstat_args* args)
{
    OsFileSystemDescriptor_t descriptor = { 0 };
    svc_file_fsstat_response(message, OsNotSupported, &descriptor);
}

void svc_file_fsstat_from_path_callback(struct gracht_recv_message* message, struct svc_file_fsstat_from_path_args* args)
{
    OsFileSystemDescriptor_t descriptor = { 0 };
    svc_file_fsstat_from_path_response(message, OsNotSupported, &descriptor);
}
