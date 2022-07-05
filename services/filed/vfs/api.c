/**
 * Copyright 2022, Philip Meulengracht
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
 */

#include <assert.h>
#include <ctype.h>
#include <ddk/convert.h>
#include <ddk/utils.h>
#include <os/dmabuf.h>
#include <os/types/file.h>
#include <stdlib.h>
#include <vfs/filesystem.h>
#include <vfs/scope.h>
#include "sys_file_service_server.h"

void OpenFile(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    struct VFS* fsScope = VFSScopeGet(request->processId);
    if (fsScope == NULL) {
        sys_file_open_response(request->message, OsInvalidPermissions, UUID_INVALID);
        return;
    }

    UUId_t     handle   = UUID_INVALID;
    OsStatus_t osStatus = VFSNodeOpen(fsScope, request, &handle);
    sys_file_open_response(request->message, osStatus, handle);

    free((void*)request->parameters.open.path);
    VfsRequestDestroy(request);
}

void CloseFile(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    struct VFS* fsScope = VFSScopeGet(request->processId);
    if (fsScope == NULL) {
        sys_file_close_response(request->message, OsInvalidPermissions);
        return;
    }

    OsStatus_t osStatus = VFSNodeClose(fsScope, request);
    sys_file_close_response(request->message, osStatus);

    VfsRequestDestroy(request);
}

void DeletePath(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    struct VFS* fsScope = VFSScopeGet(request->processId);
    if (fsScope == NULL) {
        sys_file_delete_response(request->message, OsInvalidPermissions);
        return;
    }

    OsStatus_t osStatus = VFSNodeUnlink(fsScope, request);
    sys_file_delete_response(request->message, osStatus);

    free((void*)request->parameters.delete_path.path);
    VfsRequestDestroy(request);
}

void ReadFile(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    struct VFS* fsScope = VFSScopeGet(request->processId);
    if (fsScope == NULL) {
        sys_file_transfer_response(request->message, OsInvalidPermissions, 0);
        return;
    }

    size_t     read     = 0;
    OsStatus_t osStatus = VFSNodeRead(request, &read);
    sys_file_transfer_response(request->message, osStatus, read);

    VfsRequestDestroy(request);
}

void WriteFile(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    struct VFS* fsScope = VFSScopeGet(request->processId);
    if (fsScope == NULL) {
        sys_file_transfer_response(request->message, OsInvalidPermissions, 0);
        return;
    }

    size_t     written  = 0;
    OsStatus_t osStatus = VFSNodeWrite(request, &written);
    sys_file_transfer_response(request->message, osStatus, written);

    VfsRequestDestroy(request);
}

void ReadFileAbsolute(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    struct VFS* fsScope = VFSScopeGet(request->processId);
    if (fsScope == NULL) {
        sys_file_transfer_absolute_response(request->message, OsInvalidPermissions, 0);
        return;
    }

    size_t     read     = 0;
    OsStatus_t osStatus = VFSNodeReadAt(request, &read);
    sys_file_transfer_absolute_response(request->message, osStatus, read);

    VfsRequestDestroy(request);
}

void WriteFileAbsolute(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    struct VFS* fsScope = VFSScopeGet(request->processId);
    if (fsScope == NULL) {
        sys_file_transfer_absolute_response(request->message, OsInvalidPermissions, 0);
        return;
    }

    size_t     written  = 0;
    OsStatus_t osStatus = VFSNodeWriteAt(request, &written);
    sys_file_transfer_absolute_response(request->message, osStatus, written);

    VfsRequestDestroy(request);
}

void Seek(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    struct VFS* fsScope = VFSScopeGet(request->processId);
    if (fsScope == NULL) {
        sys_file_seek_response(request->message, OsInvalidPermissions);
        return;
    }

    uint64_t   position = 0;
    OsStatus_t osStatus = VFSNodeSeek(request, &position);
    sys_file_seek_response(request->message, osStatus);

    VfsRequestDestroy(request);
}

void Flush(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    struct VFS* fsScope = VFSScopeGet(request->processId);
    if (fsScope == NULL) {
        sys_file_flush_response(request->message, OsInvalidPermissions);
        return;
    }

    OsStatus_t osStatus = VFSNodeFlush(request);
    sys_file_flush_response(request->message, osStatus);

    VfsRequestDestroy(request);
}

void Move(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    struct VFS* fsScope = VFSScopeGet(request->processId);
    if (fsScope == NULL) {
        sys_file_move_response(request->message, OsInvalidPermissions);
        return;
    }

    OsStatus_t osStatus = VFSNodeMove(fsScope, request);
    sys_file_move_response(request->message, osStatus);

    VfsRequestDestroy(request);
}

