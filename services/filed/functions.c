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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * File Manager Service
 * - Handles all file related services and disk services
 */

//#define __TRACE

#include <assert.h>
#include <ctype.h>
#include <ddk/convert.h>
#include <ddk/utils.h>
#include <os/dmabuf.h>
#include <os/types/file.h>
#include <stdlib.h>
#include <vfs/cache.h>
#include <vfs/filesystem.h>
#include <vfs/handle.h>
#include <vfs/scope.h>

#include "sys_file_service_server.h"

static OsStatus_t FlushInternal(FileSystemHandle_t* handle);

static inline int __IsEntryFile(FileSystemHandle_t* handle)
{
    return (handle->entry->base->Descriptor.Flags & FILE_FLAG_DIRECTORY) == 0 ? 1 : 0;
}

OsStatus_t
VfsOpenFileInternal(
        _In_  UUId_t        processId,
        _In_  FileSystem_t* filesystem,
        _In_  MString_t*    subPath,
        _In_  unsigned int  options,
        _In_  unsigned int  access,
        _Out_ UUId_t*       handleId)
{
    FileSystemCacheEntry_t* entry;
    FileSystemHandle_t*     handle;
    OsStatus_t              osStatus;

    TRACE("VfsOpenFileInternal(subPath=%s, options=0x%x, access=0x%x)",
          MStringRaw(subPath), options, access);

    // First take care of the actual file entry from the filesystem, this is the
    // first of two layers. This layer keeps track of open files and how many
    // references they. This is smart as we can then clean up all handles when
    // a filesystem is closed, or we can deny an exclusive handle if handles
    // already exist.
    osStatus = VfsFileSystemCacheGet(filesystem, subPath, options, &entry);
    if (osStatus != OsSuccess) {
        goto exit;
    }

    // Next is the handle layer, this is a per instance version of a file-entry and keeps
    // track of single-instance data like file positioning or access control.
    osStatus = VfsHandleCreate(processId, entry, options, access, &handle);
    if (osStatus == OsSuccess) {
        *handleId = handle->id;
    }

exit:
    TRACE("VfsOpenFileInternal returns=%u", osStatus);
    return osStatus;
}

void OpenFile(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    FileSystem_t* fileSystem;
    OsStatus_t    osStatus;
    UUId_t        fileHandle;
    struct VFS*     fsScope;
    struct VFSNode* fileNode;
    MString_t*      path;

    TRACE("OpenFile(path=%s, options=0x%x, access=0x%x)",
          request->parameters.open.path,
          request->parameters.open.options,
          request->parameters.open.access);

    fsScope = VFSScopeGet(request->processId);
    assert(fsScope != NULL);

    path = MStringCreate(request->parameters.open.path, StrUTF8);
    if (path == NULL) {
        sys_file_open_response(request->message, OsOutOfMemory, UUID_INVALID);
        goto cleanup;
    }

    VfsFileSystemRegisterRequest(fileSystem, request);

    // START OF REQUEST
    // ********************
    osStatus = VfsOpenFileInternal(request->processId, fileSystem, subPath,
                                   request->parameters.open.options,
                                   request->parameters.open.access,
                                   &fileHandle);
    if (osStatus != OsSuccess) {
        TRACE("OpenFile error opening entry, exited with code: %i", osStatus);
    }
    sys_file_open_response(request->message, osStatus, fileHandle);

    // END OF REQUEST
    // ********************
    VfsFileSystemUnregisterRequest(fileSystem, request);

cleanup:
    free((void*)request->parameters.open.path);
    VfsRequestDestroy(request);
}

static OsStatus_t
CloseHandleInternal(
        _In_ UUId_t              processId,
        _In_ FileSystemHandle_t* handle)
{
    OsStatus_t osStatus;

    // handle file specific flags
    if (__IsEntryFile(handle)) {
        // If there has been allocated any buffers they should
        // be flushed and cleaned up
        if (!(handle->base->Options & __FILE_VOLATILE)) {
            FlushInternal(handle);
            free(handle->base->OutBuffer);
        }
    }

    osStatus = VfsHandleDestroy(processId, handle);
    return osStatus;
}

