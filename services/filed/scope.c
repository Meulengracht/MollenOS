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

#include <ddk/utils.h>
#include <os/types/file.h>
#include <vfs/interface.h>
#include <vfs/storage.h>
#include <vfs/vfs.h>

static struct VFS*          g_rootScope      = NULL;
static guid_t               g_rootGuid       = GUID_EMPTY;
static mstring_t            g_globalName     = mstr_const(U"vfs-root");

static oserr_t
__NewMemFS(
        _In_  mstring_t*           label,
        _In_  guid_t*              guid,
        _Out_ struct VFS**         vfsOut)
{
    struct VFSStorageParameters storageParameters;
    struct VFSInterface*        interface;
    struct VFSStorage*          storage;
    oserr_t                     osStatus;
    void*                       interfaceData = NULL;
    _CRT_UNUSED(label); // TODO missing support for setting this, where should we do it

    interface = MemFSNewInterface();
    if (interface == NULL) {
        return OsOutOfMemory;
    }

    // Create a new, empty memory backend which the memory-fs will
    // be bound to. MemFS does not actually use the storage backend, but we
    // have subsystems which will inquire about storage geometry.
    storage = VFSStorageCreateMemoryBacked(UUID_INVALID, 0, NULL, 0);
    if (storage == NULL) {
        VFSInterfaceDelete(interface);
        return OsOutOfMemory;
    }

    // Normally the FileSystem interface will take care of initializing for us,
    // but when dealing with our VFS* directly, we have to take care of initializing
    // and destructing manually
    if (interface->Operations.Initialize) {
        osStatus = interface->Operations.Initialize(&storageParameters, &interfaceData);
        if (osStatus != OsOK) {
            VFSInterfaceDelete(interface);
            VFSStorageDelete(storage);
            return osStatus;
        }
    }

    osStatus = VFSNew(
            UUID_INVALID,
            guid,
            storage,
            interface,
            interfaceData,
            vfsOut
    );
    if (osStatus != OsOK) {
        VFSInterfaceDelete(interface);
        VFSStorageDelete(storage);
    }
    return osStatus;
}

static oserr_t
__MountDefaultDirectories(void)
{
    struct VFSNode* node;
    mstring_t       storage = mstr_const(U"/storage/");
    TRACE("__MountDefaultDirectories()");

    // Mount the storage folder, this is the responsibility of the storage
    // manager, which is this service for the time being
    return VFSNodeNewDirectory(
            g_rootScope, &storage,
            FILE_PERMISSION_READ | FILE_PERMISSION_OWNER_WRITE, &node);
}

void VFSScopeInitialize(void)
{
    oserr_t osStatus;

    osStatus = __NewMemFS(&g_globalName, &g_rootGuid, &g_rootScope);
    if (osStatus != OsOK) {
        ERROR("VFSScopeInitialize failed to create root filesystem scope");
        return;
    }

    osStatus = __MountDefaultDirectories();
    if (osStatus != OsOK) {
        ERROR("VFSScopeInitialize failed to mount default directories: %u", osStatus);
    }
}

struct VFS*
VFSScopeGet(
        _In_ uuid_t processId)
{
    // TODO implement filesystem scopes based on processId
    return g_rootScope;
}
