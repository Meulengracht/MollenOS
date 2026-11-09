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
#include <internal/_syscalls.h>
#include <os/device.h>
#include <os/types/syscall.h>
#include <threads.h>
#include <stdlib.h>

#include "private.h"

// Only open, close and exceptional rollback touch this registry. Transfers and
// child allocation use their context directly, without a global lookup or lock.
static once_flag          g_dmaOnce = ONCE_FLAG_INIT;
static mtx_t              g_dmaContextLock;
static struct DMAContext* g_dmaContexts;

static void
__Initialize(void)
{
    TRACE("__Initialize: initializing DMA context registry");
    mtx_init(&g_dmaContextLock, mtx_plain);
}

oserr_t
DMAOpen(
    _In_    uuid_t              device,
    _In_    uint64_t            addressLimit,
    _InOut_ struct DMAContext** contextOut)
{
    struct OSDmaOpenParameters parameters = { .AddressLimit = addressLimit };
    struct DMAContext*         context;
    oserr_t                    status;
    oserr_t                    releaseStatus;

    TRACE("DMAOpen(device=%u, addressLimit=0x%llx)", device, (unsigned long long)addressLimit);
    if (contextOut == NULL || device == UUID_INVALID) {
        WARNING("DMAOpen: missing context output or invalid device %u", device);
        return OS_EINVALPARAMS;
    }
    if (*contextOut != NULL) {
        TRACE("DMAOpen: device %u requires an empty context output", device);
        return OS_EBUSY;
    }

    // The provider recovers lost replies by returning the same session. Prevent
    // two local contexts from sharing that lease and releasing it independently.
    call_once(&g_dmaOnce, __Initialize);
    mtx_lock(&g_dmaContextLock);

    for (struct DMAContext* existing = g_dmaContexts; existing != NULL; existing = existing->Next) {
        if (existing->Device == device) {
            TRACE("DMAOpen: device %u already has context %u, session %llu (closing=%d)",
                  device, existing->Handle, (unsigned long long)existing->Session, existing->Closing);
            mtx_unlock(&g_dmaContextLock);
            return OS_EBUSY;
        }
    }

    context = calloc(1, sizeof(*context));
    if (context == NULL) {
        ERROR("DMAOpen: context allocation failed for device %u", device);
        mtx_unlock(&g_dmaContextLock);
        return OS_EOOM;
    }
    atomic_init(&context->Children, 0);
    context->Device = device;
    context->Handle = UUID_INVALID;

    status = OSDeviceIOCtl(
        device,
        OSIOCTLREQUEST_DMA_QUERY,
        &parameters.Configuration,
        sizeof(parameters.Configuration)
    );
    if (status != OS_EOK) {
        ERROR("DMAOpen: OSDeviceIOCtl DMA_QUERY device %u failed: %u", device, status);
        goto error;
    }

    context->Session = parameters.Configuration.Session;
    TRACE("DMAOpen: device %u acquired session %llu, ranges %u, cache policy %u",
          device, (unsigned long long)context->Session,
          parameters.Configuration.Count, parameters.Configuration.CachePolicy);
    
    status = Syscall_DmaOpen(device, &parameters, &context->Handle);
    if (status != OS_EOK) {
        ERROR("DMAOpen: Syscall_DmaOpen device %u, session %llu failed: %u",
              device, (unsigned long long)context->Session, status);
        goto error;
    }

    context->Next = g_dmaContexts;
    g_dmaContexts = context;
    *contextOut = context;
    
    TRACE("DMAOpen: device %u opened context %u, session %llu",
          device, context->Handle, (unsigned long long)context->Session);
    mtx_unlock(&g_dmaContextLock);
    return OS_EOK;

error:
    // A failed open can still own a provider lease. Publish a cleanup-only
    // context if rollback fails, so normal teardown can retry that release.
    if (context->Session != 0) {
        releaseStatus = OSDeviceIOCtl(
            device,
            OSIOCTLREQUEST_DMA_RELEASE,
            &context->Session,
            sizeof(context->Session)
        );
        if (releaseStatus != OS_EOK) {
            ERROR("DMAOpen: OSDeviceIOCtl DMA_RELEASE device %u, session %llu rollback failed: %u (open status %u); close retained context",
                  device, (unsigned long long)context->Session, releaseStatus, status);
            
                  context->Closing = true;
            context->Next = g_dmaContexts;
            g_dmaContexts = context;
            
            *contextOut = context;
            mtx_unlock(&g_dmaContextLock);
            return status;
        }
    }

    free(context);
    mtx_unlock(&g_dmaContextLock);
    return status;
}

void
__DmaDeferCleanup(
    _In_ struct __DmaResource* resource)
{
    // Creation never exposes an incomplete object to the driver. Preserve a
    // failed rollback here instead of losing its kernel handle or SHM owner.
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

    TRACE("DMAClose(contextSlot=%p)", contextSlot);
    if (contextSlot == NULL || *contextSlot == NULL) {
        return OS_EOK;
    }

    context = *contextSlot;
    mtx_lock(&g_dmaContextLock);
    TRACE("DMAClose: device %u, context %u, session %llu (closing=%d)",
          context->Device, context->Handle, (unsigned long long)context->Session, context->Closing);

    // Retry unpublished children before checking live owners. A buffer still
    // counts after its mapping is gone but its SHM detach has failed.
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

    // Keep the object through both teardown stages. Creation cannot reuse a
    // closing domain, and a provider retry must never repeat kernel close.
    context->Closing = true;
    if (context->Handle != UUID_INVALID) {
        status = Syscall_ResourceRelease(context->Handle);
        if (status != OS_EOK) {
            ERROR("DMAClose: Syscall_ResourceRelease device %u, context %u failed: %u; cleanup remains pending",
                  context->Device, context->Handle, status);
            goto exit;
        }
        context->Handle = UUID_INVALID;
    }

    status = OSDeviceIOCtl(
        context->Device,
        OSIOCTLREQUEST_DMA_RELEASE,
        &context->Session,
        sizeof(context->Session)
    );
    if (status != OS_EOK) {
        ERROR("DMAClose: OSDeviceIOCtl DMA_RELEASE device %u, session %llu failed: %u; cleanup remains pending",
              context->Device, (unsigned long long)context->Session, status);
        goto exit;
    }
    
    for (link = &g_dmaContexts; *link != context; link = &(*link)->Next) {
        // The owning context remains registered until both releases succeed.
    }
    *link = context->Next;
    
    TRACE("DMAClose: released device %u", context->Device);
    free(context);
    *contextSlot = NULL;

exit:
    mtx_unlock(&g_dmaContextLock);
    return status;
}