void CloseFile(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    FileSystemHandle_t* handle;
    FileSystem_t*       fileSystem;
    OsStatus_t          osStatus;

    TRACE("CloseFile(handle=%u)", request->parameters.close.fileHandle);

    osStatus = VfsFileSystemGetByFileHandle(request->parameters.close.fileHandle, &fileSystem);
    if (osStatus != OsSuccess) {
        sys_file_close_response(request->message, osStatus);
        VfsRequestDestroy(request);
        return;
    }
    VfsFileSystemRegisterRequest(fileSystem, request);

    // START OF REQUEST
    // ********************
    osStatus = VfsHandleAccess(request->processId,
                                request->parameters.close.fileHandle,
                                0, &handle);
    if (osStatus == OsSuccess) {
        osStatus = CloseHandleInternal(request->processId, handle);
    }

    sys_file_close_response(request->message, osStatus);

    // END OF REQUEST
    // ********************
    VfsFileSystemUnregisterRequest(fileSystem, request);
    VfsRequestDestroy(request);
}

void DeletePath(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    FileSystemHandle_t* handle;
    OsStatus_t          status;
    FileSystem_t*       fileSystem;
    MString_t*          subPath;
    MString_t*          resolvedPath;
    UUId_t              handleId;

    TRACE("DeletePath(path=%s, options=0x%x)",
          request->parameters.delete_path.path,
          request->parameters.delete_path.options);

    // If path is not absolute or special, we should ONLY try either
    // the current working directory.
    resolvedPath = VfsPathResolve(request->processId, request->parameters.delete_path.path);
    if (!resolvedPath) {
        sys_file_delete_response(request->message, OsDoesNotExist);
        goto cleanup;
    }
    
    fileSystem = VfsFileSystemGetByPath(resolvedPath, &subPath);
    if (!fileSystem) {
        sys_file_delete_response(request->message, OsDoesNotExist);
        goto cleanup;
    }
    VfsFileSystemRegisterRequest(fileSystem, request);

    // START OF REQUEST
    // ********************

    // First step is to open the path in exclusive mode
    status = VfsOpenFileInternal(request->processId, fileSystem, resolvedPath,
                                 __FILE_VOLATILE, __FILE_READ_ACCESS | __FILE_WRITE_ACCESS,
                                 &handleId);
    if (status == OsSuccess) {
        status = VfsHandleAccess(request->processId, handleId, 0, &handle);
        if (status == OsSuccess) {
            status = fileSystem->module->DeleteEntry(&fileSystem->base, handle->entry->base);
        }
        (void)CloseHandleInternal(request->processId, handle);
    }
    MStringDestroy(resolvedPath);

    sys_file_delete_response(request->message, status);

    // END OF REQUEST
    // ********************
    VfsFileSystemUnregisterRequest(fileSystem, request);

cleanup:
    free((void*)request->parameters.delete_path.path);
    VfsRequestDestroy(request);
}

static OsStatus_t
ReadInternal(
        _In_  FileSystem_t*       fileSystem,
        _In_  FileSystemHandle_t* handle,
        _In_  UUId_t              bufferHandle,
        _In_  size_t              offset,
        _In_  size_t              length,
        _Out_ size_t*             bytesRead)
{
    struct dma_attachment dmaAttachment;
    OsStatus_t            osStatus;

    // Sanity -> Flush if we wrote and now read
    if ((handle->last_operation != __FILE_OPERATION_READ) && __IsEntryFile(handle)) {
        (void)FlushInternal(handle);
    }

    osStatus = dma_attach(bufferHandle, &dmaAttachment);
    if (osStatus != OsSuccess) {
        return osStatus;
    }

    osStatus = dma_attachment_map(&dmaAttachment, DMA_ACCESS_WRITE);
    if (osStatus != OsSuccess) {
        ERROR("ReadFile [dma_attachment_map] failed: %u", osStatus);
        dma_detach(&dmaAttachment);
        return osStatus;
    }

    osStatus = fileSystem->module->ReadEntry(&fileSystem->base, handle->entry->base, handle->base,
                                             bufferHandle, dmaAttachment.buffer, offset, length,
                                             bytesRead);
    if (osStatus == OsSuccess) {
        handle->last_operation = __FILE_OPERATION_READ;
        handle->base->Position += *bytesRead;
    }

    dma_attachment_unmap(&dmaAttachment);
    dma_detach(&dmaAttachment);
    return osStatus;
}

