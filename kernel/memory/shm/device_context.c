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
#include <string.h>
#include "device_private.h"

static bool
__ExtentFits(
    _In_ uint64_t base,
    _In_ uint64_t length,
    _In_ uint64_t limit)
{
    // Use subtraction as in deviced's DMA resolver. base + length can wrap
    // when the last byte is UINT64_MAX, even though the extent is valid.
    return length != 0 && base <= limit && length - 1 <= limit - base;
}

static oserr_t
__ValidateRanges(
    _In_ const struct SHMDeviceRange* ranges,
    _In_ uint32_t                     count)
{
    // Validate every range before copying the array. Device ranges must not
    // overlap. Physical ranges may overlap to describe alternate device addresses.
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
    _In_    uint64_t                     addressLimit,
    _InOut_ struct SHMDeviceContext**    contextOut)
{
    struct SHMDeviceContext* context;
    oserr_t                  status;
    size_t                   rangeBytes;

    // The output slot must be empty. Replacing a context would lose its reference.
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

    // SHM_DEVICE_MAX_RANGES keeps this allocation size from wrapping on 32-bit
    // kernels. The context and ranges are allocated and freed together.
    rangeBytes = count * sizeof(struct SHMDeviceRange);
    context = kmalloc(sizeof(struct SHMDeviceContext) + rangeBytes);
    if (context == NULL) {
        return OS_EOOM;
    }

    atomic_init(&context->References, 1);
    MutexConstruct(&context->QuarantineLock, MUTEX_FLAG_PLAIN);
    context->Quarantine = NULL;
    context->CachePolicy = cachePolicy;
    context->AddressLimit = addressLimit;
    context->Count = count;
    memcpy(context->Ranges, ranges, rangeBytes);

    *contextOut = context;
    return OS_EOK;
}

oserr_t
SHMDeviceContextAcquire(
    _In_ struct SHMDeviceContext* context)
{
    unsigned int references;

    // The caller must keep an existing reference valid during this call.
    // Atomic updates allow concurrent callers to add references.
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

    // Clear the caller's slot even when other references remain. Repeated
    // cleanup of this slot must not decrement the reference count again.
    if (context == NULL || *context == NULL) {
        return;
    }

    owned = *context;
    *context = NULL;
    
    if (atomic_fetch_sub(&owned->References, 1) == 1) {
        MutexDestruct(&owned->QuarantineLock);
        kfree(owned);
    }
}

oserr_t
SHMDeviceContextClose(
    _InOut_ struct SHMDeviceContext** context)
{
    // The caller must prevent new mappings while checking the reference count.
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
    // Policy queries are read-only. Mappings handle cache maintenance.
    return context == NULL ? SHMDeviceCacheUnknown : context->CachePolicy;
}

/**
 * @brief Find the longest prefix of a physical extent reachable through one range.
 *
 * Whole-buffer translation and segment building use the same alias selection.
 * Choose the alias that reaches the most bytes to reduce segment splits.
 * For equal lengths, choose the lowest device address. The result is
 * independent of range order.
 *
 * @param length Nonzero, and physicalBase + length - 1 must not wrap.
 * @return Number of bytes reachable from physicalBase, or 0 if no range
 *         contains physicalBase within the context's address limit.
 *         addressOut is only written when the result is nonzero.
 */
static uint64_t
__TranslatePrefix(
    _In_  const struct SHMDeviceContext* context,
    _In_  uint64_t                       physicalBase,
    _In_  uint64_t                       length,
    _Out_ uint64_t*                      addressOut)
{
    const struct SHMDeviceRange* range;
    uint64_t                     deviceLimit = context->AddressLimit;
    uint64_t                     offset;
    uint64_t                     address;
    uint64_t                     last;
    uint64_t                     covered;
    uint64_t                     best = 0;
    uint64_t                     bestAddress = 0;

    for (uint32_t i = 0; i < context->Count; i++) {
        range = &context->Ranges[i];
        if (physicalBase < range->PhysicalBase) {
            continue;
        }

        offset = physicalBase - range->PhysicalBase;
        if (offset >= range->Length) {
            continue;
        }

        // Creation validated this range, so a contained offset cannot overflow
        // its device base. The controller must be able to reach the first byte.
        address = range->DeviceBase + offset;
        if (address > deviceLimit) {
            continue;
        }

        // Use the distance to the final byte. When deviceLimit is UINT64_MAX
        // and address is zero, the available byte count is 2^64 and cannot fit
        // in 64 bits.
        last = length - 1;
        if (last > range->Length - 1 - offset) {
            last = range->Length - 1 - offset;
        }
        if (last > deviceLimit - address) {
            last = deviceLimit - address;
        }

        // last + 1 cannot wrap because last is at most length - 1.
        covered = last + 1;
        if (covered > best || (covered == best && address < bestAddress)) {
            best = covered;
            bestAddress = address;
        }
    }

    if (best != 0) {
        *addressOut = bestAddress;
    }
    return best;
}

