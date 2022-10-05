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

void VFSFileSystemInitialize(void)
{
    TRACE("VFSFileSystemInitialize()");

    guid_parse_string(&g_efiGuid, "C12A7328-F81F-11D2-BA4B-00A0C93EC93B");
    guid_parse_string(&g_fatGuid, "21686148-6449-6E6F-744E-656564454649");
    guid_parse_string(&g_mfsSystemGuid, "C4483A10-E3A0-4D3F-B7CC-C04A6E16612B");
    guid_parse_string(&g_mfsUserDataGuid, "80C6C62A-B0D6-4FF4-A69D-558AB6FD8B53");
    guid_parse_string(&g_mfsUserGuid, "8874F880-E7AD-4EE2-839E-6FFA54F19A72");
    guid_parse_string(&g_mfsDataGuid, "B8E1A523-5865-4651-9548-8A43A9C21384");
    usched_mtx_init(&g_mountsLock);
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
    TRACE("FileSystemNew(storage=%u, partition=%i, sector=%llu)", &storage->ID, partitionIndex, sector);

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
    usched_mtx_init(&fileSystem->Lock);
    return fileSystem;
}

static void __DestroyDriver(
        _In_ FileSystem_t* fileSystem)
{
    if (fileSystem->State != FileSystemState_CONNECTED) {
        return;
    }

    if (fileSystem->Interface && fileSystem->Interface->Operations.Destroy) {
        fileSystem->Interface->Operations.Destroy(fileSystem->Data, 0);
    }
    VFSInterfaceDelete(fileSystem->Interface);
}

void
FileSystemDestroy(
        _In_ FileSystem_t* fileSystem)
{
    TRACE("FileSystemDestroy(storage=%u, sector=%llu)",
          &fileSystem->Storage->ID,
          fileSystem->SectorStart.QuadPart);

    // Unmount and destroy all resources related to the VFS
    VFSDestroy(fileSystem->VFS);

    // Next is destroying the actual driver and the underlying
    // resources which are bound to this.
    __DestroyDriver(fileSystem);

    // Cleanup memory allocated by this subsystem
    free(fileSystem);
}

static mstring_t* __GetLabel(
        _In_ FileSystem_t* fileSystem)
{
    struct VFSStatFS stat;

    if (fileSystem->Interface && fileSystem->Interface->Operations.Stat) {
        fileSystem->Interface->Operations.Stat(fileSystem->Data, &stat);
        if (stat.Label) {
            return mstr_clone(stat.Label);
        }
    }

    // Otherwise no driver was available, or the filesystem is weird, thus
    // we must make up our own label
    // syntax: p{index}
    return mstr_fmt("p%i", fileSystem->PartitionIndex);
}

static oserr_t
__MountFileSystemAtDefault(
        _In_ FileSystem_t* fileSystem)
{
    struct VFS*     fsScope = VFSScopeGet(UUID_INVALID);
    uuid_t          nodeHandle;
    oserr_t         osStatus;
    mstring_t*      path;
    mstring_t*      label;
    TRACE("__MountFileSystemAtDefault(fs=%u)", fileSystem->ID);

    label = __GetLabel(fileSystem);
    if (label == NULL) {
        return OsOutOfMemory;
    }

    path = mstr_fmt("/storage/%s/%ms", &fileSystem->Storage->Stats.Serial[0], label);
    mstr_delete(label);
    if (path == NULL) {
        return OsOutOfMemory;
    }

    TRACE("__MountFileSystemAtDefault mounting at %ms", path);
    osStatus = VFSNodeMkdir(
            fsScope,
            path,
            FILE_PERMISSION_READ,
            &nodeHandle
    );
    if (osStatus != OsOK && osStatus != OsExists) {
        ERROR("__MountFileSystemAtDefault failed to create node %ms", path);
        mstr_delete(path);
        return osStatus;
    }

    osStatus = VFSNodeMount(fsScope, nodeHandle, fileSystem->VFS);
    if (osStatus != OsOK) {
        ERROR("__MountFileSystemAtDefault failed to mount filesystem at %ms", path);
        mstr_delete(path);
        return osStatus;
    }
    fileSystem->MountHandle = nodeHandle;
    return OsOK;
}

static oserr_t
__MountFileSystemAt(
        _In_ FileSystem_t* fileSystem,
        _In_ mstring_t*    path)
{
    struct VFS* fsScope = VFSScopeGet(UUID_INVALID);
    uuid_t      bindHandle;
    oserr_t     oserr;
    TRACE("__MountFileSystemAt(fs=%u, path=%ms)", fileSystem->ID, path);

    oserr = VFSNodeMkdir(
            fsScope,
            path,
            FILE_PERMISSION_READ,
            &bindHandle
    );
    if (oserr != OsOK && oserr != OsExists) {
        ERROR("__MountFileSystemAt failed to create node %ms", path);
        return oserr;
    }

    return VFSNodeBind(fsScope, fileSystem->MountHandle, bindHandle);
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

    // We do certainly not require an interface
    if (interface == NULL || interface->Operations.Initialize == NULL) {
        return OsOK;
    }
    __BuildStorageParameters(fileSystem, &storageParameters);
    return interface->Operations.Initialize(&storageParameters, &fileSystem->Data);
}