void ReadFile(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    FileSystemHandle_t* handle;
    OsStatus_t          osStatus;
    FileSystem_t*       fileSystem;
    size_t              bytesRead = 0;

    TRACE("ReadFile(pid=%u, id=%u, b_id=%u, len=%u)",
          request->processId,
          request->parameters.transfer.fileHandle,
          request->parameters.transfer.bufferHandle,
          LODWORD(request->parameters.transfer.length));

    osStatus = VfsFileSystemGetByFileHandle(request->parameters.transfer.fileHandle, &fileSystem);
    if (osStatus != OsSuccess) {
        sys_file_transfer_response(request->message, osStatus, 0);
        VfsRequestDestroy(request);
        return;
    }
    VfsFileSystemRegisterRequest(fileSystem, request);

    // START OF REQUEST
    // ********************
    osStatus = VfsHandleAccess(request->processId,
                                request->parameters.transfer.fileHandle,
                                __FILE_READ_ACCESS, &handle);
    if (osStatus != OsSuccess) {
        goto respond;
    }

    osStatus = ReadInternal(fileSystem, handle,
                            request->parameters.transfer.bufferHandle,
                            request->parameters.transfer.offset,
                            request->parameters.transfer.length,
                            &bytesRead);

respond:
    sys_file_transfer_response(request->message, osStatus, bytesRead);

    // END OF REQUEST
    // ********************
    VfsFileSystemUnregisterRequest(fileSystem, request);
    VfsRequestDestroy(request);
}

static OsStatus_t
WriteInternal(
        _In_  FileSystem_t*       fileSystem,
        _In_  FileSystemHandle_t* handle,
        _In_  UUId_t              bufferHandle,
        _In_  size_t              offset,
        _In_  size_t              length,
        _Out_ size_t*             bytesWritten)
{
    struct dma_attachment dmaAttachment;
    OsStatus_t            osStatus;

    // Sanity -> Clear read buffer if we are writing
    if ((handle->last_operation != __FILE_OPERATION_WRITE) && __IsEntryFile(handle)) {
        (void)FlushInternal(handle);
    }

    osStatus = dma_attach(bufferHandle, &dmaAttachment);
    if (osStatus != OsSuccess) {
        ERROR("WriteFile [dma_attach] failed: %u", osStatus);
        return osStatus;
    }

    osStatus = dma_attachment_map(&dmaAttachment, DMA_ACCESS_WRITE);
    if (osStatus != OsSuccess) {
        ERROR("WriteFile [dma_attachment_map] failed: %u", osStatus);
        dma_detach(&dmaAttachment);
        return osStatus;
    }

    osStatus = fileSystem->module->WriteEntry(&fileSystem->base, handle->entry->base, handle->base,
                                              bufferHandle, dmaAttachment.buffer, offset, length,
                                              bytesWritten);
    if (osStatus == OsSuccess) {
        handle->last_operation = __FILE_OPERATION_WRITE;
        handle->base->Position += *bytesWritten;
        if (handle->base->Position > handle->entry->base->Descriptor.Size.QuadPart) {
            handle->entry->base->Descriptor.Size.QuadPart = handle->base->Position;
        }
    }

    dma_attachment_unmap(&dmaAttachment);
    dma_detach(&dmaAttachment);
    return osStatus;
}

void WriteFile(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    FileSystemHandle_t*   handle;
    OsStatus_t            osStatus;
    FileSystem_t*         fileSystem;
    size_t                bytesWritten = 0;

    TRACE("WriteFile(pid=%u, id=%u, b_id=%u)",
          request->processId,
          request->parameters.transfer.fileHandle,
          request->parameters.transfer.bufferHandle);

    osStatus = VfsFileSystemGetByFileHandle(request->parameters.transfer.fileHandle, &fileSystem);
    if (osStatus != OsSuccess) {
        sys_file_transfer_response(request->message, osStatus, 0);
        VfsRequestDestroy(request);
        return;
    }
    VfsFileSystemRegisterRequest(fileSystem, request);

    // START OF REQUEST
    // ********************
    osStatus = VfsHandleAccess(request->processId,
                               request->parameters.transfer.fileHandle,
                               __FILE_WRITE_ACCESS,
                               &handle);
    if (osStatus != OsSuccess) {
        goto respond;
    }

    osStatus = WriteInternal(fileSystem, handle,
                             request->parameters.transfer.bufferHandle,
                             request->parameters.transfer.offset,
                             request->parameters.transfer.length,
                             &bytesWritten);

respond:
    sys_file_transfer_response(request->message, osStatus, bytesWritten);

    // END OF REQUEST
    // ********************
    VfsFileSystemUnregisterRequest(fileSystem, request);
    VfsRequestDestroy(request);
}

