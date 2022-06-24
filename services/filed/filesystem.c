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

extern void
VfsFileSystemCacheInitialize(
        _In_ FileSystem_t* fileSystem);

static uint64_t vfs_request_hash(const void* element);
static int      vfs_request_cmp(const void* element1, const void* element2);

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
VfsFileSystemCreate(
        _In_ FileSystemDisk_t*   disk,
        _In_ UUId_t              id,
        _In_ uint64_t            sector,
        _In_ uint64_t            sectorCount,
        _In_ enum FileSystemType type,
        _In_ guid_t*             typeGuid,
        _In_ guid_t*             guid)
{
    FileSystem_t*       fileSystem;
    enum FileSystemType fsType = type;

    fileSystem = (FileSystem_t*)malloc(sizeof(FileSystem_t));
    if (!fileSystem) {
        return NULL;
    }
    memset(fileSystem, 0, sizeof(FileSystem_t));

    if (!fsType) {
        // try to deduce from type guid
        fsType = __GetTypeFromGuid(typeGuid);
    }

    ELEMENT_INIT(&fileSystem->header, (uintptr_t)disk->device_id, fileSystem);
    fileSystem->id               = id;
    fileSystem->type             = fsType;
    fileSystem->state            = FileSystemState_CREATED;
    fileSystem->base.SectorStart = sector;
    fileSystem->base.SectorCount = sectorCount;
    memcpy(&fileSystem->base.Disk, disk, sizeof(FileSystemDisk_t));
    usched_mtx_init(&fileSystem->lock);

    hashtable_construct(&fileSystem->requests, 0,
                        sizeof(FileSystemRequest_t*),
                        vfs_request_hash, vfs_request_cmp);
    VfsFileSystemCacheInitialize(fileSystem);
    return fileSystem;
}

void FileSystemDestroy(FileSystem_t* fileSystem)
{
    hashtable_destroy(&fileSystem->requests);
    hashtable_destroy(&fileSystem->cache);
    free(fileSystem);
}

static OsStatus_t
__MountFileSystemAtDefault(
        _In_ FileSystem_t* fileSystem)
{
    struct VFS*     fsScope = VFSScopeGet(UUID_INVALID);
    struct VFSNode* deviceNode;
    struct VFSNode* partitionNode;
    OsStatus_t      osStatus;
    MString_t*      path;

    path = MStringCreate("/storage/", StrUTF8);
    if (path == NULL) {
        return OsOutOfMemory;
    }
    MStringAppendCharacters(path, &fileSystem->base.Disk.descriptor.Serial[0], StrUTF8);

    // retrieve the device node, this WILL be mounted earlier once the disk is registered with
    // the file service
    osStatus = VFSNodeLookup(fsScope, path, &deviceNode);
    if (osStatus != OsOK) {
        ERROR("__MountFileSystemAtDefault failed to lookup node %s", MStringRaw(path));
        return osStatus;
    }

    // now we create the partition node, and we format this for the partition label.
    // TODO verify the name of the partition label
    osStatus = VFSNodeChildNew(fsScope, deviceNode, fileSystem->base.Label, VFS_NODE_FILESYSTEM, &partitionNode);
    if (osStatus) {
        return osStatus;
    }

    osStatus = VFSNodeFileSystemDataSet(partitionNode, fileSystem);
    if (osStatus != OsOK) {
        return osStatus;
    }

    // Store the root mount node, so we can unmount later (ez)
    fileSystem->MountNode = partitionNode;
    return OsOK;
}

