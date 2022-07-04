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

}

void Flush(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{

}

void Move(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{

}

void Link(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{

}

void Duplicate(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{

}

void GetPosition(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{

}

void GetAccess(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{

}

void SetAccess(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{

}

void GetSize(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationTokenv)
{

}

void SetSize(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{

}

void StatFromHandle(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{

}

void StatFromPath(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{

}

void StatLinkPathFromPath(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{

}

void StatFileSystemByHandle(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{

}

void StatFileSystemByPath(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{

}

void StatStorageByHandle(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{

}

void StatStorageByPath(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{

}

void GetFullPathByHandle(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{

}

void RealPath(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{

}
