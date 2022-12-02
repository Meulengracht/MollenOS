/**
 * Copyright 2021, Philip Meulengracht
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
 * Virtual File Request Definitions & Structures
 * - This header describes the base virtual file-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

//#define __TRACE

#include <ddk/convert.h>
#include <ddk/utils.h>
#include <stdlib.h>
#include <string.h>
#include <vfs/scope.h>
#include <vfs/stat.h>
#include <vfs/vfs.h>

#include <sys_file_service_server.h>
#include <sys_mount_service_server.h>

extern oserr_t Mount(struct VFS* fsScope, const char* path, const char* at, const char* fsType, unsigned int flags);

void sys_file_open_invocation(struct gracht_message* message,
                              const uuid_t processId, const char* path,
                              const unsigned int options, const unsigned int access)
{
    struct VFS* fsScope = VFSScopeGet(processId);
    uuid_t      handle   = UUID_INVALID;
    oserr_t     osStatus;

    TRACE("svc_file_open_callback()");
    if (!strlen(path)) {
        sys_file_open_response(message, OS_EINVALPARAMS, UUID_INVALID);
        return;
    }

    if (fsScope == NULL) {
        sys_file_open_response(message, OS_EPERMISSIONS, UUID_INVALID);
        return;
    }

    // perform initial input verification
    if (!path) {
        sys_file_open_response(message, OS_EINVALPARAMS, UUID_INVALID);
        return;
    }

    osStatus = VFSNodeOpen(fsScope,path, options, access, &handle);
    sys_file_open_response(message, osStatus, handle);
}

void sys_file_close_invocation(struct gracht_message* message, const uuid_t processId, const uuid_t handle)
{
    TRACE("sys_file_close_invocation()");
    struct VFS* fsScope = VFSScopeGet(processId);
    oserr_t     osStatus;
    if (fsScope == NULL) {
        sys_file_close_response(message, OS_EPERMISSIONS);
        return;
    }

    osStatus = VFSNodeClose(fsScope, handle);
    sys_file_close_response(message, osStatus);
}

void sys_file_delete_invocation(struct gracht_message* message,
                                const uuid_t processId, const char* path, const unsigned int flags)
{
    TRACE("sys_file_delete_invocation()");
    struct VFS* fsScope = VFSScopeGet(processId);
    oserr_t     oserr;
    if (fsScope == NULL) {
        sys_file_delete_response(message, OS_EPERMISSIONS);
        return;
    }

    oserr = VFSNodeUnlink(fsScope, path);
    sys_file_delete_response(message, oserr);
}

void sys_file_transfer_invocation(struct gracht_message* message, const uuid_t processId, const uuid_t handle,
                                  const enum sys_transfer_direction direction, const uuid_t bufferHandle,
                                  const size_t offset, const size_t length)
{
    struct VFS* fsScope = VFSScopeGet(processId);
    size_t      transferred = 0;
    oserr_t     oserr;
    TRACE("sys_file_transfer_invocation()");

    if (bufferHandle == UUID_INVALID || length == 0) {
        sys_file_transfer_response(message, OS_EINVALPARAMS, 0);
        return;
    }
    if (fsScope == NULL) {
        sys_file_transfer_response(message, OS_EPERMISSIONS, 0);
        return;
    }

    if (direction == SYS_TRANSFER_DIRECTION_READ) {
        oserr = VFSNodeRead(handle, bufferHandle, offset, length, &transferred);
    } else {
        oserr = VFSNodeWrite(handle, bufferHandle, offset, length, &transferred);
    }
    sys_file_transfer_response(message, oserr, transferred);
}

void sys_file_seek_invocation(struct gracht_message* message, const uuid_t processId,
                              const uuid_t handle, const unsigned int seekLow, const unsigned int seekHigh)
{
    struct VFS* fsScope = VFSScopeGet(processId);
    uint64_t    position = 0;
    oserr_t     oserr;
    TRACE("sys_file_seek_invocation()")

    if (fsScope == NULL) {
        sys_file_seek_response(message, OS_EPERMISSIONS);
        return;
    }

    oserr = VFSNodeSeek(handle, &(UInteger64_t) {
            .u.LowPart = seekLow,
            .u.HighPart = seekHigh
    }, &position);
    sys_file_seek_response(message, oserr);
}

void sys_file_transfer_absolute_invocation(struct gracht_message* message, const uuid_t processId,
                                           const uuid_t handle, const enum sys_transfer_direction direction,
                                           const unsigned int seekLow, const unsigned int seekHigh,
                                           const uuid_t bufferHandle, const size_t offset, const size_t length)
{
    struct VFS* fsScope = VFSScopeGet(processId);
    size_t      transferred = 0;
    oserr_t     oserr;
    TRACE("sys_file_transfer_absolute_invocation()");

    if (bufferHandle == UUID_INVALID || length == 0) {
        sys_file_transfer_absolute_response(message, OS_EINVALPARAMS, 0);
        return;
    }
    if (fsScope == NULL) {
        sys_file_transfer_absolute_response(message, OS_EPERMISSIONS, 0);
        return;
    }

    if (direction == SYS_TRANSFER_DIRECTION_READ) {
        oserr = VFSNodeReadAt(
                handle,
                &(UInteger64_t) {
                    .u.LowPart = seekLow,
                    .u.HighPart = seekHigh
                },
                bufferHandle,
                offset,
                length,
                &transferred
        );
    } else {
        oserr = VFSNodeWriteAt(
                handle,
                &(UInteger64_t) {
                        .u.LowPart = seekLow,
                        .u.HighPart = seekHigh
                },
                bufferHandle,
                offset,
                length,
                &transferred
        );
    }
    sys_file_transfer_absolute_response(message, oserr, transferred);
}

void sys_file_mkdir_invocation(struct gracht_message* message, const uuid_t processId, const char* path, const unsigned int permissions)
{
    struct VFS* fsScope = VFSScopeGet(processId);
    uuid_t      handle;
    oserr_t     oserr;
    mstring_t*  mpath;
    TRACE("sys_file_mkdir_invocation()");

    if (!strlen(path)) {
        sys_file_mkdir_response(message, OS_EINVALPARAMS);
        return;
    }
    if (fsScope == NULL) {
        sys_file_mkdir_response(message, OS_EPERMISSIONS);
        return;
    }

    mpath = mstr_new_u8(path);
    oserr = VFSNodeMkdir(fsScope, mpath, permissions, &handle);
    mstr_delete(mpath);
    if (oserr == OS_EOK) {
        // We don't need it.
        VFSNodeClose(fsScope, handle);
    }
    sys_file_mkdir_response(message, oserr);
}

static void __ToSysDirectoryEntry(struct VFSStat* in, struct sys_directory_entry* out, uint32_t index)
{
    out->name = mstr_u8(in->Name);
    out->id = in->ID;
    out->flags = in->Flags;
    out->index = index;
}

void sys_file_readdir_invocation(struct gracht_message* message, const uuid_t processId, const uuid_t handle)
{
    struct VFSStat             stats;
    struct sys_directory_entry entry = { 0 };
    struct VFS*                fsScope;
    uint32_t                   index;
    oserr_t                    oserr;
    TRACE("sys_file_readdir_invocation()");

    fsScope = VFSScopeGet(processId);
    if (fsScope == NULL) {
        sys_file_readdir_response(message, OS_EPERMISSIONS, &entry);
        return;
    }

    oserr = VFSNodeReadDirectory(handle, &stats, &index);
    if (oserr == OS_EOK) {
        __ToSysDirectoryEntry(&stats, &entry, index);
    }
    sys_file_readdir_response(message, oserr, &entry);

    // Do cleanup of the sys_directory_entry structure, we unfornately
    // need to convert from mstr into const char. Maybe some day we should
    // do a static conversion ability in mstring library (TODO)
    if (entry.name) {
        free(entry.name);
    }
}

void sys_file_flush_invocation(struct gracht_message* message, const uuid_t processId, const uuid_t handle)
{
    struct VFS* fsScope = VFSScopeGet(processId);
    oserr_t     oserr;
    TRACE("sys_file_flush_invocation()");

    if (fsScope == NULL) {
        sys_file_flush_response(message, OS_EPERMISSIONS);
        return;
    }

    oserr = VFSNodeFlush(handle);
    sys_file_flush_response(message, oserr);
}

void sys_file_move_invocation(struct gracht_message* message, const uuid_t processId,
                              const char* source, const char* destination, const uint8_t copy)
{
    struct VFS* fsScope = VFSScopeGet(processId);
    oserr_t     oserr;
    TRACE("sys_file_move_invocation()");
    if (!strlen(source) || !strlen(destination)) {
        sys_file_move_response(message, OS_EINVALPARAMS);
        return;
    }
    if (fsScope == NULL) {
        sys_file_move_response(message, OS_EPERMISSIONS);
        return;
    }

    oserr = VFSNodeMove(fsScope, source, destination, copy);
    sys_file_move_response(message, oserr);
}

void sys_file_link_invocation(struct gracht_message* message, const uuid_t processId,
                              const char* source, const char* destination, const uint8_t symbolic)
{
    struct VFS* fsScope = VFSScopeGet(processId);
    oserr_t     oserr;
    TRACE("sys_file_link_invocation()");

    if (!strlen(source) || !strlen(destination)) {
        sys_file_link_response(message, OS_EINVALPARAMS);
        return;
    }
    if (fsScope == NULL) {
        sys_file_link_response(message, OS_EPERMISSIONS);
        return;
    }

    oserr = VFSNodeLink(fsScope, source, destination, symbolic);
    sys_file_link_response(message, oserr);
}

void sys_file_get_position_invocation(struct gracht_message* message, const uuid_t processId, const uuid_t handle)
{
    struct VFS*  fsScope = VFSScopeGet(processId);
    UInteger64_t position;
    oserr_t      oserr;
    TRACE("sys_file_get_position_invocation()");

    if (fsScope == NULL) {
        sys_file_get_position_response(message, OS_EPERMISSIONS, 0, 0);
        return;
    }

    oserr = VFSNodeGetPosition(handle,&position.QuadPart);
    sys_file_get_position_response(message, oserr, position.u.LowPart, position.u.HighPart);
}

void sys_file_duplicate_invocation(struct gracht_message* message, const uuid_t processId, const uuid_t handle)
{
    struct VFS* fsScope = VFSScopeGet(processId);
    uuid_t      dupHandle;
    oserr_t     oserr;
    TRACE("sys_file_duplicate_invocation()");

    if (fsScope == NULL) {
        sys_file_duplicate_response(message, OS_EPERMISSIONS, UUID_INVALID);
        return;
    }

    oserr = VFSNodeDuplicate(handle, &dupHandle);
    sys_file_duplicate_response(message, oserr, dupHandle);
}

void sys_file_get_access_invocation(struct gracht_message* message, const uuid_t processId, const uuid_t handle)
{
    struct VFS* fsScope = VFSScopeGet(processId);
    uint32_t    access;
    oserr_t     oserr;
    TRACE("sys_file_get_access_invocation()");

    if (fsScope == NULL) {
        sys_file_get_access_response(message, OS_EPERMISSIONS, 0);
        return;
    }

    oserr = VFSNodeGetAccess(handle, &access);
    sys_file_get_access_response(message, oserr, access);
}

void sys_file_set_access_invocation(struct gracht_message* message, const uuid_t processId,
                                    const uuid_t handle, const unsigned int access)
{
    struct VFS* fsScope = VFSScopeGet(processId);
    oserr_t     oserr;
    TRACE("sys_file_set_access_invocation()");

    if (fsScope == NULL) {
        sys_file_set_access_response(message, OS_EPERMISSIONS);
        return;
    }

    oserr = VFSNodeSetAccess(handle, access);
    sys_file_set_access_response(message, oserr);
}

void sys_file_get_size_invocation(struct gracht_message* message, const uuid_t processId, const uuid_t handle)
{
    struct VFS*  fsScope = VFSScopeGet(processId);
    UInteger64_t size;
    oserr_t      oserr;
    TRACE("sys_file_get_size_invocation()");

    if (fsScope == NULL) {
        sys_file_get_size_response(message, OS_EPERMISSIONS, 0, 0);
        return;
    }

    oserr = VFSNodeGetSize(handle, &size.QuadPart);
    sys_file_get_size_response(message, oserr, size.u.LowPart, size.u.HighPart);
}

void sys_file_set_size_invocation(struct gracht_message* message, const uuid_t processId,
                                  const uuid_t handle, const unsigned int sizeLow, const unsigned int sizeHigh)
{
    struct VFS*  fsScope = VFSScopeGet(processId);
    oserr_t      oserr;
    TRACE("sys_file_set_size_invocation()");

    if (fsScope == NULL) {
        sys_file_set_size_response(message, OS_EPERMISSIONS);
        return;
    }

    oserr = VFSNodeSetSize(handle, &(UInteger64_t) {
            .u.LowPart = sizeLow,
            .u.HighPart = sizeHigh
    });
    sys_file_set_size_response(message, oserr);
}

void sys_file_get_path_invocation(struct gracht_message* message, const uuid_t processId, const uuid_t handle)
{
    struct VFS* fsScope = VFSScopeGet(processId);
    mstring_t*  fullPath;
    oserr_t     oserr;
    TRACE("sys_file_get_path_invocation()");

    if (fsScope == NULL) {
        sys_file_get_path_response(message, OS_EPERMISSIONS, "");
        return;
    }

    oserr = VFSNodeGetPathHandle(handle, &fullPath);
    if (oserr != OS_EOK) {
        sys_file_get_path_response(message, oserr, "");
        return;
    }

    char* pathu8 = mstr_u8(fullPath);
    sys_file_get_path_response(message, OS_EOK, pathu8);
    mstr_delete(fullPath);
    free(pathu8);
}

static void __ToProtocolFileDescriptor(struct VFSStat* in, struct sys_file_descriptor* out)
{
    out->id = in->ID;
    out->storageId = in->StorageID;
    out->flags = in->Flags;
    out->permissions = in->Permissions;
    out->size = in->Size;
    to_sys_timestamp(&in->Accessed, &out->accessed);
    to_sys_timestamp(&in->Modified, &out->modified);
    to_sys_timestamp(&in->Created, &out->created);
}

void sys_file_fstat_invocation(struct gracht_message* message, const uuid_t processId, const uuid_t handle)
{
    struct VFSStat             stats;
    struct sys_file_descriptor result;
    struct VFS*                fsScope = VFSScopeGet(processId);
    oserr_t                    osStatus;
    TRACE("sys_file_fstat_invocation()");

    if (fsScope == NULL) {
        sys_file_fstat_response(message, OS_EPERMISSIONS, &result);
        return;
    }

    osStatus = VFSNodeStatHandle(handle, &stats);
    __ToProtocolFileDescriptor(&stats, &result);
    sys_file_fstat_response(message, osStatus, &result);
}

void sys_file_fstat_path_invocation(struct gracht_message* message, const uuid_t processId, const char* path, const int followLinks)
{
    struct sys_file_descriptor gdescriptor = { 0 };
    struct VFSStat             stats;
    struct VFS*                fsScope = VFSScopeGet(processId);
    oserr_t                    oserr;
    TRACE("sys_file_fstat_path_invocation()");

    if (!strlen(path)) {
        sys_file_fstat_path_response(message, OS_EINVALPARAMS, &gdescriptor);
        return;
    }
    if (fsScope == NULL) {
        sys_file_fstat_path_response(message, OS_EPERMISSIONS, &gdescriptor);
        return;
    }

    oserr = VFSNodeStat(fsScope, path, followLinks, &stats);
    __ToProtocolFileDescriptor(&stats, &gdescriptor);
    sys_file_fstat_path_response(message, oserr, &gdescriptor);
}

void sys_file_fstat_link_invocation(struct gracht_message* message, const uuid_t processId, const char* path)
{
    struct VFS* fsScope = VFSScopeGet(processId);
    mstring_t* linkPath;
    oserr_t    oserr;
    TRACE("sys_file_fstat_link_invocation()");

    if (!strlen(path)) {
        sys_file_fstat_link_response(message, OS_EINVALPARAMS, "");
        return;
    }
    if (fsScope == NULL) {
        sys_file_fstat_link_response(message, OS_EPERMISSIONS, "");
        return;
    }

    oserr = VFSNodeReadLink(fsScope, path, &linkPath);
    if (oserr != OS_EOK) {
        sys_file_fstat_link_response(message, oserr, "");
        return;
    }

    char* pathu8 = mstr_u8(linkPath);
    sys_file_fstat_link_response(message, OS_EOK, pathu8);
    mstr_delete(linkPath);
    free(pathu8);
}

static void __ToProtocolFileSystemDescriptor(struct VFSStatFS* in, struct sys_filesystem_descriptor* out)
{
    out->id = (long)in->ID;
    out->flags = 0;
    out->serial = mstr_u8(in->Serial);
    out->max_filename_length = in->MaxFilenameLength;
    out->block_size = in->BlockSize;
    out->blocks_per_segment = in->BlocksPerSegment;
    out->segments_free = in->SegmentsFree;
    out->segments_total = in->SegmentsTotal;
}

static void __CleanupProtocolFileSystemDescriptor(struct sys_filesystem_descriptor* in)
{
    free(in->serial);
}

void sys_file_fsstat_invocation(struct gracht_message* message, const uuid_t processId, const uuid_t handle)
{
    struct VFSStatFS                 stats;
    struct sys_filesystem_descriptor result;
    struct VFS*                      fsScope = VFSScopeGet(processId);
    oserr_t                          oserr;
    TRACE("sys_file_fsstat_invocation()");

    if (fsScope == NULL) {
        sys_file_fsstat_response(message, OS_EPERMISSIONS, &result);
        return;
    }

    oserr = VFSNodeStatFsHandle(handle, &stats);
    __ToProtocolFileSystemDescriptor(&stats, &result);
    sys_file_fsstat_response(message, oserr, &result);
    __CleanupProtocolFileSystemDescriptor(&result);
}

void sys_file_fsstat_path_invocation(struct gracht_message* message, const uuid_t processId, const char* path, const int followLinks)
{
    struct VFSStatFS                 stats;
    struct sys_filesystem_descriptor result;
    struct VFS*                      fsScope = VFSScopeGet(processId);
    oserr_t                          osStatus;
    TRACE("sys_file_fsstat_path_invocation()");

    if (fsScope == NULL) {
        sys_file_fsstat_path_response(message, OS_EPERMISSIONS, &result);
        return;
    }

    osStatus = VFSNodeStatFs(fsScope, path, followLinks, &stats);
    __ToProtocolFileSystemDescriptor(&stats, &result);
    sys_file_fsstat_path_response(message, osStatus, &result);
}

void sys_file_realpath_invocation(struct gracht_message* message, const char* path, const int followLinks)
{
    struct VFS* fsScope = VFSScopeGet(UUID_INVALID);
    mstring_t*  realPath;
    oserr_t     oserr;
    TRACE("sys_file_realpath_invocation(path=%s)", path);

    if (!strlen(path)) {
        sys_file_realpath_response(message, OS_EINVALPARAMS, "");
        return;
    }
    if (fsScope == NULL) {
        sys_file_realpath_response(message, OS_EPERMISSIONS, "");
        return;
    }

    oserr = VFSNodeRealPath(fsScope, path, followLinks, &realPath);
    if (oserr != OS_EOK) {
        sys_file_realpath_response(message, oserr, "");
        return;
    }

    char* pathu8 = mstr_u8(realPath);
    sys_file_realpath_response(message, OS_EOK, pathu8);
    mstr_delete(realPath);
    free(pathu8);
}

void sys_file_ststat_invocation(struct gracht_message* message, const uuid_t processId, const uuid_t fileHandle)
{
    StorageDescriptor_t        stats;
    struct sys_disk_descriptor result;
    struct VFS*                fsScope = VFSScopeGet(processId);
    oserr_t                    oserr;
    TRACE("sys_file_ststat_invocation()");

    if (fsScope == NULL) {
        sys_file_ststat_response(message, OS_EPERMISSIONS, &result);
        return;
    }

    oserr = VFSNodeStatStorageHandle(fileHandle, &stats);
    to_sys_disk_descriptor_dkk(&stats, &result);
    sys_file_ststat_response(message, oserr, &result);
}

void sys_file_ststat_path_invocation(struct gracht_message* message, const uuid_t processId, const char* filePath, const int followLinks)
{
    struct VFS*                fsScope = VFSScopeGet(processId);
    StorageDescriptor_t        stats;
    struct sys_disk_descriptor gdescriptor = { 0 };
    oserr_t                    oserr;
    TRACE("sys_file_ststat_path_invocation()");

    if (!strlen(filePath)) {
        sys_file_ststat_path_response(message, OS_EINVALPARAMS, &gdescriptor);
        return;
    }
    if (fsScope == NULL) {
        sys_file_ststat_path_response(message, OS_EPERMISSIONS, &gdescriptor);
        return;
    }

    oserr = VFSNodeStatStorage(fsScope, filePath, followLinks, &stats);
    to_sys_disk_descriptor_dkk(&stats, &gdescriptor);
    sys_file_ststat_path_response(message, oserr, &gdescriptor);
}

void sys_mount_mount_invocation(struct gracht_message* message, const uuid_t processId,
                                const char* path, const char* at, const char* fsType, const enum sys_mount_flags flags)
{
    struct VFS* fsScope = VFSScopeGet(processId);
    oserr_t     oserr;
    TRACE("sys_mount_mount_invocation()");

    if (!strlen(at)) {
        sys_mount_mount_response(message, OS_EINVALPARAMS);
        return;
    }
    if (fsScope == NULL) {
        sys_mount_mount_response(message, OS_EPERMISSIONS);
        return;
    }

    oserr = Mount(fsScope, path, at, fsType, flags);
    sys_mount_mount_response(message, oserr);
}

void sys_mount_unmount_invocation(struct gracht_message* message, const uuid_t processId, const char* path)
{
    struct VFS* fsScope = VFSScopeGet(processId);
    mstring_t*  mpath;
    oserr_t     oserr;
    TRACE("sys_mount_unmount_invocation()");

    if (!strlen(path)) {
        sys_mount_mount_response(message, OS_EINVALPARAMS);
        return;
    }

    if (fsScope == NULL) {
        sys_mount_unmount_response(message, OS_EPERMISSIONS);
        return;
    }

    mpath = mstr_new_u8(path);
    oserr = VFSNodeUnmountPath(fsScope, mpath);
    if (oserr != OS_EOK) {
        goto cleanup;
    }

cleanup:
    sys_mount_unmount_response(message, oserr);
    mstr_delete(mpath);
}