static OsStatus_t
__MountFileSystemAt(
        _In_ FileSystem_t* fileSystem,
        _In_ MString_t*    path)
{
    struct VFS*     fsScope = VFSScopeGet(UUID_INVALID);
    struct VFSNode* bindNode;
    OsStatus_t      osStatus;

    osStatus = VFSNodeNew(fsScope, path, 0, &bindNode);
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
    OsStatus_t osStatus;

    if (!fileSystem) {
        return;
    }

    TRACE("VfsFileSystemMount resolving filesystem module");
    fileSystem->module = VfsLoadModule(fileSystem->type);
    if (!fileSystem->module) {
        ERROR("VfsFileSystemMount failed to load filesystem of type %u", fileSystem->type);
        fileSystem->state = FileSystemState_ERROR;
        return;
    }

    osStatus = fileSystem->module->Initialize(&fileSystem->base);
    if (osStatus != OsOK) {
        ERROR("VfsFileSystemMount failed to initialize filesystem of type %u: %u", fileSystem->type, osStatus);
        fileSystem->state = FileSystemState_ERROR;
        return;
    }

    // Start out by mounting access to this partition under the device node
    osStatus = __MountFileSystemAtDefault(fileSystem);
    if (osStatus != OsOK) {
        ERROR("VfsFileSystemMount failed to mount filesystem %s", MStringRaw(fileSystem->base.Label));
        fileSystem->state = FileSystemState_ERROR;
        return;
    }

    // set state to loaded at this point, even if the bind mounts fail
    fileSystem->state = FileSystemState_MOUNTED;

    // If a mount point was supplied then we try to mount the filesystem as well at that point,
    // but otherwise we will be checking the default list
    if (mountPoint) {
        osStatus = __MountFileSystemAt(fileSystem, mountPoint);
        if (osStatus != OsOK) {
            WARNING("VfsFileSystemMount failed to bind mount filesystem %s at %s",
                    MStringRaw(mountPoint), MStringRaw(fileSystem->base.Label));
        }
    } else {
        // look up default mount points
        for (int i = 0; g_defaultMounts[i].path != NULL; i++) {
            MString_t* label = MStringCreate(g_defaultMounts[i].label, StrUTF8);
            if (label && MStringCompare(label, fileSystem->base.Label, 0) == MSTRING_FULL_MATCH) {
                MString_t* bindPath = MStringCreate(g_defaultMounts[i].path, StrUTF8);
                osStatus = __MountFileSystemAt(fileSystem, bindPath);
                if (osStatus != OsOK) {
                    WARNING("VfsFileSystemMount failed to bind mount filesystem %s at %s",
                            MStringRaw(mountPoint), MStringRaw(fileSystem->base.Label));
                    MStringDestroy(label);
                }
                MStringDestroy(bindPath);
                break;
            }
            MStringDestroy(label);
        }
    }

    TRACE("VfsFileSystemMount notifying session about %s", MStringRaw(VFSNodePath(fileSystem->MountNode)));
    __NotifySessionManager(VFSNodePath(fileSystem->MountNode));
}

OsStatus_t
VfsFileSystemUnmount(
        _In_ FileSystem_t* fileSystem,
        _In_ unsigned int  flags)
{
    struct VFS* fsScope = VFSScopeGet(UUID_INVALID);
    OsStatus_t  osStatus;

    usched_mtx_lock(&fileSystem->lock);
    if (fileSystem->state == FileSystemState_MOUNTED) {
        osStatus = VFSNodeDestroy(fsScope, fileSystem->MountNode);
        if (osStatus != OsOK) {
            usched_mtx_unlock(&fileSystem->lock);
            ERROR("VfsFileSystemUnmount failed to unmount filesystem %s", MStringRaw(fileSystem->base.Label));
            return osStatus;
        }

        // destroy all filehandles / requests

        // unload filesystem
        fileSystem->module->Destroy(&fileSystem->base, flags);
        VfsUnloadModule(fileSystem->module);
    }
    usched_mtx_unlock(&fileSystem->lock);
    FileSystemDestroy(fileSystem);
}

void VfsFileSystemRegisterRequest(
        _In_ FileSystem_t*        fileSystem,
        _In_ FileSystemRequest_t* request)
{
    assert(fileSystem != NULL);
    assert(request != NULL);

    // when requests are registered, we set their state to INPROGRESS
    VfsRequestSetState(request, FileSystemRequest_INPROGRESS);

    usched_mtx_lock(&fileSystem->lock);
    hashtable_set(&fileSystem->requests, request);
    usched_mtx_unlock(&fileSystem->lock);
}

void VfsFileSystemUnregisterRequest(
        _In_ FileSystem_t*        fileSystem,
        _In_ FileSystemRequest_t* request)
{
    assert(fileSystem != NULL);
    assert(request != NULL);

    usched_mtx_lock(&fileSystem->lock);
    hashtable_remove(&fileSystem->requests, request);
    usched_mtx_unlock(&fileSystem->lock);

    VfsRequestSetState(request, FileSystemRequest_DONE);
}

static uint64_t vfs_request_hash(const void* element)
{
    const FileSystemRequest_t* request = element;
    return request->id;
}

static int vfs_request_cmp(const void* element1, const void* element2)
{
    const FileSystemRequest_t* lh = element1;
    const FileSystemRequest_t* rh = element2;
    return lh->id != rh->id;
}
