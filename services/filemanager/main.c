/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS - File Manager Service
 * - Handles all file related services and disk services
 */
//#define __TRACE

#include <ctype.h>
#include <ddk/utils.h>
#include <ds/collection.h>
#include "include/vfs.h"
#include <internal/_ipc.h>
#include <stdlib.h>
#include <string.h>

#include "svc_file_protocol_server.h"
#include "svc_path_protocol_server.h"
#include "svc_storage_protocol_server.h"

// Static storage for the filemanager
static int          DiskTable[__FILEMANAGER_MAXDISKS] = { 0 };
static Collection_t ResolveQueue    = COLLECTION_INIT(KeyId);
static Collection_t FileSystems     = COLLECTION_INIT(KeyId);
static Collection_t OpenHandles     = COLLECTION_INIT(KeyId);
static Collection_t OpenFiles       = COLLECTION_INIT(KeyId);
static Collection_t Modules         = COLLECTION_INIT(KeyId);
static Collection_t Disks           = COLLECTION_INIT(KeyId);

//static UUId_t FileSystemIdGenerator = 0;
static UUId_t FileIdGenerator = 0;

Collection_t*
VfsGetOpenFiles(void) {
    return &OpenFiles;
}

Collection_t*
VfsGetOpenHandles(void) {
    return &OpenHandles;
}

Collection_t*
VfsGetModules(void) {
    return &Modules;
}

Collection_t*
VfsGetDisks(void) {
    return &Disks;
}

Collection_t*
VfsGetFileSystems(void) {
    return &FileSystems;
}

Collection_t*
VfsGetResolverQueue(void) {
    return &ResolveQueue;
}

UUId_t
VfsIdentifierFileGet(void) {
    return FileIdGenerator++;
}

UUId_t
VfsIdentifierAllocate(
    _In_ FileSystemDisk_t *Disk)
{
    int ArrayStartIndex = 0;
    int ArrayEndIndex   = 0;
    int i;

    // Start out by determing start index
    ArrayEndIndex = __FILEMANAGER_MAXDISKS / 2;
    if (Disk->Flags & SVC_STORAGE_REGISTER_FLAGS_REMOVABLE) {
        ArrayStartIndex = __FILEMANAGER_MAXDISKS / 2;
        ArrayEndIndex = __FILEMANAGER_MAXDISKS;
    }

    // Now iterate the range for the type of disk
    for (i = ArrayStartIndex; i < ArrayEndIndex; i++) {
        if (DiskTable[i] == 0) {
            DiskTable[i] = 1;
            return (UUId_t)(i - ArrayStartIndex);
        }
    }
    return UUID_INVALID;
}

OsStatus_t
VfsIdentifierFree(
    _In_ FileSystemDisk_t   *Disk,
    _In_ UUId_t              Id)
{
    int ArrayIndex = (int)Id;
    if (Disk->Flags & SVC_STORAGE_REGISTER_FLAGS_REMOVABLE) {
        ArrayIndex += __FILEMANAGER_MAXDISKS / 2;
    }
    if (ArrayIndex < __FILEMANAGER_MAXDISKS) {
        DiskTable[ArrayIndex] = 0;
        return OsSuccess;
    }
    else {
        return OsError;
    }
}

OsStatus_t OnUnload(void)
{
    return OsSuccess;
}

void GetServiceAddress(struct ipmsg_addr* address)
{
    address->type = IPMSG_ADDRESS_PATH;
    address->data.path = SERVICE_FILE_PATH;
}

OsStatus_t
OnLoad(void)
{
    // Register supported interfaces
    gracht_server_register_protocol(&svc_file_protocol);
    gracht_server_register_protocol(&svc_path_protocol);
    gracht_server_register_protocol(&svc_storage_protocol);
    return OsSuccess;
}
