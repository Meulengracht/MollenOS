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
 *
 * File Manager Service
 * - Handles all file related services and disk services
 */
#define __TRACE

#include <ctype.h>
#include <ddk/filesystem.h>
#include <ddk/utils.h>
#include "include/vfs.h"
#include <internal/_ipc.h>
#include <os/mollenos.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include "svc_storage_protocol_server.h"

static int GlbInitHasRun = 0;

void ctt_storage_event_transfer_status_callback(
    struct ctt_storage_transfer_status_event* args)
{
    
}

static void
NotifySessionManagerOfNewDisk(
    _In_ char* identifier)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetSessionService());
    svc_session_new_device(GetGrachtClient(), &msg.base, identifier);
}

OsStatus_t
VfsResolveQueueExecute(void)
{
    // Iterate nodes and resolve
    foreach(fNode, VfsGetResolverQueue()) {
        FileSystem_t *Fs = (FileSystem_t*)fNode->Data;
        DataKey_t Key = { .Value.Id = Fs->Descriptor.Disk.Device };
        Fs->Module = VfsResolveFileSystem(Fs);

        // Sanitize the module - must exist 
        if (Fs->Module == NULL) {
            MStringDestroy(Fs->Identifier);
            VfsIdentifierFree(&Fs->Descriptor.Disk, Fs->Id);
            free(Fs);
            continue;
        }

        // Run initializor function
        if (Fs->Module->Initialize(&Fs->Descriptor) != OsSuccess) {
            MStringDestroy(Fs->Identifier);
            VfsIdentifierFree(&Fs->Descriptor.Disk, Fs->Id);
            free(Fs);
            continue;
        }

        // Add to list, by using the disk id as identifier
        CollectionAppend(VfsGetFileSystems(), CollectionCreateNode(Key, Fs));
    }
    return OsSuccess;
}

OsStatus_t
DiskRegisterFileSystem(
    _In_ FileSystemDisk_t*  Disk,
    _In_ uint64_t           Sector,
    _In_ uint64_t           SectorCount,
    _In_ FileSystemType_t   Type) 
{
    // Variables
    FileSystem_t *Fs = NULL;
    char IdentBuffer[8];
    DataKey_t Key = { .Value.Id = Disk->Device };

    // Trace
    TRACE("DiskRegisterFileSystem(Sector %u, Size %u, Type %u)",
        LODWORD(Sector), LODWORD(SectorCount), Type);

    // Allocate a new disk id
    UUId_t Id = VfsIdentifierAllocate(Disk);

    // Prep the buffer so we can build a new fs-identifier
    memset(&IdentBuffer[0], 0, 8);

    // Copy the storage ident over 
    // We use "st" for hard media, and "rm" for removables
    strcpy(&IdentBuffer[0], (Disk->Flags & SVC_STORAGE_REGISTER_FLAGS_REMOVABLE) ? "rm" : "st");
    itoa((int)Id, &IdentBuffer[2], 10);

    // Allocate a new copy of the fs-structure
    Fs = (FileSystem_t*)malloc(sizeof(FileSystem_t));
    if (!Fs) {
        return OsOutOfMemory;
    }
    
    // Initialize the structure
    Fs->Id = Id;
    Fs->Type = Type;
    Fs->Identifier = MStringCreate(&IdentBuffer[0], StrASCII);
    Fs->Descriptor.Flags = 0;
    Fs->Descriptor.SectorStart = Sector;
    Fs->Descriptor.SectorCount = SectorCount;
    memcpy(&Fs->Descriptor.Disk, Disk, sizeof(FileSystemDisk_t));

    // Resolve the module from the filesystem type 
    // - Now there is a special case, if no FS are
    //   registered and init hasn't run, and it's not MFS
    //   then we add to a resolve-wait queue
    if (!GlbInitHasRun && Fs->Type != FSMFS) {
        CollectionAppend(VfsGetResolverQueue(), CollectionCreateNode(Key, Fs));
    }
    else {
        TRACE("Resolving filesystem");
        Fs->Module = VfsResolveFileSystem(Fs);

        // Sanitize the module - must exist
        if (Fs->Module == NULL) {
            ERROR("Filesystem driver did not exist");
            MStringDestroy(Fs->Identifier);
            VfsIdentifierFree(&Fs->Descriptor.Disk, Fs->Id);
            free(Fs);
            return OsError;
        }

        // Run initializor function
        if (Fs->Module->Initialize(&Fs->Descriptor) != OsSuccess) {
            ERROR("Filesystem driver failed to initialize");
            MStringDestroy(Fs->Identifier);
            VfsIdentifierFree(&Fs->Descriptor.Disk, Fs->Id);
            free(Fs);
            return OsError;
        }

        // Add to list, by using the disk id as identifier
        CollectionAppend(VfsGetFileSystems(), CollectionCreateNode(Key, Fs));

        // Send notification to sessionmanager
        NotifySessionManagerOfNewDisk(&IdentBuffer[0]);

        // Start init?
        if (!GlbInitHasRun) {
            Fs->Descriptor.Flags |= __FILESYSTEM_BOOT;
            GlbInitHasRun = 1;
            VfsResolveQueueExecute();
        }
    }
    return OsSuccess;
}

