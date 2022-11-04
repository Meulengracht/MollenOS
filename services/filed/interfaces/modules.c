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

#define __TRACE

#include <ddk/convert.h>
#include <ddk/utils.h>
#include <os/services/process.h>
#include <os/usched/cond.h>
#include <os/usched/job.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <vfs/interface.h>

#include <gracht/link/vali.h>
#include <internal/_utils.h>
#include <ctt_filesystem_service_client.h>

static const char* g_modulePaths[] = {
        "/modules",
        "/initfs/modules",
        NULL
};

static oserr_t
__DriverInitialize(struct VFSInterface* interface, struct VFSStorageParameters* params, void** instanceData)
{
    struct vali_link_message   msg = VALI_MSG_INIT_HANDLE(interface->DriverID);
    struct ctt_fs_setup_params cttParams;
    oserr_t                    oserr;
    uintptr_t                  fsctx;

    to_fs_setup_params(params, &cttParams);
    ctt_filesystem_setup(GetGrachtClient(), &msg.base, &cttParams);
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    ctt_filesystem_setup_result(GetGrachtClient(), &msg.base, &oserr, &fsctx);
    *instanceData = (void*)fsctx;
    return oserr;
}

static oserr_t
__DriverDestroy(struct VFSInterface* interface, void* instanceData, unsigned int unmountFlags)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(interface->DriverID);
    oserr_t                  oserr;

    ctt_filesystem_destroy(GetGrachtClient(), &msg.base, (uintptr_t)instanceData);
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    ctt_filesystem_destroy_result(GetGrachtClient(), &msg.base, &oserr);
    return oserr;
}

static oserr_t
__DriverOpen(struct VFSInterface* interface, void* instanceData, mstring_t* path, void** dataOut)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(interface->DriverID);
    oserr_t                  oserr;
    uintptr_t                fctx;
    char*                    cpath = mstr_u8(path);

    ctt_filesystem_open(GetGrachtClient(), &msg.base, (uintptr_t)instanceData, cpath);
    free(cpath);
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    ctt_filesystem_open_result(GetGrachtClient(), &msg.base, &oserr, &fctx);
    *dataOut = (void*)fctx;
    return oserr;
}

static oserr_t
__DriverCreate(struct VFSInterface* interface, void* instanceData, void* data, mstring_t* name, uint32_t owner, uint32_t flags, uint32_t permissions, void** dataOut)
{
    struct vali_link_message  msg = VALI_MSG_INIT_HANDLE(interface->DriverID);
    oserr_t                   oserr;
    uintptr_t                 fctx;
    struct ctt_fs_open_params params;

    ctt_fs_open_params_init(&params);
    params.name = mstr_u8(name);
    params.owner = owner;
    params.flags = flags;
    params.permissions = permissions;

    ctt_filesystem_create(GetGrachtClient(), &msg.base, (uintptr_t)instanceData, (uintptr_t)data, &params);
    ctt_fs_open_params_destroy(&params);
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    ctt_filesystem_create_result(GetGrachtClient(), &msg.base, &oserr, &fctx);
    *dataOut = (void*)fctx;
    return oserr;
}

static oserr_t
__DriverClose(struct VFSInterface* interface, void* instanceData, void* data)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(interface->DriverID);
    oserr_t                  oserr;

    ctt_filesystem_close(GetGrachtClient(), &msg.base, (uintptr_t)instanceData, (uintptr_t)data);
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    ctt_filesystem_close_result(GetGrachtClient(), &msg.base, &oserr);
    return oserr;
}

static oserr_t
__DriverStat(struct VFSInterface* interface, void* instanceData, struct VFSStatFS* stats)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(interface->DriverID);
    oserr_t                  oserr;
    struct ctt_fsstat        cttStats;

    ctt_filesystem_fsstat(GetGrachtClient(), &msg.base, (uintptr_t)instanceData);
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    ctt_filesystem_fsstat_result(GetGrachtClient(), &msg.base, &oserr, &cttStats);
    from_fsstat(&cttStats, stats);
    ctt_fsstat_destroy(&cttStats);
    return oserr;
}