static OsStatus_t
SeekInternal(
        _In_ FileSystem_t*       fileSystem,
        _In_ FileSystemHandle_t* handle,
        _In_ LargeUInteger_t*    position)
{
    OsStatus_t osStatus;

    // Flush buffers before seeking
    if (!(handle->base->Options & __FILE_VOLATILE) && __IsEntryFile(handle)) {
        osStatus = FlushInternal(handle);
        if (osStatus != OsSuccess) {
            TRACE("SeekInternal Failed to flush file before seek");
            return osStatus;
        }
    }

    osStatus = fileSystem->module->SeekInEntry(&fileSystem->base,
                                               handle->entry->base,
                                               handle->base,
                                               position->QuadPart);
    if (osStatus == OsSuccess) {
        handle->last_operation          = __FILE_OPERATION_NONE;
        handle->base->OutBufferPosition = 0;
    }
    return osStatus;
}

void Seek(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    FileSystemHandle_t* handle;
    OsStatus_t          osStatus;
    FileSystem_t*       fileSystem;
    LargeUInteger_t     position;

    position.u.LowPart  = request->parameters.seek.position_low;
    position.u.HighPart = request->parameters.seek.position_high;

    TRACE("Seek(handle %u, seekLo 0x%x, seekHi 0x%x)",
          request->parameters.seek.fileHandle,
          request->parameters.seek.position_low,
          request->parameters.seek.position_high);

    osStatus = VfsFileSystemGetByFileHandle(request->parameters.seek.fileHandle, &fileSystem);
    if (osStatus != OsSuccess) {
        sys_file_seek_response(request->message, osStatus);
        VfsRequestDestroy(request);
        return;
    }
    VfsFileSystemRegisterRequest(fileSystem, request);

    // START OF REQUEST
    // ********************
    osStatus = VfsHandleAccess(request->processId,
                               request->parameters.seek.fileHandle,
                               0, &handle);
    if (osStatus != OsSuccess) {
        goto respond;
    }

    osStatus = SeekInternal(fileSystem, handle, &position);

respond:
    sys_file_seek_response(request->message, osStatus);

    // END OF REQUEST
    // ********************
    VfsFileSystemUnregisterRequest(fileSystem, request);
    VfsRequestDestroy(request);
}


void ReadFileAbsolute(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    FileSystemHandle_t* handle;
    OsStatus_t          osStatus;
    FileSystem_t*       fileSystem;
    size_t              bytesRead = 0;
    LargeUInteger_t     position;

    position.u.LowPart  = request->parameters.transfer_absolute.position_low;
    position.u.HighPart = request->parameters.transfer_absolute.position_high;

    TRACE("ReadFileAbsolute(pid=%u, id=%u, b_id=%u, len=%u)",
          request->processId,
          request->parameters.transfer_absolute.fileHandle,
          request->parameters.transfer_absolute.bufferHandle,
          LODWORD(request->parameters.transfer_absolute.length));

    osStatus = VfsFileSystemGetByFileHandle(request->parameters.transfer_absolute.fileHandle, &fileSystem);
    if (osStatus != OsSuccess) {
        sys_file_transfer_absolute_response(request->message, osStatus, 0);
        VfsRequestDestroy(request);
        return;
    }
    VfsFileSystemRegisterRequest(fileSystem, request);

    // START OF REQUEST
    // ********************
    osStatus = VfsHandleAccess(request->processId,
                               request->parameters.transfer_absolute.fileHandle,
                               __FILE_READ_ACCESS, &handle);
    if (osStatus != OsSuccess) {
        goto respond;
    }

    osStatus = SeekInternal(fileSystem, handle, &position);
    if (osStatus != OsSuccess) {
        goto respond;
    }

    osStatus = ReadInternal(fileSystem, handle,
                            request->parameters.transfer_absolute.bufferHandle,
                            request->parameters.transfer_absolute.offset,
                            request->parameters.transfer_absolute.length,
                            &bytesRead);

respond:
    sys_file_transfer_absolute_response(request->message, osStatus, bytesRead);

    // END OF REQUEST
    // ********************
    VfsFileSystemUnregisterRequest(fileSystem, request);
    VfsRequestDestroy(request);
}

