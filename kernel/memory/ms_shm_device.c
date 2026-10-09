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

//#define __TRACE

#include <shm_device.h>
#include <heap.h>
#include <limits.h>
#include <string.h>

/**
 * @brief Keep copied address rules alive independently of the caller's storage.
 *
 * Only References changes after creation, so translations need no lock while
 * the caller owns a reference. Ranges contains exactly Count entries.
 */
struct SHMDeviceContext {
    _Atomic(unsigned int)     References;
    enum SHMDeviceCachePolicy CachePolicy;
    uint32_t                  Count;
    struct SHMDeviceRange     Ranges[];
};

static bool
__ExtentFits(
    _In_ uint64_t base,
    _In_ uint64_t length,
    _In_ uint64_t limit)
{
    // Use the same subtraction-based check as deviced's DMA resolver. Adding
    // base and length could wrap, and would reject a valid final byte at MAX.
    return length != 0 && base <= limit && length - 1 <= limit - base;
}

static oserr_t
__ValidateRanges(
    _In_ const struct SHMDeviceRange* ranges,
    _In_ uint32_t                     count)
{
    // Validate all entries before keeping any of them. A device address must
    // have one meaning, but physical overlaps are valid alternative addresses.
    for (uint32_t i = 0; i < count; i++) {
        if (!__ExtentFits(ranges[i].PhysicalBase, ranges[i].Length, UINT64_MAX) ||
            !__ExtentFits(ranges[i].DeviceBase, ranges[i].Length, UINT64_MAX)) {
            return OS_EINVALPARAMS;
        }

        for (uint32_t j = 0; j < i; j++) {
            uint64_t first = ranges[i].DeviceBase;
            uint64_t second = ranges[j].DeviceBase;
            if (first <= second ? second - first < ranges[i].Length :
                                  first - second < ranges[j].Length) {
                return OS_EINVALPARAMS;
            }
        }
    }
    return OS_EOK;
}

oserr_t
SHMDeviceContextCreate(
    _In_    const struct SHMDeviceRange* ranges,
    _In_    uint32_t                     count,
    _In_    enum SHMDeviceCachePolicy    cachePolicy,
    _InOut_ struct SHMDeviceContext**    contextOut)
{
    struct SHMDeviceContext* context;
    oserr_t                  status;
    size_t                   rangeBytes;

    // Preserve an occupied slot: replacing it would lose its owning reference.
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

    status = __ValidateRanges(ranges, count);
    if (status != OS_EOK) {
        return status;
    }

    // The small range-count bound also keeps this allocation size from wrapping
    // on 32-bit kernels. One allocation makes failure and final release simple.
    rangeBytes = count * sizeof(struct SHMDeviceRange);
    context = kmalloc(sizeof(struct SHMDeviceContext) + rangeBytes);
    if (context == NULL) {
        return OS_EOOM;
    }

    atomic_init(&context->References, 1);
    context->CachePolicy = cachePolicy;
    context->Count = count;
    memcpy(context->Ranges, ranges, rangeBytes);

    *contextOut = context;
    return OS_EOK;
}

oserr_t
SHMDeviceContextRetain(
    _In_ struct SHMDeviceContext* context)
{
    unsigned int references;

    // As with the kernel's interrupt references, an existing owner protects
    // this pointer. Atomic updates let separate owners take references safely.
    if (context == NULL) {
        return OS_EINVALPARAMS;
    }
    
    references = atomic_load(&context->References);
    do {
        // Wrapping the count would allow release to free a still-used context.
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

    // Clear the owner's slot even when other references survive, so retrying
    // cleanup on this slot cannot accidentally release someone else's owner.
    if (context == NULL || *context == NULL) {
        return;
    }

    owned = *context;
    *context = NULL;
    
    if (atomic_fetch_sub(&owned->References, 1) == 1) {
        kfree(owned);
    }
}

enum SHMDeviceCachePolicy
SHMDeviceContextGetCachePolicy(
    _In_ const struct SHMDeviceContext* context)
{
    // Keep policy queries read-only; cache handling belongs to actual mappings.
    return context == NULL ? SHMDeviceCacheUnknown : context->CachePolicy;
}

oserr_t
SHMDeviceContextTranslate(
    _In_  const struct SHMDeviceContext* context,
    _In_  uint64_t                       physicalBase,
    _In_  uint64_t                       length,
    _In_  uint64_t                       deviceLimit,
    _Out_ uint64_t*                      deviceAddressOut)
{
    const struct SHMDeviceRange* range;
    uint64_t                    offset;
    uint64_t                    address;
    uint64_t                    selected = UINT64_MAX;
    uint32_t                    i;
    bool                        found = false;

    // Validate the requested extent before subtracting bases or lengths below.
    if (context == NULL || deviceAddressOut == NULL) {
        return OS_EINVALPARAMS;
    }
    if (!__ExtentFits(physicalBase, length, UINT64_MAX)) {
        return OS_EINVALPARAMS;
    }

    for (i = 0; i < context->Count; i++) {
        range = &context->Ranges[i];
        if (physicalBase < range->PhysicalBase) {
            continue;
        }
        
        offset = physicalBase - range->PhysicalBase;
        if (offset >= range->Length || length > range->Length - offset) {
            continue;
        }

        // Creation validated this range, so a contained offset cannot overflow
        // its device base. Check the controller limit on the whole buffer too.
        address = range->DeviceBase + offset;
        if (!__ExtentFits(address, length, deviceLimit)) {
            continue;
        }

        if (!found || address < selected) {
            selected = address;
            found = true;
        }
    }
    if (!found) {
        return OS_ENOENT;
    }

    // Publish once, after trying every alias, so failure never returns a
    // partial result and input order never changes the chosen device address.
    *deviceAddressOut = selected;
    return OS_EOK;
}
