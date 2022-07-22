/**
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * File Manager Service
 * - Handles all file related services and disk services
 */

//#define __TRACE

#include <ddk/utils.h>
#include <os/sharedobject.h>
#include <stdlib.h>
#include <string.h>
#include "vfs/vfs_interface.h"

static const char* g_fsModules[] = {
	NULL,
	"$sys/drivers/filesystems/fat.dll",
	"$sys/drivers/filesystems/exfat.dll",
	"$sys/drivers/filesystems/ntfs.dll",
	"$sys/drivers/filesystems/hfs.dll",
	"$sys/drivers/filesystems/hpfs.dll",
	"mfs.dll",
	"$sys/drivers/filesystems/ext.dll"
};

static oserr_t
__LoadInternalAPI(
        _In_ struct VFSInterface* interface,
        _In_ Handle_t             handle)
{
    TRACE("__LoadInternalAPI()");

    interface->Operations.Initialize = (FsInitialize_t)SharedObjectGetFunction(handle, "FsInitialize");
    interface->Operations.Destroy    = (FsDestroy_t)SharedObjectGetFunction(handle, "FsDestroy");
    interface->Operations.Stat       = (FsStat_t)SharedObjectGetFunction(handle, "FsStat");

    interface->Operations.Open     = (FsOpen_t)SharedObjectGetFunction(handle, "FsOpen");
    interface->Operations.Close    = (FsClose_t)SharedObjectGetFunction(handle, "FsClose");
    interface->Operations.Link     = (FsLink_t)SharedObjectGetFunction(handle, "FsLink");
    interface->Operations.Unlink   = (FsUnlink_t)SharedObjectGetFunction(handle, "FsUnlink");
    interface->Operations.ReadLink = (FsReadLink_t)SharedObjectGetFunction(handle, "FsReadLink");
    interface->Operations.Create   = (FsCreate_t)SharedObjectGetFunction(handle, "FsCreate");
    interface->Operations.Move     = (FsMove_t)SharedObjectGetFunction(handle, "FsMove");
    interface->Operations.Truncate = (FsTruncate_t)SharedObjectGetFunction(handle, "FsTruncate");
    interface->Operations.Read     = (FsRead_t)SharedObjectGetFunction(handle, "FsRead");
    interface->Operations.Write    = (FsWrite_t)SharedObjectGetFunction(handle, "FsWrite");
    interface->Operations.Seek     = (FsSeek_t)SharedObjectGetFunction(handle, "FsSeek");

    // Sanitize required functions
    if (interface->Operations.Open == NULL) {
        WARNING("__LoadInternalAPI FsOpen is required, was not present");
        return OsNotSupported;
    }

    if (interface->Operations.Read == NULL) {
        WARNING("__LoadInternalAPI FsRead is required, was not present");
        return OsNotSupported;
    }

    if (interface->Operations.Close == NULL) {
        WARNING("__LoadInternalAPI FsClose is required, was not present");
        return OsNotSupported;
    }
    return OsOK;
}

struct VFSInterface*
VFSInterfaceNew(
        _In_  enum FileSystemType   type,
        _In_  Handle_t              dllHandle,
        _In_  struct VFSOperations* operations)
{
    struct VFSInterface* interface;

    interface = (struct VFSInterface*)malloc(sizeof(struct VFSInterface));
    if (!interface) {
        return NULL;
    }

    interface->Type   = type;
    interface->Handle = dllHandle;
    usched_mtx_init(&interface->Lock);
    if (operations) {
        memcpy(&interface->Operations, operations, sizeof(struct VFSOperations));
    }
    return interface;
}

oserr_t
VFSInterfaceLoadInternal(
        _In_  enum FileSystemType   type,
        _Out_ struct VFSInterface** interfaceOut)
{
    struct VFSInterface* interface;
	Handle_t             handle;
    oserr_t              osStatus;
    TRACE("VFSInterfaceLoadInternal(%u)", type);

	if (type == FileSystemType_UNKNOWN) {
	    return OsNotSupported;
	}

    handle = SharedObjectLoad(g_fsModules[(int)type]);
    if (handle == HANDLE_INVALID) {
        ERROR("VFSInterfaceLoadInternal failed to load %s", g_fsModules[(int)type]);
        return OsError;
    }

    interface = VFSInterfaceNew(type, handle, NULL);
    if (interface == NULL) {
        return OsOutOfMemory;
    }

    osStatus = __LoadInternalAPI(interface, handle);
    if (osStatus != OsOK) {
        VFSInterfaceDelete(interface);
        return osStatus;
    }

    *interfaceOut = interface;
    return OsOK;
}

void
VFSInterfaceDelete(
        _In_ struct VFSInterface* interface)
{
    if (!interface) {
        return;
    }

    if (interface->Handle != NULL) {
        SharedObjectUnload(interface->Handle);
    }
    free(interface);
}
