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
#include "vfs/vfs_module.h"

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
        _In_ struct VFSModule* module,
        _In_ Handle_t          handle)
{
    TRACE("__LoadInternalAPI()");

    module->Operations.Initialize = (FsInitialize_t)SharedObjectGetFunction(handle, "FsInitialize");
    module->Operations.Destroy    = (FsDestroy_t)SharedObjectGetFunction(handle, "FsDestroy");
    module->Operations.Stat       = (FsStat_t)SharedObjectGetFunction(handle, "FsStat");

    module->Operations.Open     = (FsOpen_t)SharedObjectGetFunction(handle, "FsOpen");
    module->Operations.Close    = (FsClose_t)SharedObjectGetFunction(handle, "FsClose");
    module->Operations.Link     = (FsLink_t)SharedObjectGetFunction(handle, "FsLink");
    module->Operations.Unlink   = (FsUnlink_t)SharedObjectGetFunction(handle, "FsUnlink");
    module->Operations.ReadLink = (FsReadLink_t)SharedObjectGetFunction(handle, "FsReadLink");
    module->Operations.Create   = (FsCreate_t)SharedObjectGetFunction(handle, "FsCreate");
    module->Operations.Move     = (FsMove_t)SharedObjectGetFunction(handle, "FsMove");
    module->Operations.Truncate = (FsTruncate_t)SharedObjectGetFunction(handle, "FsTruncate");
    module->Operations.Read     = (FsRead_t)SharedObjectGetFunction(handle, "FsRead");
    module->Operations.Write    = (FsWrite_t)SharedObjectGetFunction(handle, "FsWrite");
    module->Operations.Seek     = (FsSeek_t)SharedObjectGetFunction(handle, "FsSeek");

    // Sanitize required functions
    if (module->Operations.Open == NULL) {
        WARNING("__LoadInternalAPI FsOpen is required, was not present");
        return OsNotSupported;
    }

    if (module->Operations.Read == NULL) {
        WARNING("__LoadInternalAPI FsRead is required, was not present");
        return OsNotSupported;
    }

    if (module->Operations.Close == NULL) {
        WARNING("__LoadInternalAPI FsClose is required, was not present");
        return OsNotSupported;
    }
    return OsOK;
}

struct VFSModule*
VFSModuleNew(
        _In_  enum FileSystemType   type,
        _In_  Handle_t              dllHandle,
        _In_  struct VFSOperations* operations)
{
    struct VFSModule* module;

    module = (struct VFSModule*)malloc(sizeof(struct VFSModule));
    if (!module) {
        return NULL;
    }

    module->Type   = type;
    module->Handle = dllHandle;
    usched_mtx_init(&module->Lock);
    if (operations) {
        memcpy(&module->Operations, operations, sizeof(struct VFSOperations));
    }
    return module;
}

oserr_t
VFSModuleLoadInternal(
        _In_  enum FileSystemType type,
        _Out_ struct VFSModule**  moduleOut)
{
    struct VFSModule* module;
	Handle_t          handle;
    oserr_t           osStatus;
    TRACE("VFSModuleLoadInternal(%u)", type);

	if (type == FileSystemType_UNKNOWN) {
	    return OsNotSupported;
	}

    handle = SharedObjectLoad(g_fsModules[(int)type]);
    if (handle == HANDLE_INVALID) {
        ERROR("VFSModuleLoadInternal failed to load %s", g_fsModules[(int)type]);
        return OsError;
    }

    module = VFSModuleNew(type, handle, NULL);
    if (module == NULL) {
        return OsOutOfMemory;
    }

    osStatus = __LoadInternalAPI(module, handle);
    if (osStatus != OsOK) {
        VFSModuleDelete(module);
        return osStatus;
    }

    *moduleOut = module;
    return OsOK;
}

void
VFSModuleDelete(
        _In_ struct VFSModule* module)
{
    if (!module) {
        return;
    }

    if (module->Handle != NULL) {
        SharedObjectUnload(module->Handle);
    }
    free(module);
}
