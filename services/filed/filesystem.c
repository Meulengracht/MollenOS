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
 */

#define __TRACE

#include <assert.h>
#include <ddk/utils.h>
#include <internal/_ipc.h>
#include <stdlib.h>
#include <string.h>
#include <vfs/filesystem.h>
#include <vfs/scope.h>
#include <vfs/vfs.h>
#include <vfs/vfs_module.h>

struct mount_point {
    element_t     header;
    mstring_t*    path;
    unsigned int  flags;
    FileSystem_t* filesystem;
};

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
};
static struct usched_mtx g_mountsLock;

static void __NotifySessionManager(mstring_t* mountPoint)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetSessionService());
    char*                    mp  = mstr_u8(mountPoint);

    sys_session_disk_connected(GetGrachtClient(), &msg.base, mp);
    free(mp);
}

enum FileSystemType
FileSystemParseGuid(
        _In_ guid_t* guid)
{
    if (!guid_cmp(guid, &g_efiGuid) || !guid_cmp(guid, &g_fatGuid)) {
        return FileSystemType_FAT;
    }
    if (!guid_cmp(guid, &g_mfsSystemGuid) || !guid_cmp(guid, &g_mfsUserDataGuid) ||
        !guid_cmp(guid, &g_mfsUserGuid) || !guid_cmp(guid, &g_mfsDataGuid)) {
        return FileSystemType_MFS;
    }
    return FileSystemType_UNKNOWN;
}

void VfsFileSystemInitialize(void)
{
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
        _In_ StorageDescriptor_t* storage,
        _In_ uuid_t               id,
        _In_ guid_t*              guid,
        _In_ uint64_t             sector,
        _In_ uint64_t             sectorCount,
        _In_ struct VFSModule*    module)
{
    FileSystem_t* fileSystem;
    oserr_t       osStatus;

    fileSystem = (FileSystem_t*)malloc(sizeof(FileSystem_t));
    if (!fileSystem) {
        return NULL;
    }
    memset(fileSystem, 0, sizeof(FileSystem_t));

    ELEMENT_INIT(&fileSystem->Header, (uintptr_t)storage->DeviceID, fileSystem);
    fileSystem->State                  = FileSystemState_CREATED;
    fileSystem->Module                 = module;
    fileSystem->CommonData.SectorStart = sector;
    fileSystem->CommonData.SectorCount = sectorCount;
    memcpy(&fileSystem->CommonData.Storage, storage, sizeof(StorageDescriptor_t));
    usched_mtx_init(&fileSystem->Lock);

    osStatus = VFSNew(id, guid, fileSystem->Module, &fileSystem->CommonData, &fileSystem->VFS);
    if (osStatus != OsOK) {
        ERROR("FileSystemNew failed to initialize vfs data");
        fileSystem->State = FileSystemState_ERROR;
    }
    return fileSystem;
}

void FileSystemDestroy(FileSystem_t* fileSystem)
{
    if (fileSystem->VFS != NULL) {
        VFSDestroy(fileSystem->VFS);
    }
    if (fileSystem->Module != NULL) {
        VFSModuleDelete(fileSystem->Module);
    }
    free(fileSystem);
}

static oserr_t
__MountFileSystemAtDefault(
        _In_ FileSystem_t* fileSystem)
{
    struct VFS*     fsScope = VFSScopeGet(UUID_INVALID);
    struct VFSNode* partitionNode;
    oserr_t        osStatus;
    mstring_t*      path;

    path = mstr_fmt("/storage/%s/%ms", &fileSystem->CommonData.Storage.Serial[0], fileSystem->CommonData.Label);
    if (path == NULL) {
        return OsOutOfMemory;
    }

    osStatus = VFSNodeNewDirectory(fsScope, path, &partitionNode);
    if (osStatus != OsOK && osStatus != OsExists) {
        ERROR("__MountFileSystemAtDefault failed to create node %ms", path);
        mstr_delete(path);
        return osStatus;
    }

    osStatus = VFSNodeMount(fsScope, partitionNode, NULL);
    if (osStatus != OsOK) {
        ERROR("__MountFileSystemAtDefault failed to mount filesystem at %ms", path);
        mstr_delete(path);
        return osStatus;
    }

    // Store the root mount node, so we can unmount later (ez)
    fileSystem->MountNode = partitionNode;
    return OsOK;
}

