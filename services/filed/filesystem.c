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
 *
 */

#define __TRACE

#include <assert.h>
#include <ddk/utils.h>
#include <os/types/file.h>
#include <stdlib.h>
#include <string.h>
#include <vfs/filesystem.h>
#include <vfs/interface.h>
#include <vfs/scope.h>
#include <vfs/storage.h>
#include <vfs/vfs.h>

#include <sys_file_service_server.h>

extern gracht_server_t* __crt_get_service_server(void);

struct default_mounts {
    const char*  label;
    const char*  path;
    unsigned int flags;
};

static guid_t g_efiGuid = GUID_EMPTY;
static guid_t g_fatGuid = GUID_EMPTY;
static guid_t g_mfsSystemGuid = GUID_EMPTY;
static guid_t g_mfsUserDataGuid = GUID_EMPTY;
static guid_t g_mfsUserGuid = GUID_EMPTY;
static guid_t g_mfsDataGuid = GUID_EMPTY;

#define MOUNT_READONLY 0

static struct default_mounts g_defaultMounts[] = {
        { "vali-efi",  "/efi",  MOUNT_READONLY },
        { "vali-boot", "/boot", MOUNT_READONLY },
        { "vali-data", "/data",     0 },
        { NULL, NULL, 0 },
};
static struct usched_mtx g_mountsLock;

static void __StorageReadyEvent(mstring_t* mountPoint)
{
    char* mp = mstr_u8(mountPoint);
    sys_file_event_storage_ready_all(__crt_get_service_server(), mp);
    free(mp);
}

const char*
FileSystemParseGuid(
        _In_ guid_t* guid)
{
    if (!guid_cmp(guid, &g_efiGuid) || !guid_cmp(guid, &g_fatGuid)) {
        return "fat";
    }
    if (!guid_cmp(guid, &g_mfsSystemGuid) || !guid_cmp(guid, &g_mfsUserDataGuid) ||
        !guid_cmp(guid, &g_mfsUserGuid) || !guid_cmp(guid, &g_mfsDataGuid)) {
        return "mfs";
    }
    return NULL;
}

void VFSFileSystemStartup(void)
{
    TRACE("VFSFileSystemStartup()");

    guid_parse_string(&g_efiGuid, "C12A7328-F81F-11D2-BA4B-00A0C93EC93B");
    guid_parse_string(&g_fatGuid, "21686148-6449-6E6F-744E-656564454649");
    guid_parse_string(&g_mfsSystemGuid, "C4483A10-E3A0-4D3F-B7CC-C04A6E16612B");
    guid_parse_string(&g_mfsUserDataGuid, "80C6C62A-B0D6-4FF4-A69D-558AB6FD8B53");
    guid_parse_string(&g_mfsUserGuid, "8874F880-E7AD-4EE2-839E-6FFA54F19A72");
    guid_parse_string(&g_mfsDataGuid, "B8E1A523-5865-4651-9548-8A43A9C21384");
    usched_mtx_init(&g_mountsLock, USCHED_MUTEX_PLAIN);
}

FileSystem_t*
FileSystemNew(
        _In_ struct VFSStorage* storage,
        _In_ int                partitionIndex,
        _In_ UInteger64_t*      sector,
        _In_ uuid_t             id,
        _In_ guid_t*            guid)
{
    FileSystem_t* fileSystem;
    TRACE("FileSystemNew(storage=%u, partition=%i, sector=%llu)",
          storage->ID, partitionIndex, sector->QuadPart);

    fileSystem = (FileSystem_t*)malloc(sizeof(FileSystem_t));
    if (!fileSystem) {
        return NULL;
    }
    memset(fileSystem, 0, sizeof(FileSystem_t));

    ELEMENT_INIT(&fileSystem->Header, (uintptr_t)storage->ID, fileSystem);
    fileSystem->ID                     = id;
    fileSystem->Storage                = storage;
    fileSystem->State                  = FileSystemState_NO_INTERFACE;
    fileSystem->PartitionIndex         = partitionIndex;
    fileSystem->Interface              = NULL;
    fileSystem->SectorStart.QuadPart   = sector->QuadPart;
    memcpy(&fileSystem->GUID, guid, sizeof(guid_t));
    usched_mtx_init(&fileSystem->Lock, USCHED_MUTEX_PLAIN);
    return fileSystem;
}

static void __DestroyDriver(
        _In_ FileSystem_t* fileSystem)
{
    if (fileSystem->State != FileSystemState_CONNECTED) {
        return;
    }

    if (fileSystem->Interface && fileSystem->Interface->Operations.Destroy) {
        fileSystem->Interface->Operations.Destroy(fileSystem->Interface, fileSystem->Data, 0);
    }
    VFSInterfaceDelete(fileSystem->Interface);
}