static oserr_t
__DriverLink(struct VFSInterface* interface, void* instanceData, void* data, mstring_t* linkName, mstring_t* linkTarget, int symbolic)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(interface->DriverID);
    oserr_t                  oserr;
    char*                    name = mstr_u8(linkName);
    char*                    target = mstr_u8(linkTarget);

    ctt_filesystem_link(GetGrachtClient(), &msg.base, (uintptr_t)instanceData, (uintptr_t)data, name, target, symbolic);
    free(name); free(target);
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    ctt_filesystem_link_result(GetGrachtClient(), &msg.base, &oserr);
    return oserr;
}

static oserr_t
__DriverUnlink(struct VFSInterface* interface, void* instanceData, mstring_t* path)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(interface->DriverID);
    oserr_t                  oserr;
    char*                    cpath = mstr_u8(path);

    ctt_filesystem_unlink(GetGrachtClient(), &msg.base, (uintptr_t)instanceData, cpath);
    free(cpath);
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    ctt_filesystem_unlink_result(GetGrachtClient(), &msg.base, &oserr);
    return oserr;
}

static oserr_t
__DriverReadlink(struct VFSInterface* interface, void* instanceData, mstring_t* path, mstring_t** pathOut)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(interface->DriverID);
    oserr_t                  oserr;
    char*                    cpath = mstr_u8(path);
    char*                    buffer = malloc(_MAXPATH);

    ctt_filesystem_readlink(GetGrachtClient(), &msg.base, (uintptr_t)instanceData, cpath);
    free(cpath);
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    ctt_filesystem_readlink_result(GetGrachtClient(), &msg.base, &oserr, buffer, _MAXPATH);
    *pathOut = mstr_new_u8(buffer);
    free(buffer);
    return oserr;
}

static oserr_t
__DriverMove(struct VFSInterface* interface, void* instanceData, mstring_t* from, mstring_t* to, int copy)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(interface->DriverID);
    oserr_t                  oserr;
    char*                    cfrom = mstr_u8(from);
    char*                    cto = mstr_u8(to);

    ctt_filesystem_move(GetGrachtClient(), &msg.base, (uintptr_t)instanceData, cfrom, cto, copy);
    free(cfrom); free(cto);
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    ctt_filesystem_move_result(GetGrachtClient(), &msg.base, &oserr);
    return oserr;
}

static oserr_t
__DriverRead(struct VFSInterface* interface, void* instanceData, void* data, uuid_t bufferHandle, size_t bufferOffset, size_t unitCount, size_t* unitsRead)
{
    struct vali_link_message      msg = VALI_MSG_INIT_HANDLE(interface->DriverID);
    oserr_t                       oserr;
    struct ctt_fs_transfer_params params;
    uint64_t                      read;

    params.buffer_id = bufferHandle;
    params.offset = bufferOffset;
    params.count = unitCount;

    ctt_filesystem_read(GetGrachtClient(), &msg.base, (uintptr_t)instanceData, (uintptr_t)data, &params);
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    ctt_filesystem_read_result(GetGrachtClient(), &msg.base, &oserr, &read);
    *unitsRead = read;
    return oserr;
}

static oserr_t
__DriverWrite(struct VFSInterface* interface, void* instanceData, void* data, uuid_t bufferHandle, size_t bufferOffset, size_t unitCount, size_t* unitsWritten)
{
    struct vali_link_message      msg = VALI_MSG_INIT_HANDLE(interface->DriverID);
    oserr_t                       oserr;
    struct ctt_fs_transfer_params params;
    uint64_t                      written;

    params.buffer_id = bufferHandle;
    params.offset = bufferOffset;
    params.count = unitCount;

    ctt_filesystem_write(GetGrachtClient(), &msg.base, (uintptr_t)instanceData, (uintptr_t)data, &params);
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    ctt_filesystem_write_result(GetGrachtClient(), &msg.base, &oserr, &written);
    *unitsWritten = (size_t)written;
    return oserr;
}

