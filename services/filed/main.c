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
 */

#include <internal/_ipc.h>
#include <vfs/filesystem.h>
#include <vfs/scope.h>
#include <vfs/storage.h>
#include <vfs/vfs.h>

#include <sys_file_service_server.h>
#include <sys_mount_service_server.h>
#include <sys_storage_service_server.h>

extern gracht_server_t* __crt_get_service_server(void);

oserr_t OnUnload(void)
{
    return OsOK;
}

void GetServiceAddress(struct ipmsg_addr* address)
{
    address->type = IPMSG_ADDRESS_PATH;
    address->data.path = SERVICE_FILE_PATH;
}

oserr_t OnLoad(void)
{
    // Initialize subsystems
    VFSNodeHandleInitialize();
    VFSStorageInitialize();
    VfsFileSystemInitialize();
    VFSScopeInitialize();

    // Register supported interfaces
    gracht_server_register_protocol(__crt_get_service_server(), &sys_file_server_protocol);
    gracht_server_register_protocol(__crt_get_service_server(), &sys_mount_server_protocol);
    gracht_server_register_protocol(__crt_get_service_server(), &sys_storage_server_protocol);
    return OsOK;
}
