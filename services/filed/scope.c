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
#include <vfs/vfs.h>
#include <vfs/vfs_interface.h>

static struct VFS*          g_rootScope      = NULL;
static guid_t               g_rootGuid       = GUID_EMPTY;
static mstring_t            g_globalName     = mstr_const(U"vfs-root");
static struct VFSCommonData g_rootCommonData = { 0 };

static oserr_t
__NewMemFS(
        _In_  mstring_t*           label,
        _In_  guid_t*              guid,
        _In_ struct VFSCommonData* vfsCommonData,
        _Out_ struct VFS**         vfsOut)
{
    struct VFSInterface* interface;
    oserr_t              osStatus;
    _CRT_UNUSED(label); // TODO missing support for setting this, where should we do it

    interface = MemFSNewInterface();
    if (interface == NULL) {
        return OsOutOfMemory;
    }

    osStatus = VFSNew(UUID_INVALID, guid, interface, vfsCommonData, vfsOut);
    if (osStatus != OsOK) {
        VFSInterfaceDelete(interface);
    }

    // Normally the FileSystem interface will take care of initializing for us,
    // but when dealing with our VFS* directly, we have to take care of initializing
    // and destructing manually
    if (interface->Operations.Initialize) {
        osStatus = interface->Operations.Initialize(vfsCommonData);
        if (osStatus != OsOK) {
            VFSInterfaceDelete(interface);
        }
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

    osStatus = __NewMemFS(
            &g_globalName, &g_rootGuid,
            &g_rootCommonData, &g_rootScope);
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