oserr_t
__Connect(
        _In_ FileSystem_t*        fileSystem,
        _In_ struct VFSInterface* interface)
{
    oserr_t oserr;
    TRACE("__Connect(fs=%u)", fileSystem->ID);

    oserr = __InitializeInterface(fileSystem, interface);
    if (oserr != OsOK) {
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
    if (oserr != OsOK) {
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

    if (fileSystem == NULL) {
        return OsInvalidParameters;
    }
    TRACE("VFSFileSystemConnectInterface(fs=%u)", fileSystem->ID);

    usched_mtx_lock(&fileSystem->Lock);
    if (fileSystem->State == FileSystemState_CONNECTED) {
        goto enable;
    }

    osStatus = __Connect(fileSystem, interface);
    if (osStatus != OsOK) {
        goto exit;
    }

    // OK we connected, store and switch state
    fileSystem->State     = FileSystemState_CONNECTED;
    fileSystem->Interface = interface;

    // Start out by mounting access to this partition under the device node
enable:
    osStatus = __MountFileSystemAtDefault(fileSystem);
    if (osStatus != OsOK) {
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
    oserr_t    osStatus = OsOK;
    mstring_t* path;

    if (fileSystem == NULL) {
        return OsInvalidParameters;
    }
    TRACE("VFSFileSystemMount(fs=%u)", fileSystem->ID);

    usched_mtx_lock(&fileSystem->Lock);
    if (fileSystem->State != FileSystemState_MOUNTED) {
        osStatus = OsNotSupported;
        goto exit;
    }

    // If a mount point was supplied then we try to mount the filesystem at that point,
    // but otherwise we will be checking the default list
    mstring_t* fsLabel = __GetLabel(fileSystem);
    if (mountPoint) {
        osStatus = __MountFileSystemAt(fileSystem, mountPoint);
        if (osStatus != OsOK) {
            WARNING("VFSFileSystemMount failed to bind mount filesystem %ms at %ms",
                    fsLabel, mountPoint);
        }

        osStatus = VFSNodeGetPathHandle(fileSystem->MountHandle, &path);
        if (osStatus != OsOK) {
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
                if (osStatus != OsOK) {
                    WARNING("VFSFileSystemMount failed to bind mount filesystem %ms at %ms",
                            fsLabel, bindPath);
                    mstr_delete(label);
                } else {
                    TRACE("VFSFileSystemMount notifying session about %ms", bindPath);
                    __StorageReadyEvent(bindPath);
                }
                mstr_delete(bindPath);
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
    oserr_t osStatus = OsNotSupported;
    _CRT_UNUSED(flags);

    if (fileSystem == NULL) {
        return OsInvalidParameters;
    }
    TRACE("VFSFileSystemUnmount(fs=%u)", fileSystem->ID);

    usched_mtx_lock(&fileSystem->Lock);
    if (fileSystem->State == FileSystemState_MOUNTED) {
        // TODO this is absolutely wrong, right now we are trying to unmount
        // ourselves in the wrong VFS (Which is ourselves). The right thing to do
        // here is to STORE THE VFS WE ARE MOUNTED IN.
        VFSNodeUnmount(
                fileSystem->VFS,
                fileSystem->MountHandle
        );
        // TODO this is also wrong fs scope!
        VFSNodeClose(fileSystem->VFS, fileSystem->MountHandle);
        fileSystem->State = FileSystemState_CONNECTED;
        osStatus = OsOK;
    }
    usched_mtx_unlock(&fileSystem->Lock);
    return osStatus;
}

oserr_t
VFSFileSystemDisconnectInterface(
        _In_ FileSystem_t* fileSystem,
        _In_ unsigned int  flags)
{
    oserr_t osStatus = OsBusy;

    if (fileSystem == NULL) {
        return OsInvalidParameters;
    }
    TRACE("VFSFileSystemDisconnectInterface(fs=%u)", fileSystem->ID);

    usched_mtx_lock(&fileSystem->Lock);
    if (fileSystem->State == FileSystemState_CONNECTED) {
        osStatus = fileSystem->Interface->Operations.Destroy(&fileSystem->Data, flags);
        fileSystem->State = FileSystemState_NO_INTERFACE;
    }
    usched_mtx_unlock(&fileSystem->Lock);
    return osStatus;
}
