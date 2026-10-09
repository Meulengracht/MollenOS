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
#include <arch/utils.h>
#include <ddk/barrier.h>
#include <handle.h>
#include <heap.h>
#include <limits.h>
#include <shm.h>
#include <string.h>
#include "private.h"

struct SHMDeviceContext {
    _Atomic(unsigned int)     References;
    enum SHMDeviceCachePolicy CachePolicy;
    uint64_t                  AddressLimit;
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
    _In_    uint64_t                     addressLimit,
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
            covered = __TranslatePrefix(context, physical, remaining, &address);
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
        segmentsOut,
        &required
    );
    if (status != OS_EOK) {
        return status;
    }

    *segmentCount = required;
    return OS_EOK;
}

/**
 * Temporary memory a device uses instead of the buffer itself. Copy is zero
 * when the mapping uses the buffer directly. Both views are kernel mappings,
 * so the sync calls can copy between them with plain memory copies.
 */
struct SHMDeviceBounce {
    vaddr_t Source;       // Kernel view of the buffer's pages.
    size_t  SourceLength;
    size_t  SourceOffset; // Where the mapped range starts inside that view.
    vaddr_t Copy;         // Kernel view of the bounce pages; the range starts at 0.
    size_t  CopyLength;
};

/**
 * Remember which part of a buffer a device may use, and where.
 * Everything except DeviceOwned is fixed at creation. DeviceOwned is only
 * changed by the sync calls, which the mapping's owner must not overlap.
 * Segments contains exactly SegmentCount entries. Extents holds the same bytes
 * as physical blocks, which the sync calls need for cache work. When bouncing,
 * both describe the bounce pages, since those are what the device uses.
 */
struct SHMDeviceMapping {
    uuid_t                   SHMID;
    struct SHMDeviceContext* Context;
    size_t                   Offset;
    size_t                   Length;
    enum SHMDeviceDirection  Direction;
    bool                     DeviceOwned;
    struct SHMDeviceBounce   Bounce;
    SHMSG_t*                 Extents;
    int                      ExtentCount;
    uint32_t                 SegmentCount;
    struct SHMDeviceSegment  Segments[];
};

static oserr_t
__CheckBufferKind(
    _In_ const struct SHMBuffer* buffer)
{
    // Exported buffers wrap memory SHM does not own, 
    // so SHM cannot use it for device memory.
    if (buffer->Exported) {
        return OS_ENOTSUPPORTED;
    }
    return OS_EOK;
}

/**
 * @brief Tell whether cache work for the range would reach bytes outside it.
 *
 * When a device that cannot see the CPU cache writes memory, cached lines are
 * thrown away, always as whole lines. A line shared with bytes outside the
 * range could hold CPU changes to those bytes, which would be lost, so such a
 * range must be bounced. Coherent devices need no cache work, and a device
 * that only reads only causes write-backs, which never lose data.
 */
static bool
__SharesCacheLines(
    _In_ const struct SHMBuffer*        buffer,
    _In_ const struct SHMDeviceContext* context,
    _In_ size_t                         offset,
    _In_ size_t                         length,
    _In_ enum SHMDeviceDirection        direction)
{
    size_t line;
    size_t start;

    if (context->CachePolicy == SHMDeviceCacheCoherent || direction == SHMDeviceToDevice) {
        return false;
    }

    // Pages start on a line boundary, so the position inside the buffer
    // decides alignment.
    line = CpuDataCacheLineSize();
    start = buffer->Offset + offset;
    return start % line != 0 || length % line != 0;
}

/**
 * @brief Describe exactly the requested bytes of a page list as physical blocks.
 *
 * SHMBuildSG cannot be reused here: it reports missing pages as address zero
 * and lets its last entry run to the end of the final page. A device must only
 * ever see the requested bytes, and only memory that really exists, so this
 * walks the page list itself. Neighbouring pages that are also next to each
 * other in physical memory are joined, as SHMBuildSG does, to keep lists short.
 * Used for both buffer pages and bounce pages.
 *
 * @param start First byte, counted from the start of the first page.
 * @param extents Room for one entry per page touched by the range.
 * @return OS_EOK, or OS_EINCOMPLETE if a page in the range was never allocated.
 */
