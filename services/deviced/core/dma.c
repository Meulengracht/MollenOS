/**
 * MollenOS
 *
 * Copyright, Philip Meulengracht
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

#include <ddk/utils.h>
#include <os/types/dma.h>
#include <os/types/device.h>
#include <os/usched/mutex.h>
#include <stdlib.h>
#include <string.h>

#include <core/dma.h>

struct __DmaSession {
    struct __DmaSession* Next;
    uuid_t Device;
    uuid_t Principal;
    uint64_t Token;
    struct DmDmaLease* Lease;
};

static struct usched_mtx    g_dmaSessionsLock;
static struct __DmaSession* g_dmaSessions;
static uint64_t             g_dmaSessionToken;

void
DmDmaSessionsInitialize(void)
{
    usched_mtx_init(&g_dmaSessionsLock, USCHED_MUTEX_PLAIN);
}

oserr_t
DmDmaControl(
    _In_    uuid_t              principal,
    _In_    uuid_t              device,
    _In_    enum OSIOCtlRequest request,
    _InOut_ void*               buffer,
    _In_    size_t              length)
{
    struct __DmaSession**          link;
    struct __DmaSession*           session;
    const struct DmDmaDescription* description;
    struct OSDmaConfiguration*     result = buffer;
    oserr_t                        status = OS_EOK;

    if (principal == UUID_INVALID) {
        ERROR("DmDmaControl: device %u request %u has an invalid principal (%u)",
              device, request, OS_EINVALPARAMS);
        return OS_EINVALPARAMS;
    }

    if (request == OSIOCTLREQUEST_DMA_QUERY &&
        (buffer == NULL || length != sizeof(*result))) {
        ERROR("DmDmaControl: device %u principal %u DMA query has buffer %p length %zu, expected %zu (%u)",
              device, principal, buffer, length, sizeof(*result), OS_EINVALPARAMS);
        return OS_EINVALPARAMS;
    }
    
    if (request == OSIOCTLREQUEST_DMA_RELEASE &&
        (buffer == NULL || length != sizeof(uint64_t))) {
        ERROR("DmDmaControl: device %u principal %u DMA release has buffer %p length %zu, expected %zu (%u)",
              device, principal, buffer, length, sizeof(uint64_t), OS_EINVALPARAMS);
        return OS_EINVALPARAMS;
    }

    // This mutex is separate from the device registry lock. DMA preparation may
    // block and must not call back into the DMA session protocol.
    usched_mtx_lock(&g_dmaSessionsLock);
    
    for (link = &g_dmaSessions; *link != NULL; link = &(*link)->Next) {
        if ((*link)->Device == device) {
            break;
        }
    }
    session = *link;
    
    if (session != NULL && session->Principal != principal) {
        WARNING("DmDmaControl: device %u request %u principal %u conflicts with session principal %u (%u)",
                device, request, principal, session->Principal, OS_EBUSY);
        status = OS_EBUSY;
        goto done;
    }

    if (request == OSIOCTLREQUEST_DMA_RELEASE) {
        if (session != NULL && session->Token == *(uint64_t*)buffer) {
            *link = session->Next;
            DmDmaLeaseRelease(&session->Lease);
            free(session);
        }
        goto done;
    }

    if (session == NULL) {
        if (g_dmaSessionToken == UINT64_MAX) {
            ERROR("DmDmaControl: device %u principal %u has no available DMA session tokens (%u)",
                  device, principal, OS_EOOM);
            status = OS_EOOM;
            goto done;
        }
        
        session = calloc(1, sizeof(struct __DmaSession));
        if (session == NULL) {
            ERROR("DmDmaControl: failed to allocate a DMA session for device %u principal %u (%u)",
                  device, principal, OS_EOOM);
            status = OS_EOOM;
            goto done;
        }
        
        status = DmDevicePrepareDmaForPrincipal(device, principal, &session->Lease);
        if (status != OS_EOK) {
            ERROR("DmDmaControl: failed to prepare DMA for device %u principal %u (%u)",
                  device, principal, status);
            free(session);
            goto done;
        }

        session->Device = device;
        session->Principal = principal;
        session->Token = ++g_dmaSessionToken;
        *link = session;
    }

    description = DmDmaLeaseGetDescription(session->Lease);
    
    memset(result, 0, sizeof(struct OSDmaConfiguration));
    
    if (description->Map.Count > DMA_MAX_RANGES) {
        ERROR("DmDmaControl: device %u principal %u DMA map has %u ranges, limit is %u (%u)",
              device, principal, description->Map.Count, (unsigned int)DMA_MAX_RANGES,
              OS_ENOTSUPPORTED);
        status = OS_ENOTSUPPORTED;
        goto done;
    }
    
    result->Count = description->Map.Count;
    result->Session = session->Token;
    result->CachePolicy = (enum SHMDeviceCachePolicy)description->CachePolicy;

    for (unsigned int i = 0; i < result->Count; i++) {
        result->Ranges[i].PhysicalBase = description->Map.Ranges[i].PhysicalBase;
        result->Ranges[i].DeviceBase = description->Map.Ranges[i].DeviceBase;
        result->Ranges[i].Length = description->Map.Ranges[i].Length;
    }

done:
    usched_mtx_unlock(&g_dmaSessionsLock);
    return status;
}
