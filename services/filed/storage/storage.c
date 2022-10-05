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
#include <os/usched/job.h>
#include <vfs/storage.h>
#include <vfs/filesystem.h>
#include <vfs/interface.h>
#include <vfs/scope.h>
#include <vfs/vfs.h>

#include <sys_storage_service_server.h>

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
        _In_ struct VFSStorageOperations* operations,
        _In_ unsigned int                 flags)
{
    struct VFSStorage* storage;

    storage = malloc(sizeof(struct VFSStorage));
    if (!storage) {
        return NULL;
    }
    memset(storage, 0, sizeof(struct VFSStorage));

    ELEMENT_INIT(&storage->ListHeader, 0, 0);
    usched_mtx_init(&storage->Lock);
    storage->State = VFSSTORAGE_STATE_INITIALIZING;
    memcpy(&storage->Operations, operations, sizeof(struct VFSStorageOperations));
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
    free(storage);
}

oserr_t
VFSStorageRegisterPartition(
        _In_  struct VFSStorage*  storage,
        _In_  int                 partitionIndex,
        _In_  UInteger64_t*       sector,
        _In_  guid_t*             guid,
        _Out_ struct FileSystem** fileSystemOut)
{
    FileSystem_t* fileSystem;
    uuid_t        id;
    TRACE("VFSStorageRegisterFileSystem(sector=%u)", sector->u.LowPart);

    // Get a new filesystem identifier specific to the storage, and then we create
    // the filesystem instance. The storage descriptor, which tells the FS about the
    // storage medium it is on (could be anything), must always be supplied, and should
    // always be known ahead of FS registration.
    id = VFSIdentifierAllocate(storage);
    fileSystem = FileSystemNew(
            storage,
            partitionIndex,
            sector,
            id,
            guid
    );
    if (!fileSystem) {
        VFSIdentifierFree(storage, id);
        return OsOutOfMemory;
    }

    *fileSystemOut = fileSystem;

    usched_mtx_lock(&storage->Lock);
    list_append(&storage->Filesystems, &fileSystem->Header);
    usched_mtx_unlock(&storage->Lock);
    return OsOK;
}