void
FileSystemDestroy(
        _In_ FileSystem_t* fileSystem)
{
    TRACE("FileSystemDestroy(storage=%u, sector=%llu)",
          fileSystem->Storage->ID,
          fileSystem->SectorStart.QuadPart);

    // Unmount and destroy all resources related to the VFS
    VFSDestroy(fileSystem->VFS);

    // Next is destroying the actual driver and the underlying
    // resources which are bound to this.
    __DestroyDriver(fileSystem);

    // Return the identifier VFSStorageRegisterPartition allocated for this
    // filesystem, so a disk that is registered again gets the same ones.
    VFSIdentifierFree(fileSystem->Storage, fileSystem->ID);

    // Cleanup memory allocated by this subsystem
    mstr_delete(fileSystem->MountPath);
    mstr_delete(fileSystem->BindPath);
    free(fileSystem);
}

static mstring_t* __GetLabel(
        _In_ FileSystem_t* fileSystem)
{
    struct VFSStatFS stat = { 0 };

    if (fileSystem->Interface && fileSystem->Interface->Operations.Stat) {
        oserr_t oserr = fileSystem->Interface->Operations.Stat(fileSystem->Interface, fileSystem->Data, &stat);
        if (oserr == OS_EOK && stat.Label) {
            return mstr_clone(stat.Label);
        }
    }

    // Otherwise no driver was available, or the filesystem is weird, thus
    // we must make up our own label
    // syntax: p{index}
    return mstr_fmt("p%i", fileSystem->PartitionIndex);
}

// Closes the handle on a default mount directory and then removes the
// directory, so the next mount of the same disk can create it again.
// VFSNodeUnlink refuses a directory with open handles, so the close goes first.
static void
__RemoveMountDirectory(
        _In_ struct VFS* fsScope,
        _In_ uuid_t      directoryHandle,
        _In_ mstring_t*  path)
{
    char*   cpath;
    oserr_t oserr;

    oserr = VFSNodeClose(fsScope, directoryHandle);
    if (oserr != OS_EOK) {
        ERROR("__RemoveMountDirectory failed to close %ms: %u", path, oserr);
    }

    cpath = mstr_u8(path);
    if (cpath == NULL) {
        ERROR("__RemoveMountDirectory failed to convert path %ms", path);
        return;
    }

    oserr = VFSNodeUnlink(fsScope, cpath);
    if (oserr != OS_EOK) {
        ERROR("__RemoveMountDirectory failed to remove %ms: %u", path, oserr);
    }
    free(cpath);
}

static oserr_t
__MountFileSystemAtDefault(
        _In_ FileSystem_t* fileSystem)
{
    struct VFS* fsScope = VFSScopeGet(UUID_INVALID);
    uuid_t      nodeHandle;
    oserr_t     osStatus;
    mstring_t*  path;
    mstring_t*  label;
    TRACE("__MountFileSystemAtDefault(fs=%u)", fileSystem->ID);

    label = __GetLabel(fileSystem);
    if (label == NULL) {
        return OS_EOOM;
    }

    path = mstr_fmt("/storage/%s/%ms", &fileSystem->Storage->Stats.Serial[0], label);
    mstr_delete(label);
    if (path == NULL) {
        return OS_EOOM;
    }

    // VFSNodeMkdir gives no handle for a directory that already exists
    // (OS_EEXISTS), so that is a failure here too.
    osStatus = VFSNodeMkdir(
        fsScope,
        path,
        FILE_PERMISSION_READ,
        &nodeHandle
    );
    if (osStatus != OS_EOK) {
        ERROR("__MountFileSystemAtDefault failed to create node %ms: %u", path, osStatus);
        mstr_delete(path);
        return osStatus;
    }

    osStatus = VFSNodeMount(fsScope, nodeHandle, fileSystem->VFS);
    if (osStatus != OS_EOK) {
        ERROR("__MountFileSystemAtDefault failed to mount filesystem at %ms", path);
        __RemoveMountDirectory(fsScope, nodeHandle, path);
        mstr_delete(path);
        return osStatus;
    }

    // VFSFileSystemUnmount undoes this mount with these, in the same scope.
    fileSystem->MountHandle = nodeHandle;
    fileSystem->MountScope  = fsScope;
    fileSystem->MountPath   = path;
    return OS_EOK;
}