static oserr_t
__DriverTruncate(struct VFSInterface* interface, void* instanceData, void* data, uint64_t size)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(interface->DriverID);
    oserr_t                  oserr;

    ctt_filesystem_truncate(GetGrachtClient(), &msg.base, (uintptr_t)instanceData, (uintptr_t)data, size);
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    ctt_filesystem_truncate_result(GetGrachtClient(), &msg.base, &oserr);
    return oserr;
}

static oserr_t
__DriverSeek(struct VFSInterface* interface, void* instanceData, void* data, uint64_t absolutePosition, uint64_t* absolutePositionOut)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(interface->DriverID);
    oserr_t                  oserr;

    ctt_filesystem_seek(GetGrachtClient(), &msg.base, (uintptr_t)instanceData, (uintptr_t)data, absolutePosition);
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    ctt_filesystem_seek_result(GetGrachtClient(), &msg.base, &oserr, absolutePositionOut);
    return oserr;
}

struct VFSInterface*
VFSInterfaceNew(
        _In_  Handle_t              dllHandle,
        _In_  struct VFSOperations* operations)
{
    struct VFSInterface* interface;

    interface = (struct VFSInterface*)malloc(sizeof(struct VFSInterface));
    if (!interface) {
        return NULL;
    }

    interface->Handle = dllHandle;
    usched_mtx_init(&interface->Lock);
    if (operations) {
        memcpy(&interface->Operations, operations, sizeof(struct VFSOperations));
    }
    return interface;
}

struct __DetachedContext {
    // params
    const char* Type;

    // return
    uuid_t ProcessID;
};

static struct __DetachedContext* __DetachedContext_new(const char* type)
{
    struct __DetachedContext* context = malloc(sizeof(struct __DetachedContext));
    if (context == NULL) {
        return NULL;
    }
    context->Type   = type;
    context->Handle = HANDLE_INVALID;
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
    uuid_t                    processID;
    ENTRY("__TryLocateModule()");

    i = 0;
    while (g_modulePaths[i] && !usched_is_cancelled(cancellationToken)) {
        snprintf(&tmp[0], sizeof(tmp), "%s/%s.dll", g_modulePaths[i], context->Type);
        oserr_t oserr = ProcessSpawn(&tmp[0], NULL, &processID);
        if (oserr == OS_EOK) {
            context->ProcessID = processID;
            break;
        }
        i++;
    }
    EXIT("__TryLocateModule");
}

static Handle_t __RunDetached(
        _In_  const char* type)
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
        _In_  const char*           type,
        _Out_ struct VFSInterface** interfaceOut)
{
    struct VFSInterface* interface;
	Handle_t             handle;
    oserr_t              osStatus;
    TRACE("VFSInterfaceLoadInternal(%s)", type);

	if (type == NULL) {
	    return OS_ENOTSUPPORTED;
	}

    // Until such a time
    handle = __RunDetached(type);
    if (handle == HANDLE_INVALID) {
        ERROR("VFSInterfaceLoadInternal failed to load %s", type);
        return OS_ENOENT;
    }

    interface = VFSInterfaceNew(handle, NULL);
    if (interface == NULL) {
        return OS_EOOM;
    }

    osStatus = __LoadInternalAPI(interface, handle);
    if (osStatus != OS_EOK) {
        VFSInterfaceDelete(interface);
        return osStatus;
    }

    *interfaceOut = interface;
    return OS_EOK;
}

oserr_t
VFSInterfaceLoadDriver(
        _In_  uuid_t                 interfaceDriverID,
        _Out_ struct VFSInterface**  interfaceOut)
{
    // TODO implement this
    return OS_ENOTSUPPORTED;
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
