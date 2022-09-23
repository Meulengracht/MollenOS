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
#include <os/usched/cond.h>
#include <os/usched/job.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <vfs/vfs_interface.h>

static const char* g_modulePaths[] = {
        "/modules",
        "/initfs/modules",
        NULL
};

static const char* g_moduleNames[] = {
	NULL,
	"fat.dll",
	"exfat.dll",
	"ntfs.dll",
	"hfs.dll",
	"hpfs.dll",
	"mfs.dll",
	"ext.dll"
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

struct __DetachedContext {
    // params
    enum FileSystemType Type;

    // return
    Handle_t Handle;
};

static struct __DetachedContext* __DetachedContext_new(enum FileSystemType type)
{
    struct __DetachedContext* context = malloc(sizeof(struct __DetachedContext));
    if (context == NULL) {
        return NULL;
    }
    context->Type   = type;
    context->Handle = UUID_INVALID;
    return context;
}

static void __DetachedContext_delete(struct __DetachedContext* context)
{
    free(context);
}

static void
__TryLocateModule(
        _In_ void* argument,
        _In_ void* cancellationToken)
{
    struct __DetachedContext* context = argument;
    char                      tmp[256];
    int                       i;

    i = 0;
    while (g_modulePaths[i]) {
        snprintf(&tmp[0], sizeof(tmp), "%s/%s", g_modulePaths[i], g_moduleNames[(int)context->Type]);
        Handle_t handle = SharedObjectLoad(&tmp[0]);
        if (handle != HANDLE_INVALID) {
            context->Handle = handle;
            return;
        }
        i++;
    }
}

static Handle_t __RunDetached(
        _In_ enum FileSystemType type)
{
    struct usched_job_parameters jobParameters;
    struct __DetachedContext*    context;
    uuid_t                       jobID;
    int                          exitCode;
    Handle_t                     handle;

    context = __DetachedContext_new(type);
    if (context == NULL) {
        return HANDLE_INVALID;
    }

    usched_job_parameters_init(&jobParameters);
    usched_job_parameters_set_detached(&jobParameters, true);
    jobID = usched_job_queue3(__TryLocateModule, context, &jobParameters);
    if (usched_job_join(jobID, &exitCode)) {
        ERROR("__RunDetached failed to join on job %u: %i", jobID, errno);
    }
    handle = context->Handle;
    __DetachedContext_delete(context);
    return handle;
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

    // Until such a time
    handle = __RunDetached(type);
    if (handle == HANDLE_INVALID) {
        ERROR("VFSInterfaceLoadInternal failed to load %s", g_moduleNames[(int)type]);
        return OsNotExists;
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