static int
InitializeDisk(void* Context)
{
    FileSystemDisk_t*        disk = Context;
    struct vali_link_message msg  = VALI_MSG_INIT_HANDLE(disk->Driver);
    OsStatus_t               status;
    
    ctt_storage_stat(GetGrachtClient(), &msg.base, disk->Device);
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GetGrachtBuffer(), GRACHT_WAIT_BLOCK);
    ctt_storage_stat_result(GetGrachtClient(), &msg.base, &status, &disk->Descriptor);
    if (status != OsSuccess) {
        // TODO: disk states
        // Disk->State = Crashed
        return OsStatusToErrno(status);
    }
    
    // Detect the disk layout, and if it fails
    // try to detect which kind of filesystem is present
    return OsStatusToErrno(DiskDetectLayout(disk));
}

void svc_storage_register_callback(struct gracht_recv_message* message, struct svc_storage_register_args* args)
{
    FileSystemDisk_t* disk;
    DataKey_t         key = { .Value.Id = args->device_id };
    thrd_t            thread;

    TRACE("RegisterDisk(Driver %u, Device %u, Flags 0x%x)", 
        args->driver_id, args->device_id, args->flags);

    // Allocate a new instance of a disk descriptor 
    // to store data and store initial data
    disk = (FileSystemDisk_t*)malloc(sizeof(FileSystemDisk_t));
    if (!disk) {
        ERROR("storage_register:: ran out of memory");
        return;
    }
    
    disk->Driver = args->driver_id;
    disk->Device = args->device_id;
    disk->Flags  = args->flags;
    // TODO: disk states
    //Disk->State = Initializing
    
    CollectionAppend(VfsGetDisks(), CollectionCreateNode(key, disk));
    thrd_create(&thread, InitializeDisk, disk);
}

void svc_storage_unregister_callback(struct gracht_recv_message* message, struct svc_storage_unregister_args* args)
{
    FileSystemDisk_t* disk = NULL;
    CollectionItem_t* lNode = NULL;
    DataKey_t         key = { .Value.Id = args->device_id };
    
    // Keep iterating untill no more FS's are present on disk
    lNode = CollectionGetNodeByKey(VfsGetFileSystems(), key, 0);
    while (lNode != NULL) {
        FileSystem_t* fileSystem = (FileSystem_t*)lNode->Data;

        // Close all open files that relate to this filesystem
        // @todo

        // Call destroy handler for that FS
        if (fileSystem->Module->Destroy(&fileSystem->Descriptor, args->flags) != OsSuccess) {
            // What do?
        }
        fileSystem->Module->References--;

        // Sanitize the module references 
        // If there are no more refs then cleanup module
        if (fileSystem->Module->References <= 0) {
            // Unload? Or keep loaded?
        }

        // Cleanup resources allocated by the filesystem 
        VfsIdentifierFree(&fileSystem->Descriptor.Disk, fileSystem->Id);
        MStringDestroy(fileSystem->Identifier);
        free(fileSystem);

        CollectionRemoveByNode(VfsGetFileSystems(), lNode);
        lNode = CollectionGetNodeByKey(VfsGetFileSystems(), key, 0);
    }

    // Remove the disk from the list of disks
    disk = CollectionGetDataByKey(VfsGetDisks(), key, 0);
    CollectionRemoveByKey(VfsGetDisks(), key);
    free(disk);
}

void svc_storage_get_descriptor_callback(struct gracht_recv_message* message, struct svc_storage_get_descriptor_args* args)
{
    OsStorageDescriptor_t descriptor = { 0 };
    
    svc_storage_get_descriptor_response(message, OsNotSupported, &descriptor);
}

void svc_storage_get_descriptor_from_path_callback(struct gracht_recv_message* message, struct svc_storage_get_descriptor_from_path_args* args)
{
    OsStorageDescriptor_t descriptor = { 0 };
    
    svc_storage_get_descriptor_from_path_response(message, OsNotSupported, &descriptor);
}
