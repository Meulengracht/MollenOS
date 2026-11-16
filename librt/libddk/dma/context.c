/**
 * Copyright 2023, Philip Meulengracht
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

//#define __TRACE

#include <ddk/utils.h>
#include <ddk/service.h>
#include <gracht/link/vali.h>
#include <sys_device_service_client.h>
#include <internal/_syscalls.h>
#include <internal/_utils.h>
#include <os/device.h>
#include <os/types/syscall.h>
#include <threads.h>
#include <stdlib.h>

#include "private.h"

// Only open, close and the cleanup of failed creations touch this registry.
// Transfers and the creation of buffers and mappings use their context directly,
// so they need no global lookup or lock.
static once_flag          g_dmaOnce = ONCE_FLAG_INIT;
static mtx_t              g_dmaContextLock;
static struct DMAContext* g_dmaContexts;
static uint64_t           g_dmaRequestId;

static void
__Initialize(void)
{
    TRACE("__Initialize: initializing DMA context registry");
    mtx_init(&g_dmaContextLock, mtx_plain);
}

// Asks the device service for a binding, which is the permission to open a DMA
// context for this device. A transport error does not tell us whether the service
// granted it. decoded is set to true only when a reply was received and read, so
// the caller can tell a clear rejection from an unknown result.
static oserr_t
__RequestBinding(
    _InOut_ struct DMAContext* context,
    _Out_   bool*              decoded)
{
    struct vali_link_message message = VALI_MSG_INIT_HANDLE(GetDeviceService());
    OSDmaBindingResult_t     binding = { 0 };
    oserr_t                  result = OS_EPROTOCOL;
    int                      status;

    *decoded = false;

    status = sys_device_dma_bind(GetGrachtClient(), &message.base, context->Device, context->RequestId);
    if (status != 0) {
        WARNING("__RequestBinding: device %u send failed: %i", context->Device, status);
        return OS_EPROTOCOL;
    }

    status = gracht_client_await(GetGrachtClient(), &message.base, GRACHT_AWAIT_ASYNC);
    if (status != 0) {
        WARNING("__RequestBinding: device %u wait failed: %i", context->Device, status);
        return OS_EPROTOCOL;
    }

    status = sys_device_dma_bind_result(
        GetGrachtClient(),
        &message.base,
        &result,
        &binding.Handle,
        &binding.Generation
    );
    if (status != 0) {
        WARNING("__RequestBinding: device %u decode failed: %i", context->Device, status);
        return OS_EPROTOCOL;
    }

    if (result == OS_EOK && (binding.Handle == UUID_INVALID || binding.Generation == 0)) {
        ERROR("__RequestBinding: device %u received an invalid binding", context->Device);
        return OS_EPROTOCOL;
    }

    *decoded = true;
    if (result != OS_EOK) {
        WARNING("__RequestBinding: device %u bind rejected: %u", context->Device, result);
        return result;
    }

    context->Binding = binding;
    context->BindingPending = false;
    return OS_EOK;
}

// Releases the binding recorded on this context. Only used for cleanup. A context
// with no binding recorded has nothing left to release, which does not mean the
// open succeeded.
static oserr_t
__ReleaseBinding(
    _InOut_ struct DMAContext* context)
{
    oserr_t status;

    if (context->Binding.Handle == UUID_INVALID) {
        return OS_EOK;
    }

    status = Syscall_HandleDestroy(context->Binding.Handle);
    if (status != OS_EOK && status != OS_EINCOMPLETE && status != OS_ENOENT) {
        ERROR("__ReleaseBinding: device %u binding %u cleanup failed: %u",
              context->Device, context->Binding.Handle, status);
        return status;
    }

    memset(&context->Binding, 0, sizeof(OSDmaBindingResult_t));
    return OS_EOK;
}

oserr_t
DMAOpen(
    _In_    uuid_t              device,
    _In_    uint64_t            addressLimit,
    _InOut_ struct DMAContext** contextOut)
{
    OSDmaBoundOpenParameters_t parameters = { .AddressLimit = addressLimit };
    struct DMAContext*        context;
    struct DMAContext*        existing;
    oserr_t                   status;
    oserr_t                   cleanupStatus;
    bool                      decoded;
    uuid_t                    handle = UUID_INVALID;

    if (contextOut == NULL || device == UUID_INVALID) {
        WARNING("DMAOpen: missing context output or invalid device %u", device);
        return OS_EINVALPARAMS;
    }
    if (*contextOut != NULL) {
        return OS_EBUSY;
    }
    call_once(&g_dmaOnce, __Initialize);
    mtx_lock(&g_dmaContextLock);
    for (existing = g_dmaContexts; existing != NULL; existing = existing->Next) {
        if (existing->Device == device) {
            mtx_unlock(&g_dmaContextLock);
            return OS_EBUSY;
        }
    }

    // The values 0 and UINT64_MAX are reserved by the binding protocol. When the
    // counter runs out, every later open fails. An ID is never reused, because the
    // service could mistake a new request for an older one.
    if (g_dmaRequestId >= UINT64_MAX - 1) {
        mtx_unlock(&g_dmaContextLock);
        return OS_EOVERFLOW;
    }

    context = calloc(1, sizeof(struct DMAContext));
    if (context == NULL) {
        ERROR("DMAOpen: context allocation failed for device %u", device);
        mtx_unlock(&g_dmaContextLock);
        return OS_EOOM;
    }

    atomic_init(&context->Children, 0);
    context->Device = device;
    context->RequestId = ++g_dmaRequestId;
    context->Handle = UUID_INVALID;
    context->Binding.Handle = UUID_INVALID;

    status = __RequestBinding(context, &decoded);
    if (status != OS_EOK) {
        if (!decoded) {
            // The service may have granted the binding even though the reply was
            // lost. Keep the request ID so DMAClose can ask again and get the
            // same binding back.
            context->BindingPending = true;
            goto pending;
        }
        goto discard;
    }

    parameters.Generation = context->Binding.Generation;
    status = Syscall_DmaOpenBound(
        context->Binding.Handle,
        &parameters,
        &handle
    );
    if (status != OS_EOK) {
        ERROR("DMAOpen: device %u bound context open failed: %u", device, status);
        cleanupStatus = __ReleaseBinding(context);
        if (cleanupStatus != OS_EOK) {
            goto pending;
        }
        goto discard;
    }

    // The kernel can write a value to handle and still fail the open. The value
    // is only a real context handle after the call returns OS_EOK, and only then
    // will cleanup try to release it.
    context->Handle = handle;
    goto publish;

pending:
    // This context only exists to finish cleanup. It keeps the device's place in
    // the registry until every unresolved or failed cleanup step is done, so a new
    // open for the same device is refused until then. It cannot allocate or map
    // memory.
    context->Closing = true;

publish:
    context->Next = g_dmaContexts;
    g_dmaContexts = context;
    *contextOut = context;
    mtx_unlock(&g_dmaContextLock);
    return status;

discard:
    free(context);
    mtx_unlock(&g_dmaContextLock);
    return status;
}

void
__DmaDeferCleanup(
    _In_ struct __DmaResource* resource)
{
    // A failed creation never hands a half-built object to the driver. If undoing
    // it also fails, keep the object here so its kernel handle and SHM attachment
    // are not lost.
    mtx_lock(&g_dmaContextLock);
    WARNING("__DmaDeferCleanup: device %u, context %u, mapping %u deferring resource cleanup",
            resource->Context->Device, resource->Context->Handle, resource->Handle);
    resource->Next = resource->Context->Pending;
    resource->Context->Pending = resource;
    mtx_unlock(&g_dmaContextLock);
}

oserr_t
DMAClose(
    _InOut_ struct DMAContext** contextSlot)
{
    struct DMAContext** link;
    struct DMAContext*  context;
    oserr_t             status;
    unsigned int        children;
    bool                decoded;

    TRACE("DMAClose(contextSlot=%p)", contextSlot);
    if (contextSlot == NULL || *contextSlot == NULL) {
        return OS_EOK;
    }

    context = *contextSlot;
    mtx_lock(&g_dmaContextLock);
    // Retry the cleanup of failed creations first, because they still count as
    // children. A buffer also still counts after its mapping is gone if detaching
    // its SHM buffer failed.
    status = __DmaCollect(context);
    if (status != OS_EOK) {
        ERROR("DMAClose: __DmaCollect device %u, context %u failed: %u; pending resources retained",
              context->Device, context->Handle, status);
        goto exit;
    }
    
    children = atomic_load(&context->Children);
    if (children != 0) {
        TRACE("DMAClose: device %u, context %u still owns %u children",
              context->Device, context->Handle, children);
        status = OS_EBUSY;
        goto exit;
    }

    // Each cleanup step is marked done as soon as it succeeds, so a retry resumes
    // where the last try stopped. A normal close sends no request to the device
    // service. Only a context whose binding reply was lost sends the original
    // request again.
    context->Closing = true;
    if (context->BindingPending) {
        status = __RequestBinding(context, &decoded);
        if (status != OS_EOK) {
            // Even a clear rejection does not prove the earlier request failed,
            // because that request may have been granted. Keep the cleanup context
            // until the request is resolved or the process exits.
            goto exit;
        }
    }
    
    if (context->Handle != UUID_INVALID) {
        status = Syscall_ResourceRelease(context->Handle);
        if (status != OS_EOK) {
            ERROR("DMAClose: Syscall_ResourceRelease device %u, context %u failed: %u; cleanup remains pending",
                  context->Device, context->Handle, status);
            goto exit;
        }
        context->Handle = UUID_INVALID;
    }

    status = __ReleaseBinding(context);
    if (status != OS_EOK) {
        goto exit;
    }

    for (link = &g_dmaContexts; *link != context; link = &(*link)->Next) {
        // Find the link that points to this context so it can be unlinked.
    }
    *link = context->Next;
    
    TRACE("DMAClose: released device %u", context->Device);
    free(context);
    *contextSlot = NULL;

exit:
    mtx_unlock(&g_dmaContextLock);
    return status;
}
