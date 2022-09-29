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
 */

#include <ds/mstring.h>
#include <vfs/requests.h>
#include <vfs/scope.h>
#include <vfs/vfs.h>
#include "vfs/private.h"
#include <stdlib.h>

#include <sys_mount_service_server.h>

static oserr_t __MountPath(
        _In_ struct VFS*          vfs,
        _In_ mstring_t*           path,
        _In_ struct VFSNode*      at,
        _In_ const char*          fsType,
        _In_ enum sys_mount_flags flags)
{
    struct VFSNode* node;
    oserr_t         oserr;

    oserr = VFSNodeGet(vfs, path, 1, &node);
    if (oserr != OsOK) {
        return oserr;
    }

    // If the node we are mounting is a directory, then we are simply creating
    // a "link" between the two nodes
    if (__NodeIsDirectory(node)) {
        return VFSNodeBind(vfs, node, at);
    }

    // If not, then it's a bit more complex. We need to create a new VFS
    // from the file as a storage medium. So the first thing we need to do
    // is to create as file-backed storage medium.

    // Then we can safely create a new VFS from this.
}

static oserr_t __MountSpecial(
        _In_ struct VFS*     vfs,
        _In_ struct VFSNode* at,
        _In_ const char*     fsType)
{
    if (fsType == NULL) {
        return OsInvalidParameters;
    }

    // Verify against supported special fs's
    if (!strcmp(fsType, "memfs")) {

    }
    return OsInvalidParameters;
}

void Mount(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    struct VFS*     fsScope = VFSScopeGet(request->processId);
    mstring_t*      at;
    oserr_t         oserr;
    struct VFSNode* atNode;
    _CRT_UNUSED(cancellationToken);

    if (fsScope == NULL) {
        sys_mount_mount_response(request->message, OsInvalidPermissions);
        return;
    }

    at = mstr_new_u8(request->parameters.mount.at);
    oserr = VFSNodeGet(fsScope, at, 1, &atNode);
    if (oserr != OsOK) {
        goto cleanup;
    }

    // If a path is provided, then we are either mounting a directory or
    // an image file. If no path is provided, then we are mounting a special
    // filesystem on top of the path given in <at>
    if (request->parameters.mount.path) {
        mstring_t* path = mstr_new_u8(request->parameters.mount.path);
        oserr = __MountPath(
                fsScope, path, atNode,
                request->parameters.mount.path,
                (enum sys_mount_flags)request->parameters.mount.flags
        );
        mstr_delete(path);
    } else {
        oserr = __MountSpecial(fsScope, atNode, request->parameters.mount.fs_type);
    }

cleanup:
    sys_mount_mount_response(request->message, oserr);

    // Cleanup resources allocated by us and the request
    mstr_delete(at);
    free((void*)request->parameters.mount.path);
    free((void*)request->parameters.mount.at);
    free((void*)request->parameters.mount.fs_type);
    VfsRequestDestroy(request);
}

void Unmount(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{

    struct VFS*     fsScope = VFSScopeGet(request->processId);
    mstring_t*      path;
    oserr_t         oserr;
    struct VFSNode* node;
    _CRT_UNUSED(cancellationToken);

    if (fsScope == NULL) {
        sys_mount_unmount_response(request->message, OsInvalidPermissions);
        return;
    }

    path = mstr_new_u8(request->parameters.unmount.path);
    oserr = VFSNodeGet(fsScope, path, 1, &node);
    if (oserr != OsOK) {
        goto cleanup;
    }

cleanup:
    sys_mount_unmount_response(request->message, oserr);

    // Cleanup resources allocated by us and the request
    mstr_delete(path);
    free((void*)request->parameters.unmount.path);
    VfsRequestDestroy(request);
}