void Link(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    struct VFS* fsScope = VFSScopeGet(request->processId);
    if (fsScope == NULL) {
        sys_file_link_response(request->message, OsInvalidPermissions);
        return;
    }

    OsStatus_t osStatus = VFSNodeLink(fsScope, request);
    sys_file_link_response(request->message, osStatus);

    VfsRequestDestroy(request);
}

void Duplicate(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    struct VFS* fsScope = VFSScopeGet(request->processId);
    if (fsScope == NULL) {
        sys_file_duplicate_response(request->message, OsInvalidPermissions, UUID_INVALID);
        return;
    }

    UUId_t     dupHandle;
    OsStatus_t osStatus = VFSNodeDuplicate(request, &dupHandle);
    sys_file_duplicate_response(request->message, osStatus, dupHandle);

    VfsRequestDestroy(request);
}

void GetPosition(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    struct VFS* fsScope = VFSScopeGet(request->processId);
    if (fsScope == NULL) {
        sys_file_get_position_response(request->message, OsInvalidPermissions, 0, 0);
        return;
    }

    LargeUInteger_t position;
    OsStatus_t      osStatus = VFSNodeGetPosition(request, &position.QuadPart);
    sys_file_get_position_response(request->message, osStatus, position.u.LowPart, position.u.HighPart);

    VfsRequestDestroy(request);
}

void GetAccess(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    struct VFS* fsScope = VFSScopeGet(request->processId);
    if (fsScope == NULL) {
        sys_file_get_access_response(request->message, OsInvalidPermissions, 0);
        return;
    }

    uint32_t   access;
    OsStatus_t osStatus = VFSNodeGetAccess(request, &access);
    sys_file_get_access_response(request->message, osStatus, access);

    VfsRequestDestroy(request);
}

void SetAccess(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    struct VFS* fsScope = VFSScopeGet(request->processId);
    if (fsScope == NULL) {
        sys_file_set_access_response(request->message, OsInvalidPermissions);
        return;
    }

    OsStatus_t osStatus = VFSNodeSetAccess(request);
    sys_file_set_access_response(request->message, osStatus);

    VfsRequestDestroy(request);
}

void GetSize(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationTokenv)
{
    struct VFS* fsScope = VFSScopeGet(request->processId);
    if (fsScope == NULL) {
        sys_file_get_size_response(request->message, OsInvalidPermissions, 0, 0);
        return;
    }

    LargeUInteger_t size;
    OsStatus_t      osStatus = VFSNodeGetSize(request, &size.QuadPart);
    sys_file_get_size_response(request->message, osStatus, size.u.LowPart, size.u.HighPart);

    VfsRequestDestroy(request);
}

void SetSize(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    struct VFS* fsScope = VFSScopeGet(request->processId);
    if (fsScope == NULL) {
        sys_file_set_size_response(request->message, OsInvalidPermissions);
        return;
    }

    OsStatus_t osStatus = VFSNodeSetSize(request);
    sys_file_set_size_response(request->message, osStatus);

    VfsRequestDestroy(request);
}

static void __ToProtocolFileDescriptor(struct VFSStat* in, struct sys_file_descriptor* out)
{
    struct timespec ts;

    out->id = 0; // TODO missing node id
    out->storageId = 0; // TODO missing storage id
    out->flags = in->Flags;
    out->permissions = in->Permissions;
    out->size = in->Size;
    to_sys_timestamp(&ts, &out->accessed); // TODO missing timestamps
    to_sys_timestamp(&ts, &out->modified);
    to_sys_timestamp(&ts, &out->created);
}

void StatFromHandle(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    struct VFSStat             stats;
    struct sys_file_descriptor result;
    struct VFS*                fsScope = VFSScopeGet(request->processId);
    if (fsScope == NULL) {
        sys_file_fstat_response(request->message, OsInvalidPermissions, &result);
        return;
    }

    OsStatus_t osStatus = VFSNodeStatHandle(request, &stats);
    __ToProtocolFileDescriptor(&stats, &result);
    sys_file_fstat_response(request->message, osStatus, &result);

    VfsRequestDestroy(request);
}

void StatFromPath(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    struct VFSStat             stats;
    struct sys_file_descriptor result;
    struct VFS*                fsScope = VFSScopeGet(request->processId);
    if (fsScope == NULL) {
        sys_file_fstat_path_response(request->message, OsInvalidPermissions, &result);
        return;
    }

    OsStatus_t osStatus = VFSNodeStat(fsScope, request, &stats);
    __ToProtocolFileDescriptor(&stats, &result);
    sys_file_fstat_path_response(request->message, osStatus, &result);

    free((void*)request->parameters.stat_path.path);
    VfsRequestDestroy(request);
}

