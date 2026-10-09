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

/**
 * @brief Find how many leading bytes of a physical extent one range can reach.
 *
 * Both whole-buffer translation and segment building need the same alias
 * choice, so they share this search instead of keeping two copies of the rules.
 * The alias reaching the most bytes wins, because it needs the fewest segments;
 * among equally long choices the lowest device address wins, so the result
 * never depends on the order the ranges were supplied in.
 *
 * @param length Nonzero, and physicalBase + length - 1 must not wrap.
 * @return Number of bytes reachable from physicalBase, or 0 if no range
 *         contains physicalBase within deviceLimit. addressOut is only
 *         written when the result is nonzero.
 */
static uint64_t
__TranslatePrefix(
    _In_  const struct SHMDeviceContext* context,
    _In_  uint64_t                       physicalBase,
    _In_  uint64_t                       length,
    _In_  uint64_t                       deviceLimit,
    _Out_ uint64_t*                      addressOut)
{
    const struct SHMDeviceRange* range;
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

        // Work with the distance to the final byte rather than a byte count:
        // a block reaching the top of the address space has a count of 2^64,
        // which does not fit in 64 bits, while its final-byte distance does.
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
    _In_  uint64_t                       deviceLimit,
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
        deviceLimit,
        &address
    );

    // The caller asked for one address valid for the entire buffer, so an
    // alias that only reaches part of it is not an answer. When an alias does
    // reach everything, the longest-first rule picks the lowest such address.
    if (covered != length) {
        return OS_ENOENT;
    }
    *deviceAddressOut = address;
    return OS_EOK;
}

/**
 * @brief Return whether a new piece starts exactly where the previous one ends.
 *
 * The device only cares about its own addresses, so pieces that continue each
 * other there become one segment even if their physical pages came from
 * separate extents. The length check keeps the joined length from wrapping.
 */
static bool
__ContinuesSegment(
    _In_ const struct SHMDeviceSegment* previous,
    _In_ uint64_t                       address,
    _In_ uint64_t                       length)
{
    if (address < previous->Address || address - previous->Address != previous->Length) {
        return false;
    }
    return length <= UINT64_MAX - previous->Length;
}

/**
 * @brief Walk every extent once, either counting segments or also writing them.
 *
 * Using a single walk for both passes guarantees the count pass and the fill
 * pass always agree on how the buffer is split.
 *
 * @param segmentsOut NULL to only count; otherwise must hold every segment.
 */
static oserr_t
__WalkSegments(
    _In_  const struct SHMDeviceContext* context,
    _In_  const SHMSG_t*                 extents,
    _In_  int                            extentCount,
    _In_  uint64_t                       deviceLimit,
    _Out_ struct SHMDeviceSegment*       segmentsOut,
    _Out_ uint32_t*                      countOut)
{
    struct SHMDeviceSegment last = { 0 };
    uint64_t                physical;
    uint64_t                remaining;
    uint64_t                address;
    uint64_t                covered;
    uint32_t                count = 0;

    for (int i = 0; i < extentCount; i++) {
        physical = extents[i].Address;
        remaining = extents[i].Length;

        // An empty or wrapping extent cannot describe real memory, and the
        // per-range arithmetic below relies on neither being possible.
        if (!__ExtentFits(physical, remaining, UINT64_MAX)) {
            return OS_EINVALPARAMS;
        }

        // Each pass of this loop takes the longest piece one range can reach.
        // A piece ends at a range boundary, a hole or the controller limit.
        while (remaining != 0) {
            covered = __TranslatePrefix(context, physical, remaining, deviceLimit, &address);
            if (covered == 0) {
                return OS_ENOENT;
            }

            // Track the latest segment locally so the count pass, which has no
            // output array, joins pieces exactly like the fill pass does.
            if (count != 0 && __ContinuesSegment(&last, address, covered)) {
                last.Length += covered;
            } else {
                if (count == UINT32_MAX) {
                    return OS_ENOTSUPPORTED;
                }
                last = (struct SHMDeviceSegment){ address, covered };
                count++;
            }
            if (segmentsOut != NULL) {
                segmentsOut[count - 1] = last;
            }

            // Reaching the very top of the address space may wrap physical to
            // zero, but remaining becomes zero at the same time and ends the loop.
            physical += covered;
            remaining -= covered;
        }
    }

    *countOut = count;
    return OS_EOK;
}

oserr_t
SHMDeviceContextBuildSegments(
    _In_    const struct SHMDeviceContext* context,
    _In_    const SHMSG_t*                 extents,
    _In_    int                            extentCount,
    _In_    uint64_t                       deviceLimit,
    _InOut_ uint32_t*                      segmentCount,
    _Out_   struct SHMDeviceSegment*       segmentsOut)
{
    uint32_t required;
    oserr_t  status;

    // An empty buffer has nothing to hand to a device, so treat it as a bug.
    if (context == NULL || segmentCount == NULL) {
        return OS_EINVALPARAMS;
    }
    if (extents == NULL || extentCount <= 0) {
        return OS_EINVALPARAMS;
    }

    // Always count first. This validates the whole buffer before anything is
    // written, so a failure never leaves a half-filled list behind.
    status = __WalkSegments(
        context,
        extents,
        extentCount,
        deviceLimit,
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

    status = __WalkSegments(
        context,
        extents,
        extentCount,
        deviceLimit,
        segmentsOut,
        &required
    );
    if (status != OS_EOK) {
        return status;
    }

    *segmentCount = required;
    return OS_EOK;
}