static oserr_t
__GatherExtents(
    _In_  const paddr_t* pages,
    _In_  size_t         start,
    _In_  size_t         length,
    _In_  size_t         pageSize,
    _Out_ SHMSG_t*       extents,
    _Out_ int*           countOut)
{
    size_t  page = start / pageSize;
    size_t  pageOffset = start % pageSize;
    size_t  remaining = length;
    size_t  chunk;
    paddr_t address;
    int     count = 0;

    while (remaining != 0) {
        // SHM marks a page that was never allocated with address zero.
        if (pages[page] == 0) {
            return OS_EINCOMPLETE;
        }

        address = pages[page] + pageOffset;
        chunk = pageSize - pageOffset;
        if (chunk > remaining) {
            chunk = remaining;
        }

        if (count != 0 && extents[count - 1].Address + extents[count - 1].Length == address) {
            extents[count - 1].Length += chunk;
        } else {
            extents[count] = (SHMSG_t){ .Address = address, .Length = chunk };
            count++;
        }

        remaining -= chunk;
        pageOffset = 0;
        page++;
    }

    *countOut = count;
    return OS_EOK;
}

static void
__ReleaseBounce(
    _In_ struct SHMDeviceBounce* bounce)
{
    // The bounce pages belong to this view and are freed with it. The buffer
    // view is persistent, so removing it leaves the buffer's pages alone.
    if (bounce->Copy != 0) {
        (void)MemorySpaceUnmap(GetCurrentMemorySpace(), bounce->Copy, bounce->CopyLength);
        bounce->Copy = 0;
    }
    if (bounce->Source != 0) {
        (void)MemorySpaceUnmap(GetCurrentMemorySpace(), bounce->Source, bounce->SourceLength);
        bounce->Source = 0;
    }
}

/**
 * @brief Give the kernel its own view of the buffer's pages for copying.
 *
 * The process view of the buffer belongs to whichever process mapped it and
 * may be gone, so the copies need a view in kernel memory that lives as long
 * as the mapping. This is the same kind of view SHM already keeps for IPC
 * buffers.
 */
static oserr_t
__MapSourceView(
    _In_    struct SHMBuffer*       buffer,
    _In_    size_t                  offset,
    _In_    size_t                  length,
    _In_    size_t                  pageSize,
    _InOut_ struct SHMDeviceBounce* bounce)
{
    size_t       start = buffer->Offset + offset;
    unsigned int flags = MAPPING_COMMIT | MAPPING_PERSISTENT;

    // An uncached buffer must not get a cached kernel view: the two could
    // then disagree about what is in memory.
    if (SHM_KIND(buffer->Flags) == SHM_DEVICE) {
        flags |= MAPPING_NOCACHE;
    }

    bounce->SourceOffset = start % pageSize;
    bounce->SourceLength = DIVUP(bounce->SourceOffset + length, pageSize) * pageSize;
    return MemorySpaceMap(
        GetCurrentMemorySpace(),
        &(struct MemorySpaceMapOptions) {
            .SHMTag = buffer->ID,
            .Pages = &buffer->Pages[start / pageSize],
            .Length = bounce->SourceLength,
            .Mask = buffer->PageMask,
            .Flags = flags,
            .PlacementFlags = MAPPING_PHYSICAL_FIXED | MAPPING_VIRTUAL_GLOBAL
        },
        &bounce->Source
    );
}

/**
 * @brief Allocate bounce pages and describe them as physical blocks.
 *
 * Low memory is requested because it is the most likely to be reachable by a
 * device with an address limit; the caller still checks reachability. Pages
 * are zeroed, so a device never reads data left behind by someone else.
 *
 * @param extents Room for one entry per bounce page; overwritten on success.
 */
