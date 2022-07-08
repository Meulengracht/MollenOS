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
#include <ddk/convert.h>
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
    MString_t*    path;
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

static void __NotifySessionManager(MString_t* mount_point)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetSessionService());
    sys_session_disk_connected(GetGrachtClient(), &msg.base, MStringRaw(mount_point));
}

static enum FileSystemType
__GetTypeFromGuid(
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
        _In_ uint64_t             sector,
        _In_ uint64_t             sectorCount,
        _In_ enum FileSystemType  type,
        _In_ guid_t*              typeGuid,
        _In_ guid_t*              guid)
{
    FileSystem_t*       fileSystem;
    enum FileSystemType fsType = type;
    oserr_t            osStatus;

    fileSystem = (FileSystem_t*)malloc(sizeof(FileSystem_t));
    if (!fileSystem) {
        return NULL;
    }
    memset(fileSystem, 0, sizeof(FileSystem_t));

    if (fsType == FileSystemType_UNKNOWN) {
        // try to deduce from type guid
        fsType = __GetTypeFromGuid(typeGuid);
    }

    ELEMENT_INIT(&fileSystem->Header, (uintptr_t)storage->DeviceID, fileSystem);
    fileSystem->Type                   = fsType;
    fileSystem->State                  = FileSystemState_CREATED;
    fileSystem->CommonData.SectorStart = sector;
    fileSystem->CommonData.SectorCount = sectorCount;
    memcpy(&fileSystem->CommonData.Storage, storage, sizeof(StorageDescriptor_t));
    usched_mtx_init(&fileSystem->Lock);

    osStatus = VfsLoadModule(fsType, &fileSystem->Module);
    if (osStatus != OsOK) {
        ERROR("FileSystemNew failed to load filesystem of type %u", fsType);
        fileSystem->State = FileSystemState_ERROR;
    }

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
        VfsUnloadModule(fileSystem->Module);
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
    MString_t*      path;

    path = MStringCreate("/storage/", StrUTF8);
    if (path == NULL) {
        return OsOutOfMemory;
    }
    MStringAppendCharacters(path, &fileSystem->CommonData.Storage.Serial[0], StrUTF8);
    MStringAppendCharacter(path, '/');
    MStringAppend(path, fileSystem->CommonData.Label);

    osStatus = VFSNodeNewDirectory(fsScope, path, &partitionNode);
    if (osStatus != OsOK && osStatus != OsExists) {
        ERROR("__MountFileSystemAtDefault failed to create node %s", MStringRaw(path));
        MStringDestroy(path);
        return osStatus;
    }

    osStatus = VFSNodeMount(fsScope, partitionNode, NULL);
    if (osStatus != OsOK) {
        ERROR("__MountFileSystemAtDefault failed to mount filesystem at %s", MStringRaw(path));
        MStringDestroy(path);
        return osStatus;
    }

    // Store the root mount node, so we can unmount later (ez)
    fileSystem->MountNode = partitionNode;
    return OsOK;
}

static oserr_t
__MountFileSystemAt(
        _In_ FileSystem_t* fileSystem,
        _In_ MString_t*    path)
{
    struct VFS*     fsScope = VFSScopeGet(UUID_INVALID);
    struct VFSNode* bindNode;
    oserr_t        osStatus;

    osStatus = VFSNodeNewDirectory(fsScope, path, &bindNode);
    if (osStatus != OsOK && osStatus != OsExists) {
        ERROR("__MountFileSystemAt failed to create node %s", MStringRaw(path));
        return osStatus;
    }

    return VFSNodeBind(fsScope, fileSystem->MountNode, bindNode);
}

void
VfsFileSystemMount(
        _In_ FileSystem_t* fileSystem,
        _In_ MString_t*    mountPoint)
{
    oserr_t   osStatus;
    MString_t* path;

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
        ERROR("VfsFileSystemMount failed to mount filesystem %s", MStringRaw(fileSystem->CommonData.Label));
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
            WARNING("VfsFileSystemMount failed to bind mount filesystem %s at %s",
                    MStringRaw(mountPoint), MStringRaw(fileSystem->CommonData.Label));
        }
    } else {
        // look up default mount points
        for (int i = 0; g_defaultMounts[i].path != NULL; i++) {
            MString_t* label = MStringCreate(g_defaultMounts[i].label, StrUTF8);
            if (label && MStringCompare(label, fileSystem->CommonData.Label, 0) == MSTRING_FULL_MATCH) {
                MString_t* bindPath = MStringCreate(g_defaultMounts[i].path, StrUTF8);
                osStatus = __MountFileSystemAt(fileSystem, bindPath);
                if (osStatus != OsOK) {
                    WARNING("VfsFileSystemMount failed to bind mount filesystem %s at %s",
                            MStringRaw(mountPoint), MStringRaw(fileSystem->CommonData.Label));
                    MStringDestroy(label);
                }
                MStringDestroy(bindPath);
                break;
            }
            MStringDestroy(label);
        }
    }

    path = VFSNodeMakePath(fileSystem->MountNode, 0);
    if (path == NULL) {
        // oh no

        return;
    }

    TRACE("VfsFileSystemMount notifying session about %s", MStringRaw(path));
    __NotifySessionManager(path);
    MStringDestroy(path);
}

oserr_t
VfsFileSystemUnmount(
        _In_ FileSystem_t* fileSystem,
        _In_ unsigned int  flags)
{
    struct VFS* fsScope = VFSScopeGet(UUID_INVALID);

    usched_mtx_lock(&fileSystem->Lock);
    if (fileSystem->State == FileSystemState_MOUNTED) {
        VFSNodeDestroy(fileSystem->MountNode);
        fileSystem->Module->Operations.Destroy(&fileSystem->CommonData, flags);
    }
    usched_mtx_unlock(&fileSystem->Lock);
    FileSystemDestroy(fileSystem);
}