void WriteFileAbsolute(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    FileSystemHandle_t* handle;
    OsStatus_t          osStatus;
    FileSystem_t*       fileSystem;
    size_t              bytesWritten = 0;
    LargeUInteger_t     position;

    position.u.LowPart  = request->parameters.transfer_absolute.position_low;
    position.u.HighPart = request->parameters.transfer_absolute.position_high;

    TRACE("WriteFileAbsoulte(pid=%u, id=%u, b_id=%u)",
          request->processId,
          request->parameters.transfer_absolute.fileHandle,
          request->parameters.transfer_absolute.bufferHandle);

    osStatus = VfsFileSystemGetByFileHandle(request->parameters.transfer_absolute.fileHandle, &fileSystem);
    if (osStatus != OsSuccess) {
        sys_file_transfer_absolute_response(request->message, osStatus, 0);
        VfsRequestDestroy(request);
        return;
    }
    VfsFileSystemRegisterRequest(fileSystem, request);

    // START OF REQUEST
    // ********************
    osStatus = VfsHandleAccess(request->processId,
                               request->parameters.transfer_absolute.fileHandle,
                               __FILE_WRITE_ACCESS,
                               &handle);
    if (osStatus != OsSuccess) {
        goto respond;
    }

    osStatus = SeekInternal(fileSystem, handle, &position);
    if (osStatus != OsSuccess) {
        goto respond;
    }

    osStatus = WriteInternal(fileSystem, handle,
                             request->parameters.transfer_absolute.bufferHandle,
                             request->parameters.transfer_absolute.offset,
                             request->parameters.transfer_absolute.length,
                             &bytesWritten);

respond:
    sys_file_transfer_absolute_response(request->message, osStatus, bytesWritten);

    // END OF REQUEST
    // ********************
    VfsFileSystemUnregisterRequest(fileSystem, request);
    VfsRequestDestroy(request);
}

static OsStatus_t
FlushInternal(
        _In_ FileSystemHandle_t* handle)
{
    OsStatus_t osStatus = OsSuccess;

    // If no buffering enabled skip, or if not a file skip
    if ((handle->base->Options & __FILE_VOLATILE) || !__IsEntryFile(handle)) {
        return OsSuccess;
    }

    // Empty output buffer
    // - But sanitize the buffers first
    if (handle->base->OutBuffer != NULL && handle->base->OutBufferPosition != 0) {
        size_t BytesWritten = 0;
#if 0
        fileSystem = (FileSystem_t*)entryHandle->File->System;
        status     = fileSystem->Module->WriteFile(&fileSystem->Descriptor, entryHandle, NULL, &BytesWritten);
#endif
        if (BytesWritten != handle->base->OutBufferPosition) {
            return OsDeviceError;
        }
    }
    return osStatus;
}

void Flush(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    FileSystem_t*       fileSystem;
    FileSystemHandle_t* handle;
    OsStatus_t          osStatus;

    osStatus = VfsFileSystemGetByFileHandle(request->parameters.flush.fileHandle, &fileSystem);
    if (osStatus != OsSuccess) {
        sys_file_flush_response(request->message, osStatus);
        VfsRequestDestroy(request);
        return;
    }
    VfsFileSystemRegisterRequest(fileSystem, request);

    // START OF REQUEST
    // ********************
    osStatus = VfsHandleAccess(request->processId,
                               request->parameters.flush.fileHandle,
                               0,
                               &handle);
    if (osStatus == OsSuccess) {
        osStatus = FlushInternal(handle);
    }

    sys_file_flush_response(request->message, osStatus);

    // END OF REQUEST
    // ********************
    VfsFileSystemUnregisterRequest(fileSystem, request);
    VfsRequestDestroy(request);
}