oserr_t
SHMDeviceContextTranslate(
    _In_  const struct SHMDeviceContext* context,
    _In_  uint64_t                       physicalBase,
    _In_  uint64_t                       length,
    _Out_ uint64_t*                      deviceAddressOut)
{
    uint64_t address;
    uint64_t covered;

    // Validate the requested extent before subtracting bases or lengths below.
    if (context == NULL || deviceAddressOut == NULL) {
        return OS_EINVALPARAMS;
    }

    if (!__ExtentFits(physicalBase, length, UINT64_MAX)) {
        return OS_EINVALPARAMS;
    }

    covered = __TranslatePrefix(
        context,
        physicalBase,
        length,
        &address
    );

    // A single alias must cover the entire buffer. __TranslatePrefix chooses
    // the lowest device address among aliases that cover it.
    if (covered != length) {
        return OS_ENOENT;
    }
    *deviceAddressOut = address;
    return OS_EOK;
}

/**
 * @brief Check whether a piece can extend the previous device segment.
 *
 * Pieces with consecutive device addresses form one segment even when their
 * physical pages come from separate extents. The combined length must not wrap.
 */
static bool
__ContinuesSegment(
    _In_ const struct DMASegment* previous,
    _In_ uint64_t                 address,
    _In_ uint64_t                 length)
{
    if (address < previous->Address || address - previous->Address != previous->Length) {
        return false;
    }
    return length <= UINT64_MAX - previous->Length;
}

static bool
__SegmentMeetsRequirements(
    _In_ const struct DMASegment*         segment,
    _In_ const struct DMAMapRequirements* requirements)
{
    if (requirements == NULL) {
        return true;
    }
    if (requirements->AddressAlignment != 0 &&
        (segment->Address & (requirements->AddressAlignment - 1)) != 0) {
        return false;
    }
    return requirements->LengthAlignment == 0 ||
        (segment->Length & (requirements->LengthAlignment - 1)) == 0;
}

/**
 * @brief Walk each extent once to count segments and optionally write them.
 *
 * Both passes use the same walk so they split the buffer into the same segments.
 *
 * @param segmentsOut NULL to count only. Otherwise it must hold every segment.
 */
oserr_t
SHMDeviceContextWalkSegments(
    _In_  const struct SHMDeviceContext*   context,
    _In_  const SHMSG_t*                   extents,
    _In_  int                              extentCount,
    _In_  const struct DMAMapRequirements* requirements,
    _Out_ struct DMASegment*               segmentsOut,
    _Out_ uint32_t*                        countOut)
{
    struct DMASegment last = { 0 };
    uint64_t          physical;
    uint64_t          remaining;
    uint64_t          address;
    uint64_t          covered;
    uint32_t          count = 0;

    for (int i = 0; i < extentCount; i++) {
        physical = extents[i].Address;
        remaining = extents[i].Length;

        // Range calculations require a nonempty extent whose last byte does
        // not wrap.
        if (!__ExtentFits(physical, remaining, UINT64_MAX)) {
            return OS_EINVALPARAMS;
        }

        // Each pass of this loop takes the longest piece one range can reach.
        // A piece ends at a range boundary, a hole or the controller limit.
        while (remaining != 0) {
            covered = __TranslatePrefix(context, physical, remaining, &address);
            if (covered == 0) {
                return OS_ENOENT;
            }

            // Keep the last segment locally. The count pass has no output
            // array and must join pieces the same way as the fill pass.
            if (count != 0 && __ContinuesSegment(&last, address, covered)) {
                last.Length += covered;
            } else {
                // A translated piece may join the previous one. Check lengths
                // only once a segment is complete, including the final one below.
                if (count != 0 && !__SegmentMeetsRequirements(&last, requirements)) {
                    return OS_ENOTSUPPORTED;
                }
                if (count == UINT32_MAX) {
                    return OS_ENOTSUPPORTED;
                }
                last = (struct DMASegment){ address, covered };
                count++;
            }
            if (segmentsOut != NULL) {
                segmentsOut[count - 1] = last;
            }

            // After the byte at UINT64_MAX, physical wraps to zero.
            // remaining is then zero, so the loop ends.
            physical += covered;
            remaining -= covered;
        }
    }

    if (!__SegmentMeetsRequirements(&last, requirements)) {
        return OS_ENOTSUPPORTED;
    }
    *countOut = count;
    return OS_EOK;
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

    // The buffer must contain at least one extent.
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

    // Reporting the needed size lets callers allocate exactly once and retry.
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
