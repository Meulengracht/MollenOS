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
#include <vfs/filesystem.h>
#include <vfs/requests.h>
#include <vfs/scope.h>
#include <vfs/storage.h>
#include <vfs/vfs.h>
#include <stdlib.h>

#include <sys_mount_service_server.h>

static uint32_t __AccessFlagsFromMountFlags(enum sys_mount_flags mountFlags)
{
    uint32_t access = 0;
    if (mountFlags & SYS_MOUNT_FLAGS_READ) {
        access |= FILE_PERMISSION_READ;
    }
    if (mountFlags & SYS_MOUNT_FLAGS_WRITE) {
        access |= FILE_PERMISSION_WRITE;
    }
    return access;
}

static oserr_t __MountFile(
        _In_ struct VFS*          vfs,
        _In_ const char*          cpath,
        /* TODO: file offset */
        /* TODO: user provide their own FS interface */
        _In_ uuid_t               atHandle,
        _In_ const char*          fsType,
        _In_ enum sys_mount_flags flags)
{
    struct VFSStorage* storage;
    struct FileSystem* fileSystem;
    uuid_t             handle;
    oserr_t            oserr;

    oserr = VFSNodeOpen(
            vfs, cpath,
            0,
            __AccessFlagsFromMountFlags(flags),
            &handle
    );
    if (oserr != OsOK) {
        return oserr;
    }

    // The first thing we need to do is to create as file-backed storage medium.
    // Then we need to somehow discover the correct filesystem, either by using the
    // hint provided, or allowing the user to specify a filesystem interface implementation.
    storage = VFSStorageCreateFileBacked(handle);
    if (storage == NULL) {
        (void)VFSNodeClose(vfs, handle);
        return OsOutOfMemory;
    }

    // Then we can safely create a new VFS from this.
    fileSystem = VFSStorageRegisterFileSystem(
            storage,
            0,
            0,
            NULL,
            0
    );
    if (fileSystem == NULL) {
        VFSStorageDelete(storage);
        (void)VFSNodeClose(vfs, handle);
        return OsOutOfMemory;
    }
}

static oserr_t __MountPath(
        _In_ struct VFS*          vfs,
        _In_ const char*          cpath,
        _In_ uuid_t               atHandle,
        _In_ const char*          fsType,
        _In_ enum sys_mount_flags flags)
{
    uuid_t  handle;
    oserr_t oserr;

    oserr = VFSNodeOpen(
            vfs, cpath,
            __FILE_DIRECTORY,
            FILE_PERMISSION_READ | FILE_PERMISSION_WRITE,
            &handle
    );
    if (oserr != OsOK) {
        if (oserr == OsPathIsNotDirectory) {
            // mount as a file instead
        }
        return oserr;
    }

    // If the node we are mounting is a directory, then we are simply creating
    // a "link" between the two nodes
    oserr = VFSNodeBind(vfs, handle, atHandle);
    if (oserr != OsOK) {
        // Ok something went wrong, maybe it's already bind mounted.
        (void)VFSNodeClose(vfs, handle);
        return oserr;
    }

    // Register the mount

}

static oserr_t __MountSpecial(
        _In_ struct VFS*     vfs,
        _In_ uuid_t          atHandle,
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
    uuid_t          atHandle = UUID_INVALID;
    oserr_t         oserr;
    struct VFSNode* atNode;
    _CRT_UNUSED(cancellationToken);

    if (fsScope == NULL) {
        sys_mount_mount_response(request->message, OsInvalidPermissions);
        return;
    }

    // Get a handle on the directory we are mounting on top of. This directory
    // must exist for the length of the mount.
    oserr = VFSNodeOpen(
            fsScope,
            request->parameters.mount.at,
            __FILE_DIRECTORY,
            FILE_PERMISSION_READ | FILE_PERMISSION_WRITE,
            &atHandle
    );
    if (oserr != OsOK) {
        goto cleanup;
    }

    // If a path is provided, then we are either mounting a directory or
    // an image file. If no path is provided, then we are mounting a special
    // filesystem on top of the path given in <at>
    if (request->parameters.mount.path) {
        oserr = __MountPath(
                fsScope, request->parameters.mount.path, atHandle,
                request->parameters.mount.path,
                (enum sys_mount_flags)request->parameters.mount.flags
        );
    } else {
        oserr = __MountSpecial(
                fsScope, atHandle,
                request->parameters.mount.fs_type
        );
    }

cleanup:
    sys_mount_mount_response(request->message, oserr);

    // Cleanup resources allocated by us and the request
    if (oserr != OsOK && atHandle != UUID_INVALID) {
        (void)VFSNodeClose(fsScope, atHandle);
    }
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