void Move(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    FileSystem_t* fileSystem;
    OsStatus_t    osStatus;

    osStatus = VfsFileSystemGetByPathSafe(request->parameters.move.from, &fileSystem);
    if (osStatus != OsSuccess) {
        sys_file_move_response(request->message, osStatus);
        goto cleanup;
    }
    VfsFileSystemRegisterRequest(fileSystem, request);

    // START OF REQUEST
    // ********************

    // @todo implement using existing fs functions
    _CRT_UNUSED(request);
    _CRT_UNUSED(cancellationToken);

    sys_file_move_response(request->message, OsNotSupported);

    // END OF REQUEST
    // ********************
    VfsFileSystemUnregisterRequest(fileSystem, request);

cleanup:
    free((void*)request->parameters.move.from);
    free((void*)request->parameters.move.to);
    VfsRequestDestroy(request);
}

void Link(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    FileSystem_t* fileSystem;
    OsStatus_t    osStatus;

    osStatus = VfsFileSystemGetByPathSafe(request->parameters.link.from, &fileSystem);
    if (osStatus != OsSuccess) {
        sys_file_link_response(request->message, osStatus);
        goto cleanup;
    }
    VfsFileSystemRegisterRequest(fileSystem, request);

    // START OF REQUEST
    // ********************

    // @todo not implemented
    _CRT_UNUSED(request);
    _CRT_UNUSED(cancellationToken);

    sys_file_link_response(request->message, osStatus);

    // END OF REQUEST
    // ********************
    VfsFileSystemUnregisterRequest(fileSystem, request);

cleanup:
    free((void*)request->parameters.link.from);
    free((void*)request->parameters.link.to);
    VfsRequestDestroy(request);
}

void GetPosition(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    FileSystem_t*       fileSystem;
    FileSystemHandle_t* handle;
    OsStatus_t          osStatus;
    LargeUInteger_t     value;

    osStatus = VfsFileSystemGetByFileHandle(request->parameters.get_position.fileHandle, &fileSystem);
    if (osStatus != OsSuccess) {
        sys_file_get_position_response(request->message, osStatus, 0, 0);
        VfsRequestDestroy(request);
        return;
    }
    VfsFileSystemRegisterRequest(fileSystem, request);

    // START OF REQUEST
    // ********************
    value.QuadPart = 0;
    osStatus = VfsHandleAccess(request->processId,
                               request->parameters.get_position.fileHandle,
                               0, &handle);
    if (osStatus == OsSuccess) {
        value.QuadPart = handle->base->Position;
    }

    sys_file_get_position_response(request->message, osStatus,
                                   value.u.LowPart,
                                   value.u.HighPart);

    // END OF REQUEST
    // ********************
    VfsFileSystemUnregisterRequest(fileSystem, request);
    VfsRequestDestroy(request);
}

void GetOptions(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    FileSystem_t*       fileSystem;
    FileSystemHandle_t* handle;
    OsStatus_t          osStatus;
    unsigned int        options = 0;
    unsigned int        access = 0;

    osStatus = VfsFileSystemGetByFileHandle(request->parameters.get_options.fileHandle, &fileSystem);
    if (osStatus != OsSuccess) {
        sys_file_get_options_response(request->message, osStatus, 0, 0);
        VfsRequestDestroy(request);
        return;
    }
    VfsFileSystemRegisterRequest(fileSystem, request);

    // START OF REQUEST
    // ********************
    osStatus = VfsHandleAccess(request->processId,
                               request->parameters.get_position.fileHandle,
                               0, &handle);
    if (osStatus == OsSuccess) {
        options = handle->base->Options;
        access  = handle->base->Access;
    }

    sys_file_get_options_response(request->message, osStatus, options, access);

    // END OF REQUEST
    // ********************
    VfsFileSystemUnregisterRequest(fileSystem, request);
    VfsRequestDestroy(request);
}