// Closes the handle on a bind target directory. A directory that was created
// for the bind is removed as well, one that existed before is left in place.
static void
__CloseBindTarget(
    _In_ struct VFS* fsScope,
    _In_ uuid_t      directoryHandle,
    _In_ mstring_t*  path,
    _In_ bool        createdDirectory)
{
    oserr_t oserr;

    if (createdDirectory) {
        __RemoveMountDirectory(fsScope, directoryHandle, path);
        return;
    }

    oserr = VFSNodeClose(fsScope, directoryHandle);
    if (oserr != OS_EOK) {
        ERROR("__CloseBindTarget failed to close %ms: %u", path, oserr);
    }
}

// Bind mounts the default mount directory of the filesystem at the given path,
// in the scope of the default mount. The target directory is created when it
// does not exist. VFSNodeMkdir gives no handle for a directory that already
// exists (OS_EEXISTS), so that directory is opened instead. VFSNodeOpen does
// not follow a bind or mount on the last path element, so the handle is on
// the target directory itself.
// VFSNodeBind refuses a target that is already a bind or mount point. When two
// filesystems want the same path, the first one keeps it and the second one
// fails here, while its default mount stays in place.
// The bind is stored in the filesystem, so VFSFileSystemUnmount can undo it.
static oserr_t
__MountFileSystemAt(
    _In_ FileSystem_t* fileSystem,
    _In_ mstring_t*    path)
{
    struct VFS* fsScope = fileSystem->MountScope;
    uuid_t      bindHandle;
    bool        createdDirectory = true;
    mstring_t*  bindPath;
    char*       cpath;
    oserr_t     oserr;

    TRACE("__MountFileSystemAt(fs=%u, path=%ms)", fileSystem->ID, path);

    if (path == NULL) {
        return OS_EINVALPARAMS;
    }

    // Only one bind is stored per filesystem.
    if (fileSystem->BindPath != NULL) {
        ERROR("__MountFileSystemAt filesystem %u is already bound at %ms", fileSystem->ID, fileSystem->BindPath);
        return OS_EEXISTS;
    }

    bindPath = mstr_clone(path);
    if (bindPath == NULL) {
        return OS_EOOM;
    }

    oserr = VFSNodeMkdir(
            fsScope,
            path,
            FILE_PERMISSION_READ,
            &bindHandle
    );
    if (oserr == OS_EEXISTS) {
        createdDirectory = false;
        cpath = mstr_u8(path);
        if (cpath == NULL) {
            ERROR("__MountFileSystemAt failed to convert path %ms", path);
            mstr_delete(bindPath);
            return OS_EOOM;
        }

        oserr = VFSNodeOpen(
            fsScope,
            cpath,
            __FILE_DIRECTORY,
            FILE_PERMISSION_READ,
            &bindHandle
        );
        free(cpath);
    }
    if (oserr != OS_EOK) {
        ERROR("__MountFileSystemAt failed to create or open node %ms: %u", path, oserr);
        mstr_delete(bindPath);
        return oserr;
    }

    oserr = VFSNodeBind(fsScope, fileSystem->MountHandle, bindHandle);
    if (oserr != OS_EOK) {
        ERROR("__MountFileSystemAt failed to bind filesystem %u at %ms, the path may be used by another filesystem: %u",
              fileSystem->ID, path, oserr);
        __CloseBindTarget(fsScope, bindHandle, path, createdDirectory);
        mstr_delete(bindPath);
        return oserr;
    }

    fileSystem->BindHandle           = bindHandle;
    fileSystem->BindPath             = bindPath;
    fileSystem->BindCreatedDirectory = createdDirectory;
    return OS_EOK;
}

// Undoes the bind that __MountFileSystemAt made, if there is one. The target
// directory is closed, and removed if it was created for the bind.
static void
__RemoveBind(
        _In_ FileSystem_t* fileSystem)
{
    oserr_t oserr;

    if (fileSystem->BindPath == NULL) {
        return;
    }

    oserr = VFSNodeUnbind(fileSystem->MountScope, fileSystem->BindHandle);
    if (oserr != OS_EOK) {
        ERROR("__RemoveBind failed to unbind %ms: %u", fileSystem->BindPath, oserr);
    }

    __CloseBindTarget(
        fileSystem->MountScope,
        fileSystem->BindHandle,
        fileSystem->BindPath,
        fileSystem->BindCreatedDirectory
    );
    mstr_delete(fileSystem->BindPath);
    fileSystem->BindPath             = NULL;
    fileSystem->BindHandle           = UUID_INVALID;
    fileSystem->BindCreatedDirectory = false;
}

