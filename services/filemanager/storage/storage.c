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
 * File Manager Service
 * - Handles all file related services and disk services
 */
#define __TRACE

#include <ctype.h>
#include <ddk/convert.h>
#include <ddk/utils.h>
#include <internal/_ipc.h>
#include <os/usched/usched.h>
#include <stdlib.h>
#include <vfs/storage.h>
#include <vfs/filesystem.h>
#include <vfs/requests.h>

#include "sys_storage_service_server.h"

struct VfsRemoveDiskRequest {
    UUId_t       disk_id;
    unsigned int flags;
};

static list_t            g_disks = LIST_INIT;
static struct usched_mtx g_diskLock;

extern void VfsStorageInitialize(void)
{
    usched_mtx_init(&g_diskLock);
}

OsStatus_t
VfsStorageRegisterFileSystem(
        _In_ FileSystemStorage_t* storage,
        _In_ uint64_t             sector,
        _In_ uint64_t             sectorCount,
        _In_ enum FileSystemType  type,
        _In_ guid_t*              typeGuid,
        _In_ guid_t*              guid)
{
    FileSystem_t* fileSystem;
    UUId_t        id;

    TRACE("VfsStorageRegisterFileSystem(sector=%u, sectorCount=%u, type=%u)",
          LODWORD(sector), LODWORD(sectorCount), type);

    id = VfsIdentifierAllocate(storage);
    fileSystem = VfsFileSystemCreate(
            &storage->storage, id,
            sector, sectorCount, type,
            typeGuid, guid
    );
    if (!fileSystem) {
        return OsOutOfMemory;
    }

    usched_mtx_lock(&storage->lock);
    list_append(&storage->filesystems, &fileSystem->header);
    usched_mtx_unlock(&storage->lock);

    // we must wait for an MFS to be registered before trying to load
    // additional drivers due to the fact that we only come bearing MFS driver in the initrd (for now)
    if (fileSystem->type == FileSystemType_MFS) {;
        VfsFileSystemMount(fileSystem, NULL);
    }
    return OsSuccess;
}

static void
VfsStorageEnumerate(
        _In_ FileSystemStorage_t* storage,
        _In_ void*                cancellationToken)
{
    struct vali_link_message   msg  = VALI_MSG_INIT_HANDLE(storage->storage.driver_id);
    OsStatus_t                 osStatus;
    struct sys_disk_descriptor gdescriptor;
    TRACE("VfsStorageEnumerate()");

    usched_mtx_lock(&g_diskLock);
    list_append(&g_disks, &storage->header);
    usched_mtx_unlock(&g_diskLock);

    ctt_storage_stat(GetGrachtClient(), &msg.base, storage->storage.device_id);
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    ctt_storage_stat_result(GetGrachtClient(), &msg.base, &osStatus, &gdescriptor);
    if (osStatus != OsSuccess) {
        // TODO: disk states
        // Disk->State = Crashed
        return;
    }
    from_sys_disk_descriptor_dkk(&gdescriptor, &storage->storage.descriptor);
    
    // Detect the disk layout, and if it fails
    // try to detect which kind of filesystem is present
    osStatus = VfsStorageParse(storage);
    if (osStatus != OsSuccess) {
        // TODO: disk states
        // Disk->State = Unmounted // no filesystems exposed
    }
}

static FileSystemStorage_t*
VfsStorageCreate(
        _In_ UUId_t       deviceId,
        _In_ UUId_t       driverId,
        _In_ unsigned int flags)
{
    FileSystemStorage_t* vfsStorage;

    TRACE("VfsStorageCreate(driver %u, device %u, flags 0x%x)", driverId, deviceId, flags);

    // Allocate a new instance of a disk descriptor
    // to store data and store initial data
    vfsStorage = (FileSystemStorage_t*)malloc(sizeof(FileSystemStorage_t));
    if (!vfsStorage) {
        ERROR("VfsStorageCreate ran out of memory");
        return NULL;
    }

    ELEMENT_INIT(&vfsStorage->header, (uintptr_t)deviceId, vfsStorage);
    vfsStorage->storage.driver_id = driverId;
    vfsStorage->storage.device_id = deviceId;
    vfsStorage->storage.flags     = flags;
    usched_mtx_init(&vfsStorage->lock);
    list_construct(&vfsStorage->filesystems);

    // TODO: disk states
    //Disk->State = Initializing
    return vfsStorage;
}

void sys_storage_register_invocation(struct gracht_message* message,
        const UUId_t driverId, const UUId_t deviceId, const enum sys_storage_flags flags)
{
    FileSystemStorage_t* storage = VfsStorageCreate(deviceId, driverId, (unsigned int)flags);
    if (!storage) {
        // TODO
        ERROR("sys_storage_register_invocation FAILED TO CREATE STORAGE STRUCTURE");
        return;
    }
    usched_task_queue((usched_task_fn)VfsStorageEnumerate, storage);
}

static void
UnmountStorage(
        _In_ FileSystemStorage_t* storage,
        _In_ unsigned int         flags)
{
    element_t* i;

    usched_mtx_lock(&storage->lock);
    _foreach(i, &storage->filesystems) {
        FileSystem_t* fileSystem = (FileSystem_t*)i->value;
        VfsFileSystemUnmount(fileSystem, flags);
    }
    usched_mtx_unlock(&storage->lock);
}

static void
StorageDestroy(
        _In_ struct VfsRemoveDiskRequest* request,
        _In_ void*                        cancellationToken)
{
    element_t* header;

    usched_mtx_lock(&g_diskLock);
    header = list_find(&g_disks, (void*)(uintptr_t)request->disk_id);
    if (header) {
        list_remove(&g_disks, header);
    }
    usched_mtx_unlock(&g_diskLock);

    if (header) {
        FileSystemStorage_t* storage = header->value;
        UnmountStorage(storage, request->flags);
        free(storage);
    }
    free(request);
}

void sys_storage_unregister_invocation(struct gracht_message* message, const UUId_t deviceId, const uint8_t forced)
{
    struct VfsRemoveDiskRequest* request = malloc(sizeof(struct VfsRemoveDiskRequest));
    if (!request) {
        ERROR("sys_storage_unregister_invocation FAILED TO UNREGISTER DISK");
        return;
    }

    request->disk_id = deviceId;
    request->flags = forced;
    usched_task_queue((usched_task_fn)StorageDestroy, request);
}

void
StatStorageByHandle(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    struct sys_disk_descriptor gdescriptor = { 0 };
    FileSystem_t*              fileSystem;
    OsStatus_t                 status;

    status = VfsFileSystemGetByFileHandle(request->parameters.stat_handle.fileHandle, &fileSystem);
    if (status == OsSuccess) {
        to_sys_disk_descriptor_dkk(&fileSystem->base.Disk.descriptor, &gdescriptor);
    }
    sys_storage_get_descriptor_response(request->message, status, &gdescriptor);
    VfsRequestDestroy(request);
}

void
StatStorageByPath(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    struct sys_disk_descriptor gdescriptor = { 0 };
    FileSystem_t*              fileSystem;
    OsStatus_t                 status;

    status = VfsFileSystemGetByPathSafe(request->parameters.stat_path.path, &fileSystem);
    if (status == OsSuccess) {
        to_sys_disk_descriptor_dkk(&fileSystem->base.Disk.descriptor, &gdescriptor);
    }
    sys_storage_get_descriptor_path_response(request->message, OsNotSupported, &gdescriptor);

    free((void*)request->parameters.stat_path.path);
    VfsRequestDestroy(request);
}