static oserr_t
__AllocateCopy(
    _In_    size_t                  length,
    _In_    size_t                  pageSize,
    _InOut_ struct SHMDeviceBounce* bounce,
    _Out_   SHMSG_t*                extents,
    _Out_   int*                    extentCountOut)
{
    paddr_t* pages;
    size_t   pageCount = DIVUP(length, pageSize);
    size_t   mask;
    oserr_t  status;

    status = ArchSHMTypeToPageMask(OSMEMORYCONFORMITY_LOW, &mask);
    if (status != OS_EOK) {
        return status;
    }

    // MemorySpaceMap reports the pages it allocated through this array.
    pages = kmalloc(pageCount * sizeof(paddr_t));
    if (pages == NULL) {
        return OS_EOOM;
    }

    bounce->CopyLength = pageCount * pageSize;
    status = MemorySpaceMap(
        GetCurrentMemorySpace(),
        &(struct MemorySpaceMapOptions) {
            .Pages = pages,
            .Length = bounce->CopyLength,
            .Mask = mask,
            .Flags = MAPPING_COMMIT | MAPPING_CLEAN,
            .PlacementFlags = MAPPING_VIRTUAL_GLOBAL
        },
        &bounce->Copy
    );
    if (status == OS_EOK) {
        status = __GatherExtents(pages, 0, length, pageSize, extents, extentCountOut);
    }
    kfree(pages);
    return status;
}

/**
 * @brief Switch a mapping-to-be over to bounce pages.
 *
 * Used when the device cannot reach part of the buffer, or when cache work
 * would reach bytes outside the range. The bounce pages start on a page
 * boundary and belong to the mapping alone, so neither problem applies to
 * them. On success extents describes the bounce pages instead of the buffer.
 */
static oserr_t
__PrepareBounce(
    _In_    struct SHMBuffer*       buffer,
    _In_    size_t                  offset,
    _In_    size_t                  length,
    _In_    size_t                  pageSize,
    _InOut_ struct SHMDeviceBounce* bounce,
    _Out_   SHMSG_t*                extents,
    _Out_   int*                    extentCountOut)
{
    oserr_t status;

    status = __MapSourceView(buffer, offset, length, pageSize, bounce);
    if (status != OS_EOK) {
        return status;
    }

    status = __AllocateCopy(length, pageSize, bounce, extents, extentCountOut);
    if (status != OS_EOK) {
        __ReleaseBounce(bounce);
    }
    return status;
}

/**
 * @brief Build the device segments for a buffer range into a new mapping.
 *
 * Kept apart from SHMDeviceMap so that function only deals with references.
 * The physical blocks gathered here are kept in the mapping for cache work.
 * Bounces when the device cannot use the buffer's own pages for the range,
 * unless the caller needs the device to use the buffer itself.
 */
