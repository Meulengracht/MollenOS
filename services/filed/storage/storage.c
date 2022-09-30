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

#define __TRACE

#include <ctype.h>
#include <ddk/convert.h>
#include <ddk/utils.h>
#include <internal/_ipc.h>
#include <os/usched/job.h>
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

struct VFSStorage*
VFSStorageNew(
        _In_ struct VFSStorageOperations* operations)
{
    struct VFSStorage* storage;

    storage = malloc(sizeof(struct VFSStorage));
    if (!storage) {
        return NULL;
    }

    ELEMENT_INIT(&storage->ListHeader, 0, 0);
    usched_mtx_init(&storage->Lock);
    storage->State = VFSSTORAGE_STATE_INITIALIZING;
    memcpy(&storage->Operations, operations, sizeof(struct VFSStorageOperations));
    storage->Data = NULL;
    list_construct(&storage->Filesystems);
    return storage;
}

static void __DestroyFilesystem(
        _In_ element_t* item,
        _In_ void*      context)
{
    FileSystem_t* fileSystem = (FileSystem_t*)item;
    _CRT_UNUSED(context);
    FileSystemDestroy(fileSystem);
}

void
VFSStorageDelete(
        _In_ struct VFSStorage* storage)
{
    if (storage == NULL) {
        return;
    }

    list_clear(&storage->Filesystems, __DestroyFilesystem, NULL);
    if (storage->Operations.Destroy) {
        storage->Operations.Destroy(storage->Data);
    }
    free(storage);
}

oserr_t
VFSStorageRegisterFileSystem(
        _In_ struct VFSStorage*  storage,
        _In_ int                 partitionIndex,
        _In_ uint64_t            sector,
        _In_ guid_t*             guid,
        _In_ const char*         typeHint,
        _In_ guid_t*             typeGuid,
        _In_ uuid_t              interfaceDriverID,
        _In_ mstring_t*          mountPoint)
{
    FileSystem_t*        fileSystem;
    const char*          fsType = typeHint;
    struct VFSInterface* interface = NULL;
    oserr_t              osStatus;
    uuid_t               id;

    TRACE("VFSStorageRegisterFileSystem(sector=%u, type=%s)",
          LODWORD(sector), typeHint);

    if (fsType == NULL) {
        // try to deduce from type guid
        fsType = FileSystemParseGuid(typeGuid);
    }

    // Get a new filesystem identifier specific to the storage, and then we create
    // the filesystem instance. The storage descriptor, which tells the FS about the
    // storage medium it is on (could be anything), must always be supplied, and should
    // always be known ahead of FS registration.
    id = VFSIdentifierAllocate(storage);
    fileSystem = FileSystemNew(
            storage,
            partitionIndex,
            id,
            guid,
            sector
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
    if (interfaceDriverID == UUID_INVALID) {
        osStatus = VFSInterfaceLoadInternal(typeHint, &interface);
        if (osStatus != OsOK) {
            WARNING("VFSStorageRegisterFileSystem no module for filesystem type %u", fsType);
        }
    } else {
        osStatus = VFSInterfaceLoadDriver(interfaceDriverID, &interface);
        if (osStatus != OsOK) {
            WARNING("VFSStorageRegisterFileSystem no module for filesystem type %u", fsType);
        }
    }

    osStatus = VFSFileSystemConnectInterface(fileSystem, interface);
    if (osStatus != OsOK) {
        // If the interface fails to connect, then the filesystem will go into
        // state NO_INTERFACE. We bail early then as there is no reason to mount the
        // filesystem
        return osStatus;
    }

    osStatus = VFSFileSystemMount(fileSystem, mountPoint);
    if (osStatus != OsOK) {
        return osStatus;
    }
    return OsOK;
}

static oserr_t
__StorageMount(
        _In_ struct VFSStorage* storage)
{
    struct VFS*     fsScope = VFSScopeGet(UUID_INVALID);
    struct VFSNode* deviceNode;
    oserr_t         osStatus;
    mstring_t*      path;
    TRACE("__StorageMount()");

    path = mstr_fmt("/storage/%s", &storage->Storage.Serial[0]);
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
        _In_ struct VFSStorage* storage,
        _In_ void*              cancellationToken)
{
    oserr_t osStatus;
    TRACE("__StorageSetup()");

    usched_mtx_lock(&g_diskLock);
    list_append(&g_disks, &storage->ListHeader);
    usched_mtx_unlock(&g_diskLock);


    // OK now we do some basic verification of the storage medium (again, could be
    // anthing, network, usb, file, harddisk)
    osStatus = __StorageVerify(storage);
    if (osStatus != OsOK) {
        ERROR("__StorageSetup verification of storage medium failed: %u", osStatus);
        goto error;
    }

    // Next thing is mounting the storage device as a folder
    osStatus = __StorageMount(storage);
    if (osStatus != OsOK) {
        ERROR("__StorageSetup mounting storage device failed: %u", osStatus);
        goto error;
    }

    // Detect the disk layout, and if it fails
    // try to detect which kind of filesystem is present
    osStatus = VFSStorageParse(storage);
    if (osStatus != OsOK) {
        goto error;
    }

    storage->State = STORAGE_STATE_CONNECTED;
    return;

error:
    storage->State = STORAGE_STATE_DISCONNECTED;
}

void sys_storage_register_invocation(struct gracht_message* message,
                                     const uuid_t driverId, const uuid_t deviceId, const enum sys_storage_flags flags)
{
    struct VFSStorage* storage = __FileSystemStorageNew(deviceId, driverId, (unsigned int)flags);
    if (!storage) {
        // TODO
        ERROR("sys_storage_register_invocation FAILED TO CREATE STORAGE STRUCTURE");
        return;
    }
    usched_job_queue((usched_task_fn)__StorageSetup, storage);
}

static void
__StorageUnmount(
        _In_ struct VFSStorage* fsStorage,
        _In_ unsigned int       flags)
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
        struct VFSStorage* fsStorage = header->value;
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
    usched_job_queue((usched_task_fn) __StorageDestroy, request);
}
