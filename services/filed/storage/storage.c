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

static void __StorageDestroy(struct VFSStorage* storage, unsigned int flags);

static list_t            g_disks = LIST_INIT;
static struct usched_mtx g_diskLock;

void VFSStorageStartup(void)
{
    usched_mtx_init(&g_diskLock, USCHED_MUTEX_PLAIN);
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

    // The list element value always points back to the storage. The key is not
    // used. Device-backed storage is found by its driver ID and device ID, see
    // __FindRegisteredDisk.
    ELEMENT_INIT(&storage->ListHeader, 0, storage);
    usched_mtx_init(&storage->Lock, USCHED_MUTEX_PLAIN);
    storage->State = VFSSTORAGE_STATE_INITIALIZING;
    memcpy(&storage->Operations, operations, sizeof(struct VFSStorageOperations));
    list_construct(&storage->Filesystems);
    return storage;
}

void
VFSStorageDelete(
        _In_ struct VFSStorage* storage)
{
    element_t* item;

    if (storage == NULL) {
        return;
    }

    // Take the filesystems off the list one at a time and destroy them after
    // the list lock is released. list_clear would call FileSystemDestroy while
    // holding the list spinlock, and FileSystemDestroy can wait (VFSDestroy
    // closes open handles and may yield).
    for (;;) {
        item = list_front(&storage->Filesystems);
        if (item == NULL) {
            break;
        }

        if (list_remove(&storage->Filesystems, item) != 0) {
            ERROR("VFSStorageDelete failed to remove filesystem from storage");
            break;
        }
        FileSystemDestroy((FileSystem_t*)item->value);
    }

    mstr_delete(storage->DirectoryPath);
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
        return OS_EOOM;
    }

    *fileSystemOut = fileSystem;

    usched_mtx_lock(&storage->Lock);
    list_append(&storage->Filesystems, &fileSystem->Header);
    usched_mtx_unlock(&storage->Lock);
    return OS_EOK;
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
    if (oserr != OS_EOK) {
        return oserr;
    }

    // Try to find a module for the filesystem type. If this returns an error
    // it simply means we are not in a sitatuion where we can load the filesystem
    // right now. A module may be present later, so we still register it as disconnected
    if (interfaceDriverID == UUID_INVALID) {
        oserr = VFSInterfaceLoadInternal(fsType, &interface);
        if (oserr != OS_EOK) {
            WARNING("VFSStorageRegisterFileSystem no module for filesystem type %s", fsType);
        }
    } else {
        oserr = VFSInterfaceLoadDriver(interfaceDriverID, &interface);
        if (oserr != OS_EOK) {
            WARNING("VFSStorageRegisterFileSystem no module for filesystem type %s", fsType);
        }
    }

    oserr = VFSFileSystemConnectInterface(fileSystem, interface);
    if (oserr != OS_EOK) {
        // If the interface fails to connect, then the filesystem will go into
        // state NO_INTERFACE. We bail early then as there is no reason to mount the
        // filesystem
        return oserr;
    }

    oserr = VFSFileSystemMount(fileSystem, mountPoint);
    if (oserr != OS_EOK) {
        return oserr;
    }
    return OS_EOK;
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
        return OS_EOOM;
    }
    TRACE("__StorageMount mounting at %ms", path);

    // The path is kept in the storage so __StorageRemoveDirectory can remove
    // the directory again when the storage is unregistered. A directory that
    // already exists (OS_EEXISTS) was not created here and is not kept.
    osStatus = VFSNodeNewDirectory(
            fsScope, path,
            FILE_PERMISSION_READ | FILE_PERMISSION_OWNER_WRITE,
            &deviceNode
    );
    if (osStatus != OS_EOK) {
        ERROR("__StorageMount failed to create node %ms: %u", path, osStatus);
        mstr_delete(path);
        return osStatus;
    }

    storage->DirectoryPath = path;
    return OS_EOK;
}