static oserr_t
__CreateMapping(
    _In_  struct SHMBuffer*         buffer,
    _In_  struct SHMDeviceContext*  context,
    _In_  size_t                    offset,
    _In_  size_t                    length,
    _In_  enum SHMDeviceDirection   direction,
    _In_  bool                      allowBounce,
    _Out_ struct SHMDeviceMapping** mappingOut)
{
    struct SHMDeviceMapping* mapping = NULL;
    struct SHMDeviceBounce   bounce = { 0 };
    SHMSG_t*                 extents;
    size_t                   pageSize = GetMemorySpacePageSize();
    size_t                   pageCount;
    size_t                   maxSegments;
    int                      extentCount;
    uint32_t                 segmentCount;
    bool                     needsBounce;
    oserr_t                  status;

    // One extent per touched page is the most either gather can produce: the
    // bounce range starts on a page boundary, so it never touches more pages.
    pageCount = DIVUP((buffer->Offset + offset) % pageSize + length, pageSize);
    extents = kmalloc(pageCount * sizeof(SHMSG_t));
    if (extents == NULL) {
        return OS_EOOM;
    }

    // Pages are filled in on demand under this lock, so hold it while reading.
    MutexLock(&buffer->Mutex);
    status = __GatherExtents(buffer->Pages, buffer->Offset + offset, length, pageSize, extents, &extentCount);
    MutexUnlock(&buffer->Mutex);
    if (status != OS_EOK) {
        goto cleanup;
    }

    // Bytes the device cannot reach are not an error yet: bounce pages may be
    // reachable where the buffer's own pages are not.
    status = SHMDeviceContextBuildSegments(context, extents, extentCount, &segmentCount, NULL);
    if (status != OS_EOK && status != OS_ENOENT) {
        goto cleanup;
    }
    needsBounce = status == OS_ENOENT || __SharesCacheLines(buffer, context, offset, length, direction);

    // Keep reporting unreachable bytes as such; the other reason to bounce
    // means this range cannot be used directly.
    if (needsBounce && !allowBounce) {
        if (status == OS_EOK) {
            status = OS_ENOTSUPPORTED;
        }
        goto cleanup;
    }

    if (needsBounce) {
        status = __PrepareBounce(buffer, offset, length, pageSize, &bounce, extents, &extentCount);
        if (status != OS_EOK) {
            goto cleanup;
        }

        status = SHMDeviceContextBuildSegments(context, extents, extentCount, &segmentCount, NULL);
        if (status != OS_EOK) {
            goto cleanup;
        }
    }

    // Guard the size calculation, which could otherwise wrap on 32-bit kernels.
    maxSegments = (SIZE_MAX - sizeof(struct SHMDeviceMapping)) / sizeof(struct SHMDeviceSegment);
    if (segmentCount > maxSegments) {
        status = OS_EOOM;
        goto cleanup;
    }
    mapping = kmalloc(sizeof(struct SHMDeviceMapping) + segmentCount * sizeof(struct SHMDeviceSegment));
    if (mapping == NULL) {
        status = OS_EOOM;
        goto cleanup;
    }

    status = SHMDeviceContextBuildSegments(context, extents, extentCount, &segmentCount, mapping->Segments);
    if (status != OS_EOK) {
        goto cleanup;
    }

    mapping->SHMID = buffer->ID;
    mapping->Context = context;
    mapping->Offset = offset;
    mapping->Length = length;
    mapping->Direction = direction;
    // The caller built the mapping from the CPU side, so the CPU owns it first.
    mapping->DeviceOwned = false;
    mapping->Bounce = bounce;
    mapping->Extents = extents;
    mapping->ExtentCount = extentCount;
    mapping->SegmentCount = segmentCount;
    *mappingOut = mapping;
    return OS_EOK;

cleanup:
    __ReleaseBounce(&bounce);
    if (mapping != NULL) {
        kfree(mapping);
    }
    kfree(extents);
    return status;
}

static oserr_t
__Map(
    _In_    struct SHMDeviceContext*  context,
    _In_    uuid_t                    shmID,
    _In_    size_t                    offset,
    _In_    size_t                    length,
    _In_    enum SHMDeviceDirection   direction,
    _In_    bool                      allowBounce,
    _InOut_ struct SHMDeviceMapping** mappingOut)
{
    struct SHMBuffer*        buffer;
    struct SHMDeviceMapping* mapping = NULL;
    oserr_t                  status;

    // Preserve an occupied slot: replacing it would lose its owning reference.
    if (context == NULL || mappingOut == NULL) {
        return OS_EINVALPARAMS;
    }
    if (*mappingOut != NULL) {
        return OS_EBUSY;
    }

    // Later cache handling decides what to do from the direction, so an
    // unknown value must not slip through and be treated as one of them.
    if (direction != SHMDeviceToDevice && direction != SHMDeviceFromDevice &&
        direction != SHMDeviceBidirectional) {
        return OS_EINVALPARAMS;
    }

    // This reference is what keeps the pages allocated while a device may use
    // them: SHM frees a buffer's pages only when its last reference goes away.
    status = AcquireHandleOfType(shmID, HandleTypeSHM, (void**)&buffer);
    if (status != OS_EOK) {
        return OS_ENOENT;
    }

    status = __CheckBufferKind(buffer);
    if (status != OS_EOK) {
        goto release;
    }

    // Use subtraction so a huge offset or length cannot wrap past the check.
    if (length == 0 || offset > buffer->Length || length > buffer->Length - offset) {
        status = OS_EINVALPARAMS;
        goto release;
    }

    // The mapping hands out the context with its segments, so it keeps the
    // rules alive for as long as those segments can be read.
    status = SHMDeviceContextAcquire(context);
    if (status != OS_EOK) {
        goto release;
    }

    status = __CreateMapping(buffer, context, offset, length, direction, allowBounce, &mapping);
    if (status != OS_EOK) {
        SHMDeviceContextRelease(&context);
        goto release;
    }

    *mappingOut = mapping;
    return OS_EOK;

release:
    (void)DestroyHandle(shmID);
    return status;
}