static void
__BuildStorageParameters(
        _In_ FileSystem_t*                fileSystem,
        _In_ struct VFSStorageParameters* storageParameters)
{
    storageParameters->StorageType = fileSystem->Storage->Protocol.StorageType;
    switch (fileSystem->Storage->Protocol.StorageType) {
        case VFSSTORAGE_TYPE_DEVICE: {
            storageParameters->Storage.Device.DriverID = fileSystem->Storage->Protocol.Storage.Device.DriverID;
            storageParameters->Storage.Device.DeviceID = fileSystem->Storage->Protocol.Storage.Device.DeviceID;
        } break;
        case VFSSTORAGE_TYPE_FILE: {
            storageParameters->Storage.File.HandleID = fileSystem->Storage->Protocol.Storage.File.HandleID;
        } break;
        default: break;
    }
    storageParameters->Flags = 0; // TODO To what?
    storageParameters->SectorStart.QuadPart = fileSystem->SectorStart.QuadPart;
}

static oserr_t
__InitializeInterface(
        _In_ FileSystem_t*        fileSystem,
        _In_ struct VFSInterface* interface)
{
    struct VFSStorageParameters storageParameters;
    TRACE("__InitializeInterface(fs=%u)", fileSystem->ID);

    // The initialize function is optional
    if (interface->Operations.Initialize == NULL) {
        return OS_EOK;
    }

    __BuildStorageParameters(fileSystem, &storageParameters);
    return interface->Operations.Initialize(interface, &storageParameters, &fileSystem->Data);
}

oserr_t
__Connect(
        _In_ FileSystem_t*        fileSystem,
        _In_ struct VFSInterface* interface)
{
    oserr_t oserr;
    TRACE("__Connect(fs=%u)", fileSystem->ID);

    oserr = __InitializeInterface(fileSystem, interface);
    if (oserr != OS_EOK) {
        ERROR("__Connect failed to initialize the underlying fs");
        return oserr;
    }

    oserr = VFSNew(
            fileSystem->ID,
            &fileSystem->GUID,
            fileSystem->Storage,
            interface,
            fileSystem->Data,
            &fileSystem->VFS
    );
    if (oserr != OS_EOK) {
        ERROR("__Connect failed to initialize vfs data");
    }
    return oserr;
}

oserr_t
VFSFileSystemConnectInterface(
        _In_ FileSystem_t*        fileSystem,
        _In_ struct VFSInterface* interface)
{
    oserr_t osStatus;

    if (fileSystem == NULL || interface == NULL) {
        return OS_EINVALPARAMS;
    }
    TRACE("VFSFileSystemConnectInterface(fs=%u)", fileSystem->ID);

    usched_mtx_lock(&fileSystem->Lock);
    if (fileSystem->State == FileSystemState_CONNECTED) {
        goto enable;
    }

    osStatus = __Connect(fileSystem, interface);
    if (osStatus != OS_EOK) {
        goto exit;
    }

    // OK we connected, store and switch state
    fileSystem->State     = FileSystemState_CONNECTED;
    fileSystem->Interface = interface;

    // Start out by mounting access to this partition under the device node
enable:
    osStatus = __MountFileSystemAtDefault(fileSystem);
    if (osStatus != OS_EOK) {
        mstring_t* label = __GetLabel(fileSystem);
        ERROR("VFSFileSystemConnectInterface failed to mount filesystem %ms", label);
        mstr_delete(label);
        goto exit;
    }

    fileSystem->State = FileSystemState_MOUNTED;

exit:
    usched_mtx_unlock(&fileSystem->Lock);
    return osStatus;
}

