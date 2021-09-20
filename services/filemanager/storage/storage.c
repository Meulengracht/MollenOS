/**
 * MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 * File Manager Service
 * - Handles all file related services and disk services
 */
#define __TRACE

#include <ctype.h>
#include <ddk/convert.h>
#include <ddk/filesystem.h>
#include <ddk/utils.h>
#include "../include/vfs.h"
#include <internal/_ipc.h>
#include <os/mollenos.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include "sys_storage_service_server.h"

static int    g_initHasRun = 0;
static list_t g_disks      = LIST_INIT;

static void __NotifySessionManager(char* identifier)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetSessionService());
    sys_session_disk_connected(GetGrachtClient(), &msg.base, identifier);
}

static void __LoadDelayedFileSystems(void)
{
    // Go through each file-system that has been register and initialize any filesystem
    // that was marked for delayed loading
    foreach(node, VfsGetFileSystems()) {
        FileSystem_t* fs = (FileSystem_t*)node->value;
        if (fs->state != FSCreated) {
            continue;
        }

        fs->module = VfsLoadModule(fs->type);
        if (!fs->module || fs->module->Initialize(&fs->descriptor) != OsSuccess) {
            fs->state = FSError;
            continue;
        }
        fs->state = FSLoaded;
    }
}

OsStatus_t
DiskRegisterFileSystem(
    _In_ FileSystemDisk_t* disk,
    _In_ uint64_t          sector,
    _In_ uint64_t          sectorCount,
    _In_ FileSystemType_t  type)
{
    FileSystem_t* fileSystem;
    char          buffer[8] = { 0 };
    UUId_t        id;
    OsStatus_t    status;

    TRACE("DiskRegisterFileSystem(sector=%u, sectorCount=%u, type=%u)",
          LODWORD(sector), LODWORD(sectorCount), type);

    fileSystem = (FileSystem_t*)malloc(sizeof(FileSystem_t));
    if (!fileSystem) {
        return OsOutOfMemory;
    }
    memset(fileSystem, 0, sizeof(FileSystem_t));

    id = VfsIdentifierAllocate(disk);

    // Copy the storage ident over
    // We use "st" for hard media, and "rm" for removables
    strcpy(&buffer[0], (disk->flags & SYS_STORAGE_FLAGS_REMOVABLE) ? "rm" : "st");
    itoa((int)id, &buffer[2], 10);

    ELEMENT_INIT(&fileSystem->header, (uintptr_t)disk->device_id, fileSystem);
    fileSystem->id                     = id;
    fileSystem->type                   = type;
    fileSystem->state                  = FSCreated;
    fileSystem->identifier             = MStringCreate(&buffer[0], StrASCII);
    fileSystem->descriptor.Flags       = 0;
    fileSystem->descriptor.SectorStart = sector;
    fileSystem->descriptor.SectorCount = sectorCount;
    memcpy(&fileSystem->descriptor.Disk, disk, sizeof(FileSystemDisk_t));

    // always add filesystems to list
    list_append(VfsGetFileSystems(), &fileSystem->header);

    // we must wait for a MFS to be registered before trying to load
    // additional drivers due to the fact that we only come bearing MFS driver in the initrd (for now)
    if (fileSystem->type == FSMFS) {
        TRACE("[register_fs] resolving filesystem module");
        fileSystem->module = VfsLoadModule(fileSystem->type);
        if (!fileSystem->module) {
            ERROR("[register_fs] failed to load filesystem of type %u", type);
            fileSystem->state = FSError;
            return OsDeviceError;
        }

        status = fileSystem->module->Initialize(&fileSystem->descriptor);
        if (status != OsSuccess) {
            ERROR("[register_fs] failed to initialize filesystem of type %u: %u", type, status);
            fileSystem->state = FSError;
            return status;
        }

        // set state to loaded
        fileSystem->state = FSLoaded;

        // Send notification to sessionmanager
        __NotifySessionManager(&buffer[0]);
        if (!g_initHasRun) {
            fileSystem->descriptor.Flags |= __FILESYSTEM_BOOT;
            g_initHasRun = 1;
            __LoadDelayedFileSystems();
        }
    }
    return OsSuccess;
}