// Removes the directory /storage/<Serial> that __StorageMount created. The
// filesystem mount directories below it must already be removed (see
// VFSFileSystemUnmount), as VFSNodeUnlink refuses a directory with children.
static void
__StorageRemoveDirectory(
        _In_ struct VFSStorage* storage)
{
    char*   path;
    oserr_t oserr;

    if (storage->DirectoryPath == NULL) {
        return;
    }

    path = mstr_u8(storage->DirectoryPath);
    if (path == NULL) {
        ERROR("__StorageRemoveDirectory failed to convert path %ms", storage->DirectoryPath);
        return;
    }

    oserr = VFSNodeUnlink(VFSScopeGet(UUID_INVALID), path);
    if (oserr != OS_EOK) {
        ERROR("__StorageRemoveDirectory failed to remove %s: %u", path, oserr);
    }
    free(path);
}

// Finds the disk that a driver registered. Each driver numbers its own disks,
// so the device ID alone is not unique, and the lookup always uses the driver
// ID too. A storage with UnregisterPending set is skipped: its driver has
// already unregistered it, and registration only has to finish tearing it
// down. g_diskLock must be held.
static struct VFSStorage*
__FindRegisteredDisk(
        _In_ uuid_t driverId,
        _In_ uuid_t deviceId)
{
    element_t* i;

    _foreach(i, &g_disks) {
        struct VFSStorage* storage = i->value;
        if (storage->Protocol.Storage.Device.DriverID == driverId &&
            storage->Protocol.Storage.Device.DeviceID == deviceId &&
            !storage->UnregisterPending) {
            return storage;
        }
    }
    return NULL;
}

// Sends the result of a register request. The driver may not be waiting for
// it, so a failed send is only logged.
static void
__SendRegisterResult(
        _In_ struct gracht_message* message,
        _In_ uuid_t                 driverId,
        _In_ uuid_t                 deviceId,
        _In_ oserr_t                result)
{
    int status;

    status = sys_storage_register_response(message, result);
    if (status != 0) {
        ERROR("sys_storage_register_invocation failed to send result %u to driver %u for device %u: %i",
              result, driverId, deviceId, status);
    }
}

// Sends the result of an unregister request. The driver may not be waiting for
// it, so a failed send is only logged.
static void
__SendUnregisterResult(
        _In_ struct gracht_message* message,
        _In_ uuid_t                 driverId,
        _In_ uuid_t                 deviceId,
        _In_ oserr_t                result)
{
    int status;

    status = sys_storage_unregister_response(message, result);
    if (status != 0) {
        ERROR("sys_storage_unregister_invocation failed to send result %u to driver %u for device %u: %i",
              result, driverId, deviceId, status);
    }
}