void SetOptions(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    FileSystem_t*       fileSystem;
    FileSystemHandle_t* handle;
    OsStatus_t          osStatus;

    osStatus = VfsFileSystemGetByFileHandle(request->parameters.set_options.fileHandle, &fileSystem);
    if (osStatus != OsSuccess) {
        sys_file_set_options_response(request->message, osStatus);
        VfsRequestDestroy(request);
        return;
    }
    VfsFileSystemRegisterRequest(fileSystem, request);

    // START OF REQUEST
    // ********************
    osStatus = VfsHandleAccess(request->processId,
                               request->parameters.get_position.fileHandle,
                               0, &handle);
    if (osStatus != OsSuccess) {
        handle->base->Options = request->parameters.set_options.options;
        handle->base->Access  = request->parameters.set_options.access;
    }

    sys_file_set_options_response(request->message, osStatus);

    // END OF REQUEST
    // ********************
    VfsFileSystemUnregisterRequest(fileSystem, request);
    VfsRequestDestroy(request);
}

void GetSize(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    FileSystem_t*       fileSystem;
    FileSystemHandle_t* handle;
    OsStatus_t          osStatus;
    LargeUInteger_t     value;

    osStatus = VfsFileSystemGetByFileHandle(request->parameters.get_size.fileHandle, &fileSystem);
    if (osStatus != OsSuccess) {
        sys_file_get_size_response(request->message, osStatus, 0, 0);
        VfsRequestDestroy(request);
        return;
    }
    VfsFileSystemRegisterRequest(fileSystem, request);

    // START OF REQUEST
    // ********************
    value.QuadPart = 0;
    osStatus = VfsHandleAccess(request->processId,
                               request->parameters.get_position.fileHandle,
                               0, &handle);
    if (osStatus == OsSuccess) {
        value.QuadPart = handle->entry->base->Descriptor.Size.QuadPart;
    }

    sys_file_get_size_response(request->message, osStatus,
                               value.u.LowPart,
                               value.u.HighPart);

    // END OF REQUEST
    // ********************
    VfsFileSystemUnregisterRequest(fileSystem, request);
    VfsRequestDestroy(request);
}

void SetSize(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    FileSystem_t*       fileSystem;
    FileSystemHandle_t* handle;
    OsStatus_t          osStatus;

    osStatus = VfsFileSystemGetByFileHandle(request->parameters.set_size.fileHandle, &fileSystem);
    if (osStatus != OsSuccess) {
        sys_file_set_size_response(request->message, osStatus);
        VfsRequestDestroy(request);
        return;
    }
    VfsFileSystemRegisterRequest(fileSystem, request);

    // START OF REQUEST
    // ********************
    osStatus = VfsHandleAccess(request->processId,
                               request->parameters.get_position.fileHandle,
                               0, &handle);
    if (osStatus == OsSuccess) {
        // @todo not implemented
    }

    sys_file_set_size_response(request->message, OsNotSupported);

    // END OF REQUEST
    // ********************
    VfsFileSystemUnregisterRequest(fileSystem, request);
    VfsRequestDestroy(request);
}

void GetFullPathByHandle(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    FileSystem_t*       fileSystem;
    FileSystemHandle_t* handle;
    OsStatus_t          osStatus;
    MString_t*          fullPath;
    char                zero[1] = { 0 };

    osStatus = VfsFileSystemGetByFileHandle(request->parameters.stat_handle.fileHandle, &fileSystem);
    if (osStatus != OsSuccess) {
        sys_file_get_path_response(request->message, osStatus, &zero[0]);
        VfsRequestDestroy(request);
        return;
    }
    VfsFileSystemRegisterRequest(fileSystem, request);

    // START OF REQUEST
    // ********************
    osStatus = VfsHandleAccess(request->processId,
                               request->parameters.stat_handle.fileHandle,
                               0, &handle);
    if (osStatus == OsSuccess) {
        fullPath = MStringCreate(NULL, StrUTF8);
        if (fullPath) {
            MStringAppend(fullPath, fileSystem->mount_point);
            MStringAppendCharacter(fullPath, '/');
            MStringAppend(fullPath, handle->entry->path);

            sys_file_get_path_response(request->message, osStatus, MStringRaw(fullPath));
            MStringDestroy(fullPath);
        }
        else {
            sys_file_get_path_response(request->message, osStatus, &zero[0]);
        }
    }
    else {
        sys_file_get_path_response(request->message, osStatus, &zero[0]);
    }

    // END OF REQUEST
    // ********************
    VfsFileSystemUnregisterRequest(fileSystem, request);
    VfsRequestDestroy(request);
}

