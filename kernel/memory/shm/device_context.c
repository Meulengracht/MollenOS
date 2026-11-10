/**
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
 */

#include <heap.h>
#include <limits.h>
#include "device_private.h"
#include "../dma/private.h"

oserr_t
SHMDeviceContextCreate(
    _In_    const struct SHMDeviceRange* ranges,
    _In_    uint32_t                     count,
    _In_    enum SHMDeviceCachePolicy    cachePolicy,
    _In_    uint64_t                     addressLimit,
    _InOut_ struct SHMDeviceContext**    contextOut)
{
    struct SHMDeviceContext* context;
    oserr_t                  status;
    struct DMAAddressSpace*  space = NULL;

    // *contextOut must be NULL. Replacing a live context would leak its reference.
    if (contextOut == NULL) {
        return OS_EINVALPARAMS;
    }

    if (*contextOut != NULL) {
        return OS_EBUSY;
    }

    if (ranges == NULL || count == 0) {
        return OS_EINVALPARAMS;
    }

    if (count > SHM_DEVICE_MAX_RANGES) {
        return OS_ENOTSUPPORTED;
    }

    if (cachePolicy != SHMDeviceCacheNonCoherent && cachePolicy != SHMDeviceCacheCoherent) {
        return OS_EINVALPARAMS;
    }

    status = DMAAddressSpaceCreateDirect(ranges, count, &space);
    if (status != OS_EOK) {
        return status;
    }

    // Each address mapping holds a reference to the address space, so its
    // address rules remain valid until the last mapping releases them.
    context = kmalloc(sizeof(struct SHMDeviceContext));
    if (context == NULL) {
        DMAAddressSpaceRelease(&space);
        return OS_EOOM;
    }

    atomic_init(&context->References, 1);
    MutexConstruct(&context->QuarantineLock, MUTEX_FLAG_PLAIN);
    context->Quarantine = NULL;
    context->CachePolicy = cachePolicy;
    context->AddressLimit = addressLimit;
    context->AddressSpace = space;

    *contextOut = context;
    return OS_EOK;
}

oserr_t
SHMDeviceContextAcquire(
    _In_ struct SHMDeviceContext* context)
{
    unsigned int references;

    // The caller must hold a reference until this call returns.
    // Atomic updates allow concurrent callers to add references.
    if (context == NULL) {
        return OS_EINVALPARAMS;
    }
    
    references = atomic_load(&context->References);
    do {
        // A count overflow could free the context while it is still in use.
        if (references == UINT_MAX) {
            return OS_EOVERFLOW;
        }
    } while (!atomic_compare_exchange_weak(&context->References, &references, references + 1));
    return OS_EOK;
}

void
SHMDeviceContextRelease(
    _InOut_ struct SHMDeviceContext** context)
{
    struct SHMDeviceContext* owned;

    // Clear the caller's pointer even if other references remain. Repeated
    // cleanup through this pointer must not decrement the count again.
    if (context == NULL || *context == NULL) {
        return;
    }

    owned = *context;
    *context = NULL;
    
    if (atomic_fetch_sub(&owned->References, 1) == 1) {
        DMAAddressSpaceRelease(&owned->AddressSpace);
        MutexDestruct(&owned->QuarantineLock);
        kfree(owned);
    }
}

oserr_t
SHMDeviceContextClose(
    _InOut_ struct SHMDeviceContext** context)
{
    // The caller must prevent new mappings and references during this check.
    if (atomic_load(&(*context)->References) != 1) {
        return OS_EBUSY;
    }
    SHMDeviceContextRelease(context);
    return OS_EOK;
}

enum SHMDeviceCachePolicy
SHMDeviceContextGetCachePolicy(
    _In_ const struct SHMDeviceContext* context)
{
    // Reading the policy does not synchronize caches.
    return context == NULL ? SHMDeviceCacheUnknown : context->CachePolicy;
}

// These queries calculate addresses from fixed ranges. They do not create
// mappings or reserve device addresses.
oserr_t
SHMDeviceContextTranslate(
    _In_  const struct SHMDeviceContext* context,
    _In_  uint64_t                       physicalBase,
    _In_  uint64_t                       length,
    _Out_ uint64_t*                      deviceAddressOut)
{
    if (context == NULL) {
        return OS_EINVALPARAMS;
    }
    
    return DMADirectTranslate(
        context->AddressSpace,
        context->AddressLimit,
        physicalBase,
        length,
        deviceAddressOut
    );
}

oserr_t
SHMDeviceContextWalkSegments(
    _In_  const struct SHMDeviceContext*   context,
    _In_  const SHMSG_t*                   extents,
    _In_  int                              extentCount,
    _In_  const struct DMAMapRequirements* requirements,
    _Out_ struct DMASegment*               segmentsOut,
    _Out_ uint32_t*                        countOut)
{
    return DMADirectWalkSegments(
        context->AddressSpace,
        context->AddressLimit,
        extents,
        extentCount,
        requirements,
        segmentsOut,
        countOut
    );
}

oserr_t
SHMDeviceContextBuildSegments(
    _In_    const struct SHMDeviceContext* context,
    _In_    const SHMSG_t*                 extents,
    _In_    int                            extentCount,
    _InOut_ uint32_t*                      segmentCount,
    _Out_   struct DMASegment*             segmentsOut)
{
    uint32_t required;
    oserr_t  status;

    // At least one physical byte range is required.
    if (context == NULL || segmentCount == NULL) {
        return OS_EINVALPARAMS;
    }
    if (extents == NULL || extentCount <= 0) {
        return OS_EINVALPARAMS;
    }

    // Count first to validate the whole buffer before writing segments.
    // Invalid input must not leave a partially filled list.
    status = SHMDeviceContextWalkSegments(
        context,
        extents,
        extentCount,
        NULL,
        NULL,
        &required
    );
    if (status != OS_EOK) {
        return status;
    }

    // Return the required count so the caller can allocate an array of that size.
    if (segmentsOut == NULL) {
        *segmentCount = required;
        return OS_EOK;
    }
    if (*segmentCount < required) {
        *segmentCount = required;
        return OS_EBUFFER;
    }

    status = SHMDeviceContextWalkSegments(
        context,
        extents,
        extentCount,
        NULL,
        segmentsOut,
        &required
    );
    if (status != OS_EOK) {
        return status;
    }

    *segmentCount = required;
    return OS_EOK;
}