// Adds a disk and sets it up. The steps are:
// 1. Check the arguments and create the storage object.
// 2. List the disk, unless the driver already registered the same device ID.
// 3. Send the result to the driver.
// 4. Query the disk, create /storage/<Serial> and parse the partitions.
// Every return before step 3 sends its own result, so each request gets
// exactly one answer. The result only says the disk was accepted. A failure
// in step 4 is logged here and never reaches the driver.
void sys_storage_register_invocation(struct gracht_message* message,
                                     const uuid_t driverId, const uuid_t deviceId, const enum sys_storage_flags flags)
{
    struct VFSStorage* storage;
    bool               unregisterPending;
    oserr_t            oserr;
    TRACE("sys_storage_register_invocation(driver=%u, device=%u)", driverId, deviceId);

    // The driver ID is the IPC handle filed sends its stat and transfer
    // requests to, so it must be a valid handle.
    if (driverId == UUID_INVALID) {
        ERROR("sys_storage_register_invocation invalid driver ID for device %u", deviceId);
        __SendRegisterResult(message, driverId, deviceId, OS_EINVALPARAMS);
        return;
    }

    // VFSStorageCreateDeviceBacked only fails when it cannot allocate memory.
    storage = VFSStorageCreateDeviceBacked(deviceId, driverId, flags);
    if (storage == NULL) {
        ERROR("sys_storage_register_invocation failed to create storage for driver %u device %u",
              driverId, deviceId);
        __SendRegisterResult(message, driverId, deviceId, OS_EOOM);
        return;
    }

    // The duplicate check and the append must happen under the same lock, or
    // two register requests for the same disk could both be listed.
    usched_mtx_lock(&g_diskLock);
    if (__FindRegisteredDisk(driverId, deviceId) != NULL) {
        usched_mtx_unlock(&g_diskLock);
        WARNING("sys_storage_register_invocation driver %u device %u is already registered",
                driverId, deviceId);
        VFSStorageDelete(storage);
        __SendRegisterResult(message, driverId, deviceId, OS_EEXISTS);
        return;
    }
    
    list_append(&g_disks, &storage->ListHeader);
    usched_mtx_unlock(&g_diskLock);

    // Answer before the stat query. A driver may hold its own device lock
    // while it registers a disk (virtio-blk does), and its stat handler needs
    // the same lock. If filed answered only after the stat query, a driver
    // that waits for this result would wait for filed while filed waits for
    // the stat answer.
    __SendRegisterResult(message, driverId, deviceId, OS_EOK);

    // The stat query, mounting and parsing wait for IPC, so an unregister for
    // this device can run in between. The storage is listed above, before the
    // first wait, so the unregister always finds it. While State is
    // VFSSTORAGE_STATE_INITIALIZING, unregister only marks the storage, and
    // the end of this function tears it down.

    // A failed stat query leaves nothing behind. Nothing else is set up yet,
    // so taking the storage off the list and deleting it is enough. An
    // unregister may already have marked the storage. The driver got OS_EOK
    // for that unregister, and the storage is deleted here as well.
    oserr = VFSStorageQueryDeviceStats(storage);
    if (oserr != OS_EOK) {
        ERROR("sys_storage_register_invocation failed to query driver %u device %u: %u",
              driverId, deviceId, oserr);
        usched_mtx_lock(&g_diskLock);
        if (list_remove(&g_disks, &storage->ListHeader) != 0) {
            ERROR("sys_storage_register_invocation storage for driver %u device %u was not in the disk list",
                  driverId, deviceId);
        }
        usched_mtx_unlock(&g_diskLock);
        VFSStorageDelete(storage);
        return;
    }

    // An unregister that arrived during the stat query means the disk is
    // gone. Skip mounting and parsing, which would only send requests to a
    // removed device, and tear the storage down at the end.
    usched_mtx_lock(&g_diskLock);
    unregisterPending = storage->UnregisterPending;
    usched_mtx_unlock(&g_diskLock);
    if (unregisterPending) {
        goto exit;
    }

    // Next thing is mounting the storage device as a folder
    oserr = __StorageMount(storage);
    if (oserr != OS_EOK) {
        ERROR("__StorageSetup mounting storage device failed: %u", oserr);
        goto exit;
    }

    // Detect the disk layout, and if it fails
    // try to detect which kind of filesystem is present
    oserr = VFSStorageParse(storage);
    if (oserr != OS_EOK) {
        ERROR("sys_storage_register_invocation failed to parse storage for driver %u device %u: %u",
              driverId, deviceId, oserr);
    }

exit:
    // The pending check and the state change must happen under the same lock
    // as unregister's state check, or an unregister could be lost in between.
    usched_mtx_lock(&g_diskLock);
    if (storage->UnregisterPending) {
        if (list_remove(&g_disks, &storage->ListHeader) != 0) {
            ERROR("sys_storage_register_invocation storage for driver %u device %u was not in the disk list",
                  driverId, deviceId);
        }
        usched_mtx_unlock(&g_diskLock);

        TRACE("sys_storage_register_invocation driver %u device %u was unregistered during registration",
              driverId, deviceId);
        __StorageDestroy(storage, storage->UnregisterFlags);
        return;
    }

    // A storage that failed to mount or parse stays in the list as
    // disconnected, so its unregister still finds it and removes what was set up.
    storage->State = oserr == OS_EOK ? VFSSTORAGE_STATE_CONNECTED : VFSSTORAGE_STATE_DISCONNECTED;
    usched_mtx_unlock(&g_diskLock);
}

