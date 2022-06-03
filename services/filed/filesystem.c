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

struct mount_point {
    element_t     header;
    MString_t*    path;
    unsigned int  flags;
    FileSystem_t* filesystem;
};

// mount all partitions here
// /fs/<label>
struct default_mounts {
    MString_t*   label;
    MString_t*   path;
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

static struct default_mounts g_defaultMounts[] = {
        { "vali-efi",  "/efi", MOUNT_READONLY },
        { "vali-boot", "/boot", MOUNT_READONLY },
        { "vali-data", "/", 0 },
};

static list_t            g_mounts = LIST_INIT;
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
    MStringDestroy(fileSystem->mount_point);
    free(fileSystem);
}

static void
RegisterMountPoint(
        _In_ FileSystem_t* fileSystem)
{
    struct mount_point* mount = malloc(sizeof(struct mount_point));
    if (!mount) {
        ERROR("RegisterMountPoint failed to allocate memory for mount point");
        return;
    }

    ELEMENT_INIT(&mount->header, NULL, mount);
    mount->filesystem = fileSystem;
    mount->path = fileSystem->mount_point;
    mount->flags = fileSystem->base.Flags;

    usched_mtx_lock(&g_mountsLock);
    list_append(&g_mounts, &mount->header);
    usched_mtx_unlock(&g_mountsLock);
}

static void
UnregisterMountPoint(MString_t* mountPoint)
{
    element_t* i;

    usched_mtx_lock(&g_mountsLock);
    _foreach(i, &g_mounts) {
        struct mount_point* mount = i->value;
        if (MStringCompare(mount->path, mountPoint, 0) == MSTRING_FULL_MATCH) {
            list_remove(&g_mounts, i);
            free(i);
            break;
        }
    }
    usched_mtx_unlock(&g_mountsLock);
}

void
VfsFileSystemMount(
        _In_ FileSystem_t* fileSystem,
        _In_ MString_t*    mountPoint)
{
    char       buffer[8] = { 0 };
    OsStatus_t osStatus;

    if (!fileSystem) {
        return;
    }

    if (mountPoint) {

    }
    else {
        // Copy the storage ident over
        // We use "st" for hard media, and "rm" for removables
        strcpy(&buffer[0], (fileSystem->base.Disk.flags & SYS_STORAGE_FLAGS_REMOVABLE) ? "rm" : "st");
        itoa((int)fileSystem->id, &buffer[2], 10);

        // create the mount point
        fileSystem->mount_point = MStringCreate(&buffer[0], StrASCII);
    }

    TRACE("VfsFileSystemMount resolving filesystem module");
    fileSystem->module = VfsLoadModule(fileSystem->type);
    if (!fileSystem->module) {
        ERROR("VfsFileSystemMount failed to load filesystem of type %u", fileSystem->type);
        fileSystem->state = FileSystemState_ERROR;
        return;
    }

    osStatus = fileSystem->module->Initialize(&fileSystem->base);
    if (osStatus != OsSuccess) {
        ERROR("VfsFileSystemMount failed to initialize filesystem of type %u: %u", fileSystem->type, osStatus);
        fileSystem->state = FileSystemState_ERROR;
        return;
    }

    // set state to loaded
    fileSystem->state = FileSystemState_MOUNTED;
    RegisterMountPoint(fileSystem);

    TRACE("VfsFileSystemMount notifying session about %s", MStringRaw(fileSystem->mount_point));
    __NotifySessionManager(fileSystem->mount_point);
}

void VfsFileSystemUnmount(
        _In_ FileSystem_t* fileSystem,
        _In_ unsigned int  flags)
{
    usched_mtx_lock(&fileSystem->lock);
    if (fileSystem->state == FileSystemState_MOUNTED) {
        UnregisterMountPoint(fileSystem->mount_point);

        // destroy all filehandles / requests

        // unload filesystem
        fileSystem->module->Destroy(&fileSystem->base, flags);
        VfsUnloadModule(fileSystem->module);
    }
    usched_mtx_unlock(&fileSystem->lock);

    // cleanup resources
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

FileSystem_t*
VfsFileSystemGetByFlags(
        _In_ unsigned int partitionFlags)
{
    element_t*    header;
    FileSystem_t* result = NULL;

    usched_mtx_lock(&g_mountsLock);
    _foreach(header, &g_mounts) {
        struct mount_point* mount = (struct mount_point*)header->value;
        if (mount->flags & partitionFlags) {
            result = mount->filesystem;
            break;
        }
    }
    usched_mtx_unlock(&g_mountsLock);
    return result;
}

FileSystem_t*
VfsFileSystemGetByPath(MString_t* path, MString_t** subPathOut)
{
    element_t*    header;
    int           index;
    FileSystem_t* result = NULL;
    TRACE("VfsFileSystemGetByPath(path=%s)", MStringRaw(path));

    // To open a new file we need to find the correct
    // filesystem identifier and seperate it from its absolute path
    index = MStringFind(path, ':', 0);
    if (index == MSTRING_NOT_FOUND) {
        return NULL;
    }

    usched_mtx_lock(&g_mountsLock);
    _foreach(header, &g_mounts) {
        struct mount_point* mount = (struct mount_point*)header->value;

        // accept partial matches
        index = MStringCompare(mount->path, path, 0);
        TRACE("VfsFileSystemGetByPath comparing %s==%s", MStringRaw(mount->path), MStringRaw(path));
        if (index != MSTRING_NO_MATCH) {
            index = (int)MStringLength(mount->path);
            if (subPathOut) {
                // we skip ':/' here
                *subPathOut = MStringSubString(path, index + 2, -1);
                TRACE("VfsFileSystemGetByPath subpath=%s", MStringRaw(*subPathOut));
            }
            result = mount->filesystem;
            break;
        }
    }
    usched_mtx_unlock(&g_mountsLock);
    return result;
}

OsStatus_t
VfsFileSystemGetByPathSafe(
        _In_  const char*    path,
        _Out_ FileSystem_t** fileSystem)
{
    MString_t* fspath = MStringCreate(path, StrUTF8);
    if (!fspath) {
        return OsInvalidParameters;
    }

    *fileSystem = VfsFileSystemGetByPath(fspath, NULL);
    if (!(*fileSystem)) {
        return OsDoesNotExist;
    }
    return OsSuccess;
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