oserr_t
VFSFileSystemMount(
        _In_ FileSystem_t* fileSystem,
        _In_ mstring_t*    mountPoint)
{
    oserr_t    osStatus = OS_EOK;
    mstring_t* path;

    if (fileSystem == NULL) {
        return OS_EINVALPARAMS;
    }
    TRACE("VFSFileSystemMount(fs=%u)", fileSystem->ID);

    usched_mtx_lock(&fileSystem->Lock);
    if (fileSystem->State != FileSystemState_MOUNTED) {
        osStatus = OS_ENOTSUPPORTED;
        goto exit;
    }

    // If a mount point was supplied then we try to mount the filesystem at that point,
    // but otherwise we will be checking the default list
    mstring_t* fsLabel = __GetLabel(fileSystem);
    if (mountPoint) {
        osStatus = __MountFileSystemAt(fileSystem, mountPoint);
        if (osStatus != OS_EOK) {
            WARNING("VFSFileSystemMount failed to bind mount filesystem %ms at %ms",
                    fsLabel, mountPoint);
        }

        osStatus = VFSNodeGetPathHandle(fileSystem->MountHandle, &path);
        if (osStatus != OS_EOK) {
            goto exit;
        }

        TRACE("VFSFileSystemMount notifying session about %ms", path);
        __StorageReadyEvent(path);
        mstr_delete(path);
    } else {
        // look up default mount points
        for (int i = 0; g_defaultMounts[i].path != NULL; i++) {
            mstring_t* label = mstr_new_u8(g_defaultMounts[i].label);
            if (label && !mstr_cmp(label, fsLabel)) {
                mstring_t* bindPath = mstr_new_u8(g_defaultMounts[i].path);
                osStatus = __MountFileSystemAt(fileSystem, bindPath);
                if (osStatus != OS_EOK) {
                    WARNING("VFSFileSystemMount failed to bind mount filesystem %ms at %ms",
                            fsLabel, bindPath);
                } else {
                    TRACE("VFSFileSystemMount notifying session about %ms", bindPath);
                    __StorageReadyEvent(bindPath);
                }
                mstr_delete(bindPath);
                mstr_delete(label);
                break;
            }
            mstr_delete(label);
        }
    }
    mstr_delete(fsLabel);

exit:
    usched_mtx_unlock(&fileSystem->Lock);
    return osStatus;
}

oserr_t
VFSFileSystemUnmount(
        _In_ FileSystem_t* fileSystem,
        _In_ unsigned int  flags)
{
    oserr_t osStatus = OS_ENOTSUPPORTED;
    _CRT_UNUSED(flags);

    if (fileSystem == NULL) {
        return OS_EINVALPARAMS;
    }
    TRACE("VFSFileSystemUnmount(fs=%u)", fileSystem->ID);

    usched_mtx_lock(&fileSystem->Lock);
    if (fileSystem->State == FileSystemState_MOUNTED) {
        // The bind goes first. VFSNodeUnlink refuses to remove the default
        // mount directory while it is still bound somewhere.
        __RemoveBind(fileSystem);

        // Undo __MountFileSystemAtDefault in the scope it mounted in. The
        // mount directory is closed and removed even if the unmount fails, so
        // the next registration of the disk can mount there again.
        osStatus = VFSNodeUnmount(
            fileSystem->MountScope,
            fileSystem->MountHandle
        );
        if (osStatus != OS_EOK) {
            ERROR("VFSFileSystemUnmount failed to unmount filesystem %u: %u", fileSystem->ID, osStatus);
        }

        __RemoveMountDirectory(fileSystem->MountScope, fileSystem->MountHandle, fileSystem->MountPath);
        mstr_delete(fileSystem->MountPath);
        fileSystem->MountPath   = NULL;
        fileSystem->MountScope  = NULL;
        fileSystem->MountHandle = UUID_INVALID;
        fileSystem->State       = FileSystemState_CONNECTED;
    }
    usched_mtx_unlock(&fileSystem->Lock);
    return osStatus;
}

oserr_t
VFSFileSystemDisconnectInterface(
        _In_ FileSystem_t* fileSystem,
        _In_ unsigned int  flags)
{
    oserr_t osStatus = OS_EBUSY;

    if (fileSystem == NULL) {
        return OS_EINVALPARAMS;
    }
    TRACE("VFSFileSystemDisconnectInterface(fs=%u)", fileSystem->ID);

    usched_mtx_lock(&fileSystem->Lock);
    if (fileSystem->State == FileSystemState_CONNECTED) {
        // The filesystem must already be detached from new opens. Drain its
        // records while the backend is live, including asynchronous closes.
        VFSDestroy(fileSystem->VFS);
        fileSystem->VFS = NULL;
        osStatus = fileSystem->Interface->Operations.Destroy(fileSystem->Interface, fileSystem->Data, flags);
        if (osStatus != OS_EOK) {
            WARNING("VFSFileSystemDisconnectInterface failed to destroy filesystem %u: %u", fileSystem->ID, osStatus);
        }

        // Each filesystem gets its own interface (VFSInterfaceLoadDriver), so
        // it is released here, also when Destroy failed, as nothing else uses
        // it. __DestroyDriver skips NO_INTERFACE, so this is the only release.
        VFSInterfaceDelete(fileSystem->Interface);
        fileSystem->Interface = NULL;
        fileSystem->Data      = NULL;
        fileSystem->State     = FileSystemState_NO_INTERFACE;
    }
    usched_mtx_unlock(&fileSystem->Lock);
    return osStatus;
}