// Unmounts each filesystem on the storage and disconnects its interface. The
// filesystems stay in the storage's list. VFSStorageDelete destroys them.
static void
__StorageUnmount(
        _In_ struct VFSStorage* fsStorage,
        _In_ unsigned int       flags)
{
    element_t* i;
    oserr_t    oserr;

    usched_mtx_lock(&fsStorage->Lock);
    _foreach(i, &fsStorage->Filesystems) {
        FileSystem_t* fileSystem = (FileSystem_t*)i->value;

        // OS_ENOTSUPPORTED means the filesystem was not mounted.
        oserr = VFSFileSystemUnmount(fileSystem, flags);
        if (oserr != OS_EOK && oserr != OS_ENOTSUPPORTED) {
            WARNING("__StorageUnmount failed to unmount filesystem %u: %u", fileSystem->ID, oserr);
        }

        // OS_EBUSY means the filesystem had no connected interface.
        oserr = VFSFileSystemDisconnectInterface(fileSystem, flags);
        if (oserr != OS_EOK && oserr != OS_EBUSY) {
            WARNING("__StorageUnmount failed to disconnect filesystem %u: %u", fileSystem->ID, oserr);
        }
    }
    usched_mtx_unlock(&fsStorage->Lock);
}

// Tears down a storage that is no longer in g_disks. The filesystems are
// unmounted (which removes their mount directories) and disconnected first.
// Then the device directory can be removed, as it is empty, and
// VFSStorageDelete destroys the filesystems and frees the storage.
static void
__StorageDestroy(
        _In_ struct VFSStorage* storage,
        _In_ unsigned int       flags)
{
    __StorageUnmount(storage, flags);
    __StorageRemoveDirectory(storage);
    VFSStorageDelete(storage);
}

// Removes a disk. The disk is taken off the list and the result is sent
// before the teardown starts. A disk that is still registering is only marked,
// and registration tears it down when it finishes. Every path sends exactly
// one result.
void sys_storage_unregister_invocation(struct gracht_message* message,
                                       const uuid_t driverId, const uuid_t deviceId, const uint8_t forced)
{
    struct VFSStorage* fsStorage;
    TRACE("sys_storage_unregister_invocation(driver=%u, device=%u)", driverId, deviceId);

    usched_mtx_lock(&g_diskLock);
    fsStorage = __FindRegisteredDisk(driverId, deviceId);
    if (fsStorage == NULL) {
        usched_mtx_unlock(&g_diskLock);
        WARNING("sys_storage_unregister_invocation no storage registered for driver %u device %u",
                driverId, deviceId);
        __SendUnregisterResult(message, driverId, deviceId, OS_ENOENT);
        return;
    }

    // Registration is still mounting or parsing this storage and uses it until
    // it finishes. Leave the storage in place and let registration tear it down.
    // The driver can treat the disk as removed, so the result is OS_EOK.
    if (fsStorage->State == VFSSTORAGE_STATE_INITIALIZING) {
        fsStorage->UnregisterPending = true;
        fsStorage->UnregisterFlags = forced;
        usched_mtx_unlock(&g_diskLock);
        TRACE("sys_storage_unregister_invocation driver %u device %u is still registering, teardown deferred",
              driverId, deviceId);
        __SendUnregisterResult(message, driverId, deviceId, OS_EOK);
        return;
    }

    // The storage was found under the same lock, so it is in the list. A
    // failure here means the list is damaged.
    if (list_remove(&g_disks, &fsStorage->ListHeader) != 0) {
        usched_mtx_unlock(&g_diskLock);
        ERROR("sys_storage_unregister_invocation failed to remove storage for driver %u device %u",
              driverId, deviceId);
        __SendUnregisterResult(message, driverId, deviceId, OS_EUNKNOWN);
        return;
    }
    usched_mtx_unlock(&g_diskLock);

    // Answer before the unmount. Unmounting waits for the filesystem modules,
    // which can send requests to the storage driver. A driver that holds its
    // own device lock while it waits for this result would block those
    // requests, and the unmount would never finish.
    __SendUnregisterResult(message, driverId, deviceId, OS_EOK);
    __StorageDestroy(fsStorage, forced);
}