oserr_t
SHMDeviceMap(
    _In_    struct SHMDeviceContext*  context,
    _In_    uuid_t                    shmID,
    _In_    size_t                    offset,
    _In_    size_t                    length,
    _In_    enum SHMDeviceDirection   direction,
    _InOut_ struct SHMDeviceMapping** mappingOut)
{
    return __Map(context, shmID, offset, length, direction, true, mappingOut);
}

static bool
__IsPowerOfTwoOrZero(
    _In_ uint64_t value)
{
    return (value & (value - 1)) == 0;
}

/**
 * @brief Reject requirements that no memory could ever meet.
 *
 * Catching these before allocating keeps a caller's mistake from looking like
 * a temporary shortage of suitable memory.
 */
static bool
__RequirementsValid(
    _In_ const struct SHMDeviceRequirements* requirements)
{
    if (requirements->Length == 0) {
        return false;
    }
    if (!__IsPowerOfTwoOrZero(requirements->Alignment) || !__IsPowerOfTwoOrZero(requirements->Boundary)) {
        return false;
    }

    // One block longer than the boundary would always cross it.
    if (requirements->Contiguous && requirements->Boundary != 0) {
        return requirements->Length <= requirements->Boundary;
    }
    return true;
}

/**
 * @brief Check the device's view of an allocation against the requirements.
 *
 * The current page allocator cannot be asked for contiguous or aligned
 * memory, so the result is checked afterwards instead of being guaranteed.
 */
static oserr_t
__CheckRequirements(
    _In_ const struct SHMDeviceMapping*      mapping,
    _In_ const struct SHMDeviceRequirements* requirements)
{
    const struct SHMDeviceSegment* segment;
    uint64_t                       last;

    if (requirements->Contiguous && mapping->SegmentCount != 1) {
        return OS_ENOTSUPPORTED;
    }

    // Alignment applies to where the allocation starts for the device.
    if (requirements->Alignment != 0 && (mapping->Segments[0].Address & (requirements->Alignment - 1)) != 0) {
        return OS_ENOTSUPPORTED;
    }

    // A block crosses a boundary when its first and last byte differ in the
    // address bits above the boundary size.
    for (uint32_t i = 0; requirements->Boundary != 0 && i < mapping->SegmentCount; i++) {
        segment = &mapping->Segments[i];
        last = segment->Address + (segment->Length - 1);
        if (((segment->Address ^ last) & ~(requirements->Boundary - 1)) != 0) {
            return OS_ENOTSUPPORTED;
        }
    }
    return OS_EOK;
}

