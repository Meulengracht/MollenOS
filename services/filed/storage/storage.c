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
    uuid_t       disk_id;
    unsigned int flags;
};

static list_t            g_disks = LIST_INIT;
static struct usched_mtx g_diskLock;

extern void VfsStorageInitialize(void)
{
    usched_mtx_init(&g_diskLock);
}

oscode_t
VfsStorageRegisterFileSystem(
        _In_ FileSystemStorage_t* storage,
        _In_ uint64_t             sector,
        _In_ uint64_t             sectorCount,
        _In_ enum FileSystemType  type,
        _In_ guid_t*              typeGuid,
        _In_ guid_t*              guid)
{
    FileSystem_t* fileSystem;
    uuid_t        id;

    TRACE("VfsStorageRegisterFileSystem(sector=%u, sectorCount=%u, type=%u)",
          LODWORD(sector), LODWORD(sectorCount), type);

    id = VfsIdentifierAllocate(storage);
    fileSystem = FileSystemNew(
            &storage->Storage, id,
            sector, sectorCount, type,
            typeGuid, guid
    );
    if (!fileSystem) {
        return OsOutOfMemory;
    }

    usched_mtx_lock(&storage->Lock);
    list_append(&storage->Filesystems, &fileSystem->Header);
    usched_mtx_unlock(&storage->Lock);

    // we must wait for an MFS to be registered before trying to load
    // additional drivers due to the fact that we only come bearing MFS driver in the initrd (for now)
    if (fileSystem->Type == FileSystemType_MFS) {;
        VfsFileSystemMount(fileSystem, NULL);
    }
    return OsOK;
}

static void
__StorageSetup(
        _In_ FileSystemStorage_t* fsStorage,
        _In_ void*                cancellationToken)
{
    struct vali_link_message   msg  = VALI_MSG_INIT_HANDLE(fsStorage->Storage.DriverID);
    oscode_t                 osStatus;
    struct sys_disk_descriptor gdescriptor;
    TRACE("__StorageSetup()");

    usched_mtx_lock(&g_diskLock);
    list_append(&g_disks, &fsStorage->Header);
    usched_mtx_unlock(&g_diskLock);

    ctt_storage_stat(GetGrachtClient(), &msg.base, fsStorage->Storage.DeviceID);
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    ctt_storage_stat_result(GetGrachtClient(), &msg.base, &osStatus, &gdescriptor);
    if (osStatus != OsOK) {
        fsStorage->State = STORAGE_STATE_FAILED;
        return;
    }
    from_sys_disk_descriptor_dkk(&gdescriptor, &fsStorage->Storage);
    
    // Detect the disk layout, and if it fails
    // try to detect which kind of filesystem is present
    osStatus = VfsStorageParse(fsStorage);
    if (osStatus != OsOK) {
        fsStorage->State = STORAGE_STATE_DISCONNECTED;
    } else {
        fsStorage->State = STORAGE_STATE_CONNECTED;
    }
}

static FileSystemStorage_t*
__FileSystemStorageNew(
        _In_ uuid_t       deviceID,
        _In_ uuid_t       driverID,
        _In_ unsigned int flags)
{
    FileSystemStorage_t* fsStorage;

    fsStorage = (FileSystemStorage_t*)malloc(sizeof(FileSystemStorage_t));
    if (!fsStorage) {
        return NULL;
    }
    memset(fsStorage, 0, sizeof(FileSystemStorage_t));

    ELEMENT_INIT(&fsStorage->Header, (uintptr_t)deviceID, fsStorage);
    fsStorage->Storage.DriverID = driverID;
    fsStorage->Storage.DeviceID = deviceID;
    fsStorage->Storage.Flags     = flags;
    fsStorage->State             = STORAGE_STATE_INITIALIZING;
    list_construct(&fsStorage->Filesystems);
    usched_mtx_init(&fsStorage->Lock);
    return fsStorage;
}

void sys_storage_register_invocation(struct gracht_message* message,
                                     const uuid_t driverId, const uuid_t deviceId, const enum sys_storage_flags flags)
{
    FileSystemStorage_t* storage = __FileSystemStorageNew(deviceId, driverId, (unsigned int)flags);
    if (!storage) {
        // TODO
        ERROR("sys_storage_register_invocation FAILED TO CREATE STORAGE STRUCTURE");
        return;
    }
    usched_task_queue((usched_task_fn) __StorageSetup, storage);
}

static void
__StorageUnmount(
        _In_ FileSystemStorage_t* fsStorage,
        _In_ unsigned int         flags)
{
    element_t* i;

    usched_mtx_lock(&fsStorage->Lock);
    _foreach(i, &fsStorage->Filesystems) {
        FileSystem_t* fileSystem = (FileSystem_t*)i->value;
        VfsFileSystemUnmount(fileSystem, flags);
    }
    usched_mtx_unlock(&fsStorage->Lock);
}

static void
__StorageDestroy(
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
        FileSystemStorage_t* fsStorage = header->value;
        __StorageUnmount(fsStorage, request->flags);
        free(fsStorage);
    }
    free(request);
}

void sys_storage_unregister_invocation(struct gracht_message* message, const uuid_t deviceId, const uint8_t forced)
{
    struct VfsRemoveDiskRequest* request = malloc(sizeof(struct VfsRemoveDiskRequest));
    if (!request) {
        ERROR("sys_storage_unregister_invocation FAILED TO UNREGISTER DISK");
        return;
    }

    request->disk_id = deviceId;
    request->flags = forced;
    usched_task_queue((usched_task_fn) __StorageDestroy, request);
}