static void
StorageUnloadFilesystem(
        _In_ UUId_t       deviceId,
        _In_ unsigned int flags)
{
    element_t* header;
    void*      voidKey = (void*)(uintptr_t)deviceId;

    // Keep iterating untill no more FS's are present on disk
    header = list_find(VfsGetFileSystems(), voidKey);
    while (header) {
        FileSystem_t* fileSystem = (FileSystem_t*)header->value;

        list_remove(VfsGetFileSystems(), header);

        // Close all open files that relate to this filesystem
        // @todo

        // Call destroy handler for that FS
        if (fileSystem->module && fileSystem->state == FSLoaded) {
            OsStatus_t status = fileSystem->module->Destroy(&fileSystem->descriptor, flags);
            if (status != OsSuccess) {
                // do we care?
            }
            VfsUnloadModule(fileSystem->module);
        }

        // Cleanup resources allocated by the filesystem
        VfsIdentifierFree(&fileSystem->descriptor.Disk, fileSystem->id);
        MStringDestroy(fileSystem->identifier);
        free(fileSystem);

        header = list_find(VfsGetFileSystems(), voidKey);
    }
}

static int
StorageInitialize(void* Context)
{
    FileSystemDisk_t*          disk = Context;
    struct vali_link_message   msg  = VALI_MSG_INIT_HANDLE(disk->driver_id);
    OsStatus_t                 status;
    struct sys_disk_descriptor gdescriptor;
    
    ctt_storage_stat(GetGrachtClient(), &msg.base, disk->device_id);
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    ctt_storage_stat_result(GetGrachtClient(), &msg.base, &status, &gdescriptor);
    if (status != OsSuccess) {
        // TODO: disk states
        // Disk->State = Crashed
        return OsStatusToErrno(status);
    }

    from_sys_disk_descriptor_dkk(&gdescriptor, &disk->descriptor);
    
    // Detect the disk layout, and if it fails
    // try to detect which kind of filesystem is present
    return OsStatusToErrno(DiskDetectLayout(disk));
}

static void
StorageCreate(
        _In_ UUId_t       deviceId,
        _In_ UUId_t       driverId,
        _In_ unsigned int flags)
{
    FileSystemDisk_t* disk;
    thrd_t            thread;

    TRACE("[vfs] [storage_create] driver %u, device %u, flags 0x%x", driverId, deviceId, flags);

    // Allocate a new instance of a disk descriptor
    // to store data and store initial data
    disk = (FileSystemDisk_t*)malloc(sizeof(FileSystemDisk_t));
    if (!disk) {
        ERROR("storage_register:: ran out of memory");
        return;
    }

    ELEMENT_INIT(&disk->header, (uintptr_t)deviceId, disk);
    disk->driver_id = driverId;
    disk->device_id = deviceId;
    disk->flags     = flags;
    // TODO: disk states
    //Disk->State = Initializing

    list_append(&g_disks, &disk->header);
    thrd_create(&thread, StorageInitialize, disk);
}

static void
StorageDestroy(
        _In_ UUId_t       deviceId,
        _In_ unsigned int flags)
{
    element_t* header = list_find(&g_disks, (void*)(uintptr_t)deviceId);

    if (header) {
        FileSystemDisk_t* disk = header->value;

        list_remove(&g_disks, (void*)(uintptr_t)deviceId);
        StorageUnloadFilesystem(deviceId, flags);
        free(disk);
    }
}

void sys_storage_register_invocation(struct gracht_message* message,
        const UUId_t driverId, const UUId_t deviceId, const enum sys_storage_flags flags)
{
    StorageCreate(deviceId, driverId, (unsigned int)flags);
}

void sys_storage_unregister_invocation(struct gracht_message* message, const UUId_t deviceId, const uint8_t forced)
{
    StorageDestroy(deviceId, forced);
}

void sys_storage_get_descriptor_invocation(struct gracht_message* message, const UUId_t fileHandle)
{
    struct sys_disk_descriptor gdescriptor = { 0 };
    FileSystem_t*              fileSystem;
    OsStatus_t                 status;

    status = VfsGetFileSystemByFileHandle(fileHandle, &fileSystem);
    if (status == OsSuccess) {
        to_sys_disk_descriptor_dkk(&fileSystem->descriptor.Disk.descriptor, &gdescriptor);
    }
    sys_storage_get_descriptor_response(message, status, &gdescriptor);
}

void sys_storage_get_descriptor_path_invocation(struct gracht_message* message, const char* filePath)
{
    struct sys_disk_descriptor gdescriptor = { 0 };
    FileSystem_t*              fileSystem;
    OsStatus_t                 status;

    status = VfsGetFileSystemByPath(filePath, &fileSystem);
    if (status == OsSuccess) {
        to_sys_disk_descriptor_dkk(&fileSystem->descriptor.Disk.descriptor, &gdescriptor);
    }
    sys_storage_get_descriptor_path_response(message, OsNotSupported, &gdescriptor);
}