oserr_t
SHMDeviceAllocate(
    _In_    struct SHMDeviceContext*            context,
    _In_    const struct SHMDeviceRequirements* requirements,
    _Out_   SHMHandle_t*                        bufferOut,
    _InOut_ struct SHMDeviceMapping**           mappingOut)
{
    SHMHandle_t handle;
    size_t      pageSize = GetMemorySpacePageSize();
    oserr_t     status;

    if (context == NULL || requirements == NULL || bufferOut == NULL || mappingOut == NULL) {
        return OS_EINVALPARAMS;
    }
    if (*mappingOut != NULL) {
        return OS_EBUSY;
    }

    // The length is rounded up to whole pages below, which must not wrap.
    if (!__RequirementsValid(requirements) || requirements->Length > SIZE_MAX - pageSize) {
        return OS_EINVALPARAMS;
    }

    // SHM_DEVICE gives committed, uncached pages, so neither side needs sync
    // calls to see the other's writes; SHM_CLEAN zeroes them, so the device
    // never sees old data. Low memory is the most likely to be reachable.
    // Whole pages are used so no cache line is shared with other memory.
    status = SHMCreate(
        &(SHM_t) {
            .Flags = SHM_DEVICE | SHM_CLEAN,
            .Access = SHM_ACCESS_READ | SHM_ACCESS_WRITE,
            .Conformity = OSMEMORYCONFORMITY_LOW,
            .Size = DIVUP(requirements->Length, pageSize) * pageSize
        },
        &handle
    );
    if (status != OS_EOK) {
        return status;
    }

    // The device keeps using this memory while the CPU does too, so it must
    // work on the buffer itself: a bounce copy would never be up to date.
    status = __Map(context, handle.ID, 0, handle.Length, SHMDeviceBidirectional, false, mappingOut);
    if (status == OS_EOK) {
        status = __CheckRequirements(*mappingOut, requirements);
        if (status != OS_EOK) {
            (void)SHMDeviceUnmap(mappingOut);
        }
    }
    if (status != OS_EOK) {
        (void)SHMDetach(&handle);
        return status;
    }

    *bufferOut = handle;
    return OS_EOK;
}

oserr_t
SHMDeviceUnmap(
    _InOut_ struct SHMDeviceMapping** mapping)
{
    struct SHMDeviceMapping* owned;

    // Nothing to release; succeeding keeps repeated cleanup harmless.
    if (mapping == NULL || *mapping == NULL) {
        return OS_EOK;
    }

    // While the device owns the range it may still read or write it, and
    // releasing could free those pages under it. The owner must first take
    // the range back with SHMDeviceSyncForCpu, after the device has stopped.
    if ((*mapping)->DeviceOwned) {
        return OS_EBUSY;
    }

    // Clear the slot before releasing so a retry on it cannot drop the buffer
    // or context references a second time.
    owned = *mapping;
    *mapping = NULL;

    SHMDeviceContextRelease(&owned->Context);
    // The buffer view must go before the buffer reference that keeps its pages.
    __ReleaseBounce(&owned->Bounce);
    (void)DestroyHandle(owned->SHMID);
    kfree(owned->Extents);
    kfree(owned);
    return OS_EOK;
}

/**
 * @brief Check that a sync range lies inside the mapping.
 *
 * Ranges are counted from the start of the mapping, not the buffer, so a
 * driver only needs to know about the part it mapped. Subtraction keeps a huge
 * offset or length from wrapping past the check.
 */
static bool
__RangeInMapping(
    _In_ const struct SHMDeviceMapping* mapping,
    _In_ size_t                         offset,
    _In_ size_t                         length)
{
    if (offset > mapping->Length) {
        return false;
    }
    return length <= mapping->Length - offset;
}

/**
 * @brief Run one cache operation on the physical bytes behind a sync range.
 *
 * Sync ranges count from the start of the mapping, but cache operations need
 * physical addresses, so this walks the mapping's physical blocks to find the
 * pieces the range covers. The range must already lie inside the mapping.
 */
static void
__MaintainRange(
    _In_ const struct SHMDeviceMapping* mapping,
    _In_ size_t                         offset,
    _In_ size_t                         length,
    _In_ void                           (*operation)(uintptr_t, size_t))
{
    const SHMSG_t* extent;
    size_t         skip = offset;
    size_t         chunk;

    for (int i = 0; i < mapping->ExtentCount && length != 0; i++) {
        extent = &mapping->Extents[i];
        if (skip >= extent->Length) {
            skip -= extent->Length;
            continue;
        }

        chunk = extent->Length - skip;
        if (chunk > length) {
            chunk = length;
        }
        operation(extent->Address + skip, chunk);
        length -= chunk;
        skip = 0;
    }
}

