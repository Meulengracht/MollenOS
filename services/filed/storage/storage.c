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
#include <vfs/scope.h>
#include <vfs/vfs.h>

#include "sys_storage_service_server.h"

struct VfsRemoveDiskRequest {
    uuid_t       disk_id;
    unsigned int flags;
};

static list_t            g_disks = LIST_INIT;
static struct usched_mtx g_diskLock;

void VFSStorageInitialize(void)
{
    usched_mtx_init(&g_diskLock);
}

oserr_t
VFSStorageRegisterFileSystem(
        _In_ FileSystemStorage_t* storage,
        _In_ uint64_t             sector,
        _In_ uint64_t             sectorCount,
        _In_ enum FileSystemType  type,
        _In_ guid_t*              typeGuid,
        _In_ guid_t*              guid)
{
    FileSystem_t*        fileSystem;
    enum FileSystemType  fsType = type;
    struct VFSInterface* interface = NULL;
    oserr_t              osStatus;
    uuid_t               id;

    TRACE("VFSStorageRegisterFileSystem(sector=%u, sectorCount=%u, type=%u)",
          LODWORD(sector), LODWORD(sectorCount), type);

    if (fsType == FileSystemType_UNKNOWN) {
        // try to deduce from type guid
        fsType = FileSystemParseGuid(typeGuid);
    }

    // Get a new filesystem identifier specific to the storage, and then we create
    // the filesystem instance. The storage descriptor, which tells the FS about the
    // storage medium it is on (could be anything), must always be supplied, and should
    // always be known ahead of FS registration.
    id = VFSIdentifierAllocate(storage);
    fileSystem = FileSystemNew(
            &storage->Storage, id, guid,
            sector, sectorCount
    );
    if (!fileSystem) {
        VFSIdentifierFree(storage, id);
        return OsOutOfMemory;
    }

    usched_mtx_lock(&storage->Lock);
    list_append(&storage->Filesystems, &fileSystem->Header);
    usched_mtx_unlock(&storage->Lock);

    // Try to find a module for the filesystem type. If this returns an error
    // it simply means we are not in a sitatuion where we can load the filesystem
    // right now. A module may be present later, so we still register it as disconnected
    osStatus = VFSInterfaceLoadInternal(fsType, &interface);
    if (osStatus != OsOK) {
        WARNING("VFSStorageRegisterFileSystem no module for filesystem type %u", fsType);
    }

    osStatus = VFSFileSystemConnectInterface(fileSystem, interface);
    if (osStatus != OsOK) {
        // If the interface fails to connect, then the filesystem will go into
        // state NO_INTERFACE. We bail early then as there is no reason to mount the
        // filesystem
        return osStatus;
    }

    osStatus = VFSFileSystemMount(fileSystem, NULL);
    if (osStatus != OsOK) {
        return osStatus;
    }
    return OsOK;
}

static oserr_t
__StorageVerify(
        _In_ FileSystemStorage_t* fsStorage)
{
    // TODO implement meaningful validation
    if (fsStorage->Storage.SectorCount == 0) {
        return OsError;
    }
    return OsOK;
}

static oserr_t
__StorageMount(
        _In_ FileSystemStorage_t* fsStorage)
{
    struct VFS*     fsScope = VFSScopeGet(UUID_INVALID);
    struct VFSNode* deviceNode;
    oserr_t         osStatus;
    mstring_t*      path;
    TRACE("__StorageMount()");

    path = mstr_fmt("/storage/%s", &fsStorage->Storage.Serial[0]);
    if (path == NULL) {
        return OsOutOfMemory;
    }
    TRACE("__StorageMount mounting at %ms", path);

    osStatus = VFSNodeNewDirectory(
            fsScope, path,
            FILE_PERMISSION_READ | FILE_PERMISSION_OWNER_WRITE, &deviceNode);
    if (osStatus != OsOK) {
        ERROR("__StorageMount failed to create node %ms", path);
    }

    mstr_delete(path);
    return osStatus;
}

static void
__StorageSetup(
        _In_ FileSystemStorage_t* fsStorage,
        _In_ void*                cancellationToken)
{
    struct vali_link_message   msg  = VALI_MSG_INIT_HANDLE(fsStorage->Storage.DriverID);
    oserr_t                    osStatus;
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

    // OK now we do some basic verification of the storage medium (again, could be
    // anthing, network, usb, file, harddisk)
    osStatus = __StorageVerify(fsStorage);
    if (osStatus != OsOK) {
        ERROR("__StorageSetup verification of storage medium failed: %u", osStatus);
        goto error;
    }

    // Next thing is mounting the storage device as a folder
    osStatus = __StorageMount(fsStorage);
    if (osStatus != OsOK) {
        ERROR("__StorageSetup mounting storage device failed: %u", osStatus);
        goto error;
    }

    // Detect the disk layout, and if it fails
    // try to detect which kind of filesystem is present
    osStatus = VFSStorageParse(fsStorage);
    if (osStatus != OsOK) {
        goto error;
    }

    fsStorage->State = STORAGE_STATE_CONNECTED;
    return;

error:
    fsStorage->State = STORAGE_STATE_DISCONNECTED;
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
        VfsFileSystemDisconnectInterface(fileSystem, flags);
        FileSystemDestroy(fileSystem);
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