static void __ToProtocolFileSystemDescriptor(struct VFSStatFS* in, struct sys_filesystem_descriptor* out)
{
    out->id = 0; // TODO missing id
    out->flags = 0;  // TODO missing flags
    out->serial = NULL; // TODO missing serial
    out->max_filename_length = 0; // TODO missing filename max
    out->block_size = 512; // TODO missing block size
    out->blocks_per_segment = 1; // TODO missing segment info
    out->segments_free = in->BlocksFree;
    out->segments_total = in->BlocksTotal;
}

void StatFileSystemByHandle(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    struct VFSStatFS                 stats;
    struct sys_filesystem_descriptor result;
    struct VFS*                      fsScope = VFSScopeGet(request->processId);
    if (fsScope == NULL) {
        sys_file_fsstat_response(request->message, OsInvalidPermissions, &result);
        return;
    }

    OsStatus_t osStatus = VFSNodeStatFsHandle(request, &stats);
    __ToProtocolFileSystemDescriptor(&stats, &result);
    sys_file_fsstat_response(request->message, osStatus, &result);

    VfsRequestDestroy(request);
}

void StatFileSystemByPath(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    struct VFSStatFS                 stats;
    struct sys_filesystem_descriptor result;
    struct VFS*                      fsScope = VFSScopeGet(request->processId);
    if (fsScope == NULL) {
        sys_file_fsstat_path_response(request->message, OsInvalidPermissions, &result);
        return;
    }

    OsStatus_t osStatus = VFSNodeStatFs(fsScope, request, &stats);
    __ToProtocolFileSystemDescriptor(&stats, &result);
    sys_file_fsstat_path_response(request->message, osStatus, &result);

    free((void*)request->parameters.stat_path.path);
    VfsRequestDestroy(request);
}

void StatStorageByHandle(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    StorageDescriptor_t        stats;
    struct sys_disk_descriptor result;
    struct VFS*                fsScope = VFSScopeGet(request->processId);
    if (fsScope == NULL) {
        sys_file_ststat_response(request->message, OsInvalidPermissions, &result);
        return;
    }

    OsStatus_t osStatus = VFSNodeStatStorageHandle(request, &stats);
    to_sys_disk_descriptor_dkk(&stats, &result);
    sys_file_ststat_response(request->message, osStatus, &result);

    VfsRequestDestroy(request);
}

void StatStorageByPath(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    StorageDescriptor_t        stats;
    struct sys_disk_descriptor result;
    struct VFS*                fsScope = VFSScopeGet(request->processId);
    if (fsScope == NULL) {
        sys_file_ststat_path_response(request->message, OsInvalidPermissions, &result);
        return;
    }

    OsStatus_t osStatus = VFSNodeStatStorage(fsScope, request, &stats);
    to_sys_disk_descriptor_dkk(&stats, &result);
    sys_file_ststat_path_response(request->message, osStatus, &result);

    free((void*)request->parameters.stat_path.path);
    VfsRequestDestroy(request);
}

void StatLinkPathFromPath(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    struct VFS* fsScope = VFSScopeGet(request->processId);
    if (fsScope == NULL) {
        sys_file_fstat_link_response(request->message, OsInvalidPermissions, "");
        return;
    }

    MString_t* targetPath;
    OsStatus_t osStatus = VFSNode(fsScope, request, &stats);
    sys_file_fstat_link_response(request->message, osStatus, MStringRaw(targetPath));
    MStringDestroy(targetPath);

    free((void*)request->parameters.stat_path.path);
    VfsRequestDestroy(request);
}

void GetFullPathByHandle(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    struct VFS* fsScope = VFSScopeGet(request->processId);
    if (fsScope == NULL) {
        sys_file_get_path_response(request->message, OsInvalidPermissions, "");
        return;
    }

    MString_t* fullPath;
    OsStatus_t osStatus = VFSNodeGetPathHandle(request, &fullPath);
    if (osStatus != OsOK) {
        sys_file_get_path_response(request->message, osStatus, "");
        return;
    }

    sys_file_get_path_response(request->message, OsOK, MStringRaw(fullPath));
    MStringDestroy(fullPath);
}

void RealPath(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    struct VFS* fsScope = VFSScopeGet(request->processId);
    if (fsScope == NULL) {
        sys_file_fstat_link_response(request->message, OsInvalidPermissions, "");
        return;
    }


}