static oserr_t
__MountFileSystemAt(
        _In_ FileSystem_t* fileSystem,
        _In_ mstring_t*    path)
{
    struct VFS*     fsScope = VFSScopeGet(UUID_INVALID);
    struct VFSNode* bindNode;
    oserr_t        osStatus;

    osStatus = VFSNodeNewDirectory(fsScope, path, &bindNode);
    if (osStatus != OsOK && osStatus != OsExists) {
        ERROR("__MountFileSystemAt failed to create node %ms", path);
        return osStatus;
    }

    return VFSNodeBind(fsScope, fileSystem->MountNode, bindNode);
}

void
VfsFileSystemMount(
        _In_ FileSystem_t* fileSystem,
        _In_ mstring_t*    mountPoint)
{
    oserr_t   osStatus;
    mstring_t* path;

    if (fileSystem == NULL || fileSystem->Module == NULL) {
        return;
    }

    osStatus = fileSystem->Module->Operations.Initialize(&fileSystem->CommonData);
    if (osStatus != OsOK) {
        ERROR("VfsFileSystemMount failed to initialize filesystem of type %u: %u", fileSystem->Type, osStatus);
        fileSystem->State = FileSystemState_ERROR;
        return;
    }

    // Start out by mounting access to this partition under the device node
    osStatus = __MountFileSystemAtDefault(fileSystem);
    if (osStatus != OsOK) {
        ERROR("VfsFileSystemMount failed to mount filesystem %ms", fileSystem->CommonData.Label);
        fileSystem->State = FileSystemState_ERROR;
        return;
    }

    // set state to loaded at this point, even if the bind mounts fail
    fileSystem->State = FileSystemState_MOUNTED;

    // If a mount point was supplied then we try to mount the filesystem as well at that point,
    // but otherwise we will be checking the default list
    if (mountPoint) {
        osStatus = __MountFileSystemAt(fileSystem, mountPoint);
        if (osStatus != OsOK) {
            WARNING("VfsFileSystemMount failed to bind mount filesystem %ms at %ms",
                    mountPoint, fileSystem->CommonData.Label);
        }
    } else {
        // look up default mount points
        for (int i = 0; g_defaultMounts[i].path != NULL; i++) {
            mstring_t* label = mstr_new_u8(g_defaultMounts[i].label);
            if (label && !mstr_cmp(label, fileSystem->CommonData.Label)) {
                mstring_t* bindPath = mstr_new_u8(g_defaultMounts[i].path);
                osStatus = __MountFileSystemAt(fileSystem, bindPath);
                if (osStatus != OsOK) {
                    WARNING("VfsFileSystemMount failed to bind mount filesystem %ms at %ms",
                            mountPoint, fileSystem->CommonData.Label);
                    mstr_delete(label);
                }
                mstr_delete(bindPath);
                break;
            }
            mstr_delete(label);
        }
    }

    path = VFSNodeMakePath(fileSystem->MountNode, 0);
    if (path == NULL) {
        // oh no

        return;
    }

    TRACE("VfsFileSystemMount notifying session about %ms", path);
    __NotifySessionManager(path);
    mstr_delete(path);
}

oserr_t
VfsFileSystemUnmount(
        _In_ FileSystem_t* fileSystem,
        _In_ unsigned int  flags)
{
    usched_mtx_lock(&fileSystem->Lock);
    if (fileSystem->State == FileSystemState_MOUNTED) {
        VFSNodeDestroy(fileSystem->MountNode);
        fileSystem->Module->Operations.Destroy(&fileSystem->CommonData, flags);
    }
    usched_mtx_unlock(&fileSystem->Lock);
    FileSystemDestroy(fileSystem);
}