oserr_t
SHMDeviceSyncForDevice(
    _In_ struct SHMDeviceMapping* mapping,
    _In_ size_t                   offset,
    _In_ size_t                   length)
{
    // Handing over nothing would leave the device with no bytes to use.
    if (mapping == NULL || length == 0) {
        return OS_EINVALPARAMS;
    }
    if (!__RangeInMapping(mapping, offset, length)) {
        return OS_EINVALPARAMS;
    }

    // A second hand-over means the driver lost track of a transfer that may
    // still be running, so refuse instead of silently accepting it.
    if (mapping->DeviceOwned) {
        return OS_EBUSY;
    }

    // When bouncing, the device reads the copy, so the CPU's data must be put
    // there first. A device that only writes would overwrite it anyway.
    if (mapping->Bounce.Copy != 0 && mapping->Direction != SHMDeviceFromDevice) {
        memcpy(
            (void*)(mapping->Bounce.Copy + offset),
            (const void*)(mapping->Bounce.Source + mapping->Bounce.SourceOffset + offset),
            length
        );
    }

    // Every CPU write to the buffer must be finished before the cache work and
    // before the device is told to start.
    dma_mb();

    // A device that cannot see the CPU cache reads memory directly. Before it
    // reads, write cached data back. Before it writes, also throw the lines
    // away, so no old cached line can later land on top of the device's data.
    if (mapping->Context->CachePolicy == SHMDeviceCacheNonCoherent) {
        if (mapping->Direction == SHMDeviceToDevice) {
            __MaintainRange(mapping, offset, length, CpuDataCacheClean);
        } else {
            __MaintainRange(mapping, offset, length, CpuDataCacheCleanInvalidate);
        }
    }
    mapping->DeviceOwned = true;
    return OS_EOK;
}

oserr_t
SHMDeviceSyncForCpu(
    _In_ struct SHMDeviceMapping* mapping,
    _In_ size_t                   offset,
    _In_ size_t                   length)
{
    // An empty completed range is valid: a transfer can stop before the device
    // wrote anything, and the CPU must still be able to take the range back.
    if (mapping == NULL) {
        return OS_EINVALPARAMS;
    }
    if (!__RangeInMapping(mapping, offset, length)) {
        return OS_EINVALPARAMS;
    }

    // Taking back a range the device never had points to a driver bug.
    if (!mapping->DeviceOwned) {
        return OS_EINVALPARAMS;
    }

    // The driver has seen the device finish. Order that check before any CPU
    // read of the data, so the CPU cannot read values from before the device
    // wrote them.
    dma_mb();

    // While the device owned the memory, the CPU may have read ahead and
    // cached old values. Throw those away for the bytes the device wrote.
    // Map made sure this range's lines belong to the mapping alone.
    if (mapping->Context->CachePolicy == SHMDeviceCacheNonCoherent &&
        mapping->Direction != SHMDeviceToDevice) {
        __MaintainRange(mapping, offset, length, CpuDataCacheInvalidate);
    }

    // When bouncing, the device wrote the copy. Bring back only the completed
    // bytes, so the rest of the buffer keeps whatever the CPU put there.
    if (mapping->Bounce.Copy != 0 && mapping->Direction != SHMDeviceToDevice) {
        memcpy(
            (void*)(mapping->Bounce.Source + mapping->Bounce.SourceOffset + offset),
            (const void*)(mapping->Bounce.Copy + offset),
            length
        );
    }
    mapping->DeviceOwned = false;
    return OS_EOK;
}

const struct SHMDeviceSegment*
SHMDeviceMappingSegments(
    _In_  const struct SHMDeviceMapping* mapping,
    _Out_ uint32_t*                      countOut)
{
    // Hand out the stored list rather than a copy; it lives as long as the
    // mapping and never changes, so there is nothing to keep in sync.
    if (mapping == NULL || countOut == NULL) {
        return NULL;
    }
    *countOut = mapping->SegmentCount;
    return mapping->Segments;
}
