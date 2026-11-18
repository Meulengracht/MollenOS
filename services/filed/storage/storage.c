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

    // The list element value always points back to the storage. The key is set
    // by the code that inserts the storage into a list (see
    // sys_storage_register_invocation for device-backed storage).
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

void sys_storage_register_invocation(struct gracht_message* message,
                                     const uuid_t driverId, const uuid_t deviceId, const enum sys_storage_flags flags)
{
    struct VFSStorage* storage;
    bool               unregisterPending;
    oserr_t            oserr;
    TRACE("sys_storage_register_invocation()");

    storage = VFSStorageCreateDeviceBacked(deviceId, driverId, flags);
    if (storage == NULL) {
        ERROR("sys_storage_register_invocation failed to create storage for device %u", deviceId);
        return;
    }

    // sys_storage_unregister_invocation looks up the disk by device ID only, so
    // the list key must be the device ID.
    ELEMENT_INIT(&storage->ListHeader, (uintptr_t)deviceId, storage);
    usched_mtx_lock(&g_diskLock);
    list_append(&g_disks, &storage->ListHeader);
    usched_mtx_unlock(&g_diskLock);

    // The stat query, mounting and parsing wait for IPC, so an unregister for
    // this device can run in between. The storage is listed above, before the
    // first wait, so the unregister always finds it. While State is
    // VFSSTORAGE_STATE_INITIALIZING, unregister only marks the storage, and
    // the end of this function tears it down.

    // A failed stat query leaves nothing behind. Nothing else is set up yet,
    // so taking the storage off the list and deleting it is enough.
    oserr = VFSStorageQueryDeviceStats(storage);
    if (oserr != OS_EOK) {
        ERROR("sys_storage_register_invocation failed to query device %u: %u", deviceId, oserr);
        usched_mtx_lock(&g_diskLock);
        if (list_remove(&g_disks, &storage->ListHeader) != 0) {
            ERROR("sys_storage_register_invocation storage for device %u was not in the disk list", deviceId);
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
        ERROR("sys_storage_register_invocation failed to parse storage for device %u: %u", deviceId, oserr);
    }

exit:
    // The pending check and the state change must happen under the same lock
    // as unregister's state check, or an unregister could be lost in between.
    usched_mtx_lock(&g_diskLock);
    if (storage->UnregisterPending) {
        if (list_remove(&g_disks, &storage->ListHeader) != 0) {
            ERROR("sys_storage_register_invocation storage for device %u was not in the disk list", deviceId);
        }
        usched_mtx_unlock(&g_diskLock);

        TRACE("sys_storage_register_invocation device %u was unregistered during registration", deviceId);
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

void sys_storage_unregister_invocation(struct gracht_message* message, const uuid_t deviceId, const uint8_t forced)
{
    element_t*         header;
    struct VFSStorage* fsStorage;
    TRACE("sys_storage_unregister_invocation(device=%u)", deviceId);

    usched_mtx_lock(&g_diskLock);
    header = list_find(&g_disks, (void*)(uintptr_t)deviceId);
    if (header == NULL) {
        usched_mtx_unlock(&g_diskLock);
        WARNING("sys_storage_unregister_invocation no storage registered for device %u", deviceId);
        return;
    }

    // Registration is still mounting or parsing this storage and uses it until
    // it finishes. Leave the storage in place and let registration tear it down.
    fsStorage = header->value;
    if (fsStorage->State == VFSSTORAGE_STATE_INITIALIZING) {
        fsStorage->UnregisterPending = true;
        fsStorage->UnregisterFlags = forced;
        usched_mtx_unlock(&g_diskLock);
        TRACE("sys_storage_unregister_invocation device %u is still registering, teardown deferred", deviceId);
        return;
    }

    if (list_remove(&g_disks, header) != 0) {
        usched_mtx_unlock(&g_diskLock);
        ERROR("sys_storage_unregister_invocation failed to remove storage for device %u", deviceId);
        return;
    }
    usched_mtx_unlock(&g_diskLock);

    __StorageDestroy(fsStorage, forced);
}