oserr_t
VFSStorageRegisterAndSetupPartition(
        _In_ struct VFSStorage*  storage,
        _In_ int                 partitionIndex,
        _In_ UInteger64_t*       sector,
        _In_ guid_t*             guid,
        _In_ const char*         typeHint,
        _In_ guid_t*             typeGuid,
        _In_ uuid_t              interfaceDriverID,
        _In_ mstring_t*          mountPoint)
{
    struct FileSystem*   fileSystem;
    const char*          fsType = typeHint;
    struct VFSInterface* interface = NULL;
    oserr_t              oserr;

    TRACE("VFSStorageRegisterFileSystem(sector=%u, type=%s)",
          sector->u.LowPart, typeHint);

    if (fsType == NULL) {
        // try to deduce from type guid
        fsType = FileSystemParseGuid(typeGuid);
    }

    oserr = VFSStorageRegisterPartition(
        storage, partitionIndex, sector,
        guid, &fileSystem
    );
    if (oserr != OsOK) {
        return oserr;
    }

    // Try to find a module for the filesystem type. If this returns an error
    // it simply means we are not in a sitatuion where we can load the filesystem
    // right now. A module may be present later, so we still register it as disconnected
    if (interfaceDriverID == UUID_INVALID) {
        oserr = VFSInterfaceLoadInternal(fsType, &interface);
        if (oserr != OsOK) {
            WARNING("VFSStorageRegisterFileSystem no module for filesystem type %s", fsType);
        }
    } else {
        oserr = VFSInterfaceLoadDriver(interfaceDriverID, &interface);
        if (oserr != OsOK) {
            WARNING("VFSStorageRegisterFileSystem no module for filesystem type %s", fsType);
        }
    }

    oserr = VFSFileSystemConnectInterface(fileSystem, interface);
    if (oserr != OsOK) {
        // If the interface fails to connect, then the filesystem will go into
        // state NO_INTERFACE. We bail early then as there is no reason to mount the
        // filesystem
        return oserr;
    }

    oserr = VFSFileSystemMount(fileSystem, mountPoint);
    if (oserr != OsOK) {
        return oserr;
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

    path = mstr_fmt("/storage/%s", &storage->Stats.Serial[0]);
    if (path == NULL) {
        return OsOutOfMemory;
    }
    TRACE("__StorageMount mounting at %ms", path);

    // TODO we should probably be using VFSNodeMkdir instead and keep a handle
    // on the directory for the lifetime of the storage.
    osStatus = VFSNodeNewDirectory(
            fsScope, path,
            FILE_PERMISSION_READ | FILE_PERMISSION_OWNER_WRITE,
            &deviceNode
    );
    if (osStatus != OsOK) {
        ERROR("__StorageMount failed to create node %ms", path);
    }

    mstr_delete(path);
    return osStatus;
}

struct __StorageSetupParameters {
    uuid_t                 DriverId;
    uuid_t                 DeviceId;
    enum sys_storage_flags Flags;
};

static struct __StorageSetupParameters*
__StorageSetupParametersNew(
        _In_ uuid_t                 driverId,
        _In_ uuid_t                 deviceId,
        _In_ enum sys_storage_flags flags)
{
    struct __StorageSetupParameters* setupParameters;

    setupParameters = malloc(sizeof(struct __StorageSetupParameters));
    if (setupParameters == NULL) {
        return NULL;
    }

    setupParameters->DriverId = driverId;
    setupParameters->DeviceId = deviceId;
    setupParameters->Flags = flags;
    return setupParameters;
}

static void
__StorageSetupParametersDelete(
        struct __StorageSetupParameters* setupParameters)
{
    free(setupParameters);
}

static void
__StorageSetup(
        _In_ struct __StorageSetupParameters* setupParameters,
        _In_ void*                            cancellationToken)
{
    struct VFSStorage* storage;
    oserr_t            oserr;
    _CRT_UNUSED(cancellationToken);
    TRACE("__StorageSetup()");

    storage = VFSStorageCreateDeviceBacked(
            setupParameters->DeviceId,
            setupParameters->DriverId,
            setupParameters->Flags
    );
    __StorageSetupParametersDelete(setupParameters);
    if (storage == NULL) {
        return;
    }

    usched_mtx_lock(&g_diskLock);
    list_append(&g_disks, &storage->ListHeader);
    usched_mtx_unlock(&g_diskLock);

    // Next thing is mounting the storage device as a folder
    oserr = __StorageMount(storage);
    if (oserr != OsOK) {
        ERROR("__StorageSetup mounting storage device failed: %u", oserr);
        goto error;
    }

    // Detect the disk layout, and if it fails
    // try to detect which kind of filesystem is present
    oserr = VFSStorageParse(storage);
    if (oserr != OsOK) {
        goto error;
    }

    storage->State = VFSSTORAGE_STATE_CONNECTED;
    return;
error:
    storage->State = VFSSTORAGE_STATE_DISCONNECTED;
}

void sys_storage_register_invocation(struct gracht_message* message,
                                     const uuid_t driverId, const uuid_t deviceId, const enum sys_storage_flags flags)
{
    struct __StorageSetupParameters* setupParameters;
    _CRT_UNUSED(message);

    setupParameters = __StorageSetupParametersNew(driverId, deviceId, (unsigned int)flags);
    if (setupParameters == NULL) {
        ERROR("sys_storage_register_invocation failed to allocate memory");
        return;
    }
    usched_job_queue((usched_task_fn)__StorageSetup, setupParameters);
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
        VFSFileSystemUnmount(fileSystem, flags);
        VFSFileSystemDisconnectInterface(fileSystem, flags);
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
    _CRT_UNUSED(cancellationToken);

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
    struct VfsRemoveDiskRequest* request;
    _CRT_UNUSED(message);

    request = malloc(sizeof(struct VfsRemoveDiskRequest));
    if (!request) {
        ERROR("sys_storage_unregister_invocation FAILED TO UNREGISTER DISK");
        return;
    }

    request->disk_id = deviceId;
    request->flags = forced;
    usched_job_queue((usched_task_fn) __StorageDestroy, request);
}