void StatFromHandle(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    struct sys_file_descriptor gdescriptor = { 0 };

    FileSystem_t*       fileSystem;
    FileSystemHandle_t* handle;
    OsStatus_t          osStatus;

    osStatus = VfsFileSystemGetByFileHandle(request->parameters.stat_handle.fileHandle, &fileSystem);
    if (osStatus != OsSuccess) {
        sys_file_fstat_response(request->message, osStatus, &gdescriptor);
        VfsRequestDestroy(request);
        return;
    }
    VfsFileSystemRegisterRequest(fileSystem, request);

    // START OF REQUEST
    // ********************
    osStatus = VfsHandleAccess(request->processId,
                               request->parameters.stat_handle.fileHandle,
                               0, &handle);
    if (osStatus == OsSuccess) {
        to_sys_file_descriptor(&handle->entry->base->Descriptor, &gdescriptor);
    }
    sys_file_fstat_response(request->message, osStatus, &gdescriptor);

    // END OF REQUEST
    // ********************
    VfsFileSystemUnregisterRequest(fileSystem, request);
    VfsRequestDestroy(request);
}

void StatFromPath(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    struct sys_file_descriptor gdescriptor = { 0 };

    FileSystem_t*       fileSystem;
    FileSystemHandle_t* handle;
    OsStatus_t          osStatus;
    MString_t*          resolvedPath;
    MString_t*          subPath;
    UUId_t              handleId;

    // If path is not absolute or special, we should ONLY try either
    // the current working directory.
    resolvedPath = VfsPathResolve(request->processId, request->parameters.stat_path.path);
    if (!resolvedPath) {
        sys_file_fstat_path_response(request->message, OsDoesNotExist, &gdescriptor);
        goto cleanup;
    }

    fileSystem = VfsFileSystemGetByPath(resolvedPath, &subPath);
    if (!fileSystem) {
        sys_file_fstat_path_response(request->message, OsDoesNotExist, &gdescriptor);
        goto cleanup;
    }
    VfsFileSystemRegisterRequest(fileSystem, request);

    // START OF REQUEST
    // ********************
    osStatus = VfsOpenFileInternal(request->processId,
                                   fileSystem, subPath,
                                   0,
                                   __FILE_READ_ACCESS | __FILE_READ_SHARE,
                                   &handleId);
    if (osStatus == OsSuccess) {
        osStatus = VfsHandleAccess(request->processId, handleId, 0, &handle);
        if (osStatus == OsSuccess) {
            to_sys_file_descriptor(&handle->entry->base->Descriptor, &gdescriptor);
            (void)CloseHandleInternal(request->processId, handle);
        }
    }
    sys_file_fstat_path_response(request->message, osStatus, &gdescriptor);

    // END OF REQUEST
    // ********************
    VfsFileSystemUnregisterRequest(fileSystem, request);

cleanup:
    free((void*)request->parameters.stat_path.path);
    VfsRequestDestroy(request);
}

void StatLinkPathFromPath(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    FileSystem_t*       fileSystem;
    MString_t*          resolvedPath;
    MString_t*          subPath;
    char                zero[1] = { 0 };

    // If path is not absolute or special, we should ONLY try either
    // the current working directory.
    resolvedPath = VfsPathResolve(request->processId, request->parameters.stat_path.path);
    if (!resolvedPath) {
        sys_file_fstat_link_response(request->message, OsDoesNotExist, &zero[0]);
        goto cleanup;
    }

    fileSystem = VfsFileSystemGetByPath(resolvedPath, &subPath);
    if (!fileSystem) {
        sys_file_fstat_link_response(request->message, OsDoesNotExist, &zero[0]);
        goto cleanup;
    }
    VfsFileSystemRegisterRequest(fileSystem, request);

    // START OF REQUEST
    // ********************
    // @todo missing implementation
    sys_file_fstat_link_response(request->message, OsNotSupported, &zero[0]);

    // END OF REQUEST
    // ********************
    VfsFileSystemUnregisterRequest(fileSystem, request);

cleanup:
    free((void*)request->parameters.stat_path.path);
    VfsRequestDestroy(request);
}

void GetFullPathByPath(FileSystemRequest_t* request, void* cancellationToken)
{

}
