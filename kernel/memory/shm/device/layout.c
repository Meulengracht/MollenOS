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

// Builds the device layout of a mapping. The layout is everything the device
// needs to reach a buffer range. That is the physical extents (blocks of RAM),
// the bounce pages if any, and the device segments (the address ranges the
// driver programs into the hardware). Bounce pages are spare pages the device
// can reach, used when it cannot reach the buffer's own pages.
//
// All of this is built while the mapping is still private to the caller and
// before any device can use it. Checking the caller's access rights, linking
// the mapping to its context and installing it in the IOMMU happen in device.c.

#include <shm/device.h>
#include <shm/access.h>
#include <arch/utils.h>
#include <assert.h>
#include <debug.h>
#include <heap.h>
#include <machine.h>
#include <string.h>

#include "private.h"
#include "../../dma/private.h"

/**
 * @brief Check whether cache work on this range would also hit bytes outside it.
 *
 * A cache line is the smallest block the CPU cache handles, commonly 64 bytes.
 * Before a non-coherent device writes, the CPU must discard (invalidate) every
 * cache line that covers the range. If a line reaches past the range, that
 * would also throw away recent CPU writes to the neighboring bytes. The caller
 * uses bounce pages for such ranges. Coherent devices and uncached pool memory
 * need no cache work. When the device only reads, the CPU writes cached data
 * back to RAM and keeps the lines, so neighboring bytes are safe.
 *
 * @param offset Start of the range, counted from the start of the buffer.
 * @return true if the range needs bounce pages because of shared cache lines.
 */
static bool
__SharesCacheLines(
    _In_ const struct SHMBuffer*        buffer,
    _In_ const struct SHMDeviceContext* context,
    _In_ size_t                         offset,
    _In_ size_t                         length,
    _In_ enum DMADirection              direction)
{
    size_t line;
    size_t start;

    if (context->CachePolicy == SHMDeviceCacheCoherent || direction == DMAToDevice) {
        return false;
    }

    if (SHMBufferUncached(buffer)) {
        return false;
    }

    // Pages start on cache line boundaries, so only the position inside the
    // buffer's pages decides the alignment.
    line = CpuDataCacheLineSize();
    start = buffer->Offset + offset;
    return start % line != 0 || length % line != 0;
}

/**
 * @brief Build a list of physical blocks (extents) for the requested bytes of
 *        a page list.
 *
 * A page address of zero means the page was never allocated. Walking the page
 * list directly lets us reject such pages and keep every entry inside the
 * requested range. Pages that are next to each other in physical memory are
 * joined into one entry. This works for both buffer pages and bounce pages.
 *
 * @param start Byte offset into the page list.
 * @param extents Storage for one entry per page touched by the range.
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
        // SHM uses address zero for a page that was never allocated.
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

/**
 * @brief Map the buffer's pages into kernel memory for copying.
 *
 * The process can remove its own view of the buffer while the device is still
 * using it. This kernel view stays valid until the device mapping is released,
 * so copying to and from the bounce pages keeps working.
 */
static oserr_t
__MapSourceView(
    _In_    struct SHMBuffer*  buffer,
    _In_    size_t             offset,
    _In_    size_t             length,
    _In_    size_t             pageSize,
    _InOut_ SHMDeviceBounce_t* bounce)
{
    size_t       start = buffer->Offset + offset;
    unsigned int flags = MAPPING_COMMIT | MAPPING_PERSISTENT;
    oserr_t      status;

    // Use the same cache setting as the buffer's other views. Mixed settings
    // on the same pages can make the views see different data.
    if (SHMBufferUncached(buffer)) {
        flags |= MAPPING_NOCACHE;
    }

    bounce->SourceOffset = start % pageSize;
    bounce->SourceMappingLength = DIVUP(bounce->SourceOffset + length, pageSize) * pageSize;
    status = MemorySpaceMap(
        GetCurrentMemorySpace(),
        &(struct MemorySpaceMapOptions) {
            .SHMTag = buffer->ID,
            .Pages = &buffer->Pages[start / pageSize],
            .Length = bounce->SourceMappingLength,
            .Mask = __MASK,
            .Flags = flags,
            .PlacementFlags = MAPPING_PHYSICAL_FIXED | MAPPING_VIRTUAL_GLOBAL
        },
        &bounce->SourceMapping
    );
    if (status != OS_EOK) {
        ERROR("__MapSourceView: MemorySpaceMap buffer %u, length %zu failed: %u",
              buffer->ID, bounce->SourceMappingLength, status);
    }
    return status;
}

/**
 * @brief Allocate bounce pages from the device pool and build their extents.
 *
 * The pool is chosen at boot from memory that devices can reach. On ARM64 the
 * kernel views of it are uncached. The caller must still check the device
 * addresses after translating them. The pages are zeroed so a device cannot
 * read data left by an earlier user. If the pool is full, the mapping fails.
 * Ordinary memory is not used as a fallback, because the device may not be
 * able to reach it.
 *
 * @param extents Storage for one entry per bounce page. Filled on success.
 */
static oserr_t
__AllocateBouncePages(
    _In_    size_t                           length,
    _In_    size_t                           pageSize,
    _In_    const struct DMAMapRequirements* requirements,
    _InOut_ SHMDeviceBounce_t*               bounce,
    _Out_   SHMSG_t*                         extents,
    _Out_   int*                             extentCountOut)
{
    paddr_t*     pages;
    size_t       pageCount = DIVUP(length, pageSize);
    unsigned int flags = MAPPING_COMMIT | MAPPING_PERSISTENT | MAPPING_CLEAN;
    uint64_t     alignment = requirements != NULL ? requirements->AddressAlignment : 0;
    oserr_t      status;

    if (alignment > SIZE_MAX) {
        WARNING("__AllocateBouncePages: alignment %llu is too large", (unsigned long long)alignment);
        return OS_ENOTSUPPORTED;
    }

    status = SystemMemoryAllocateDevice(
        &GetMachine()->PhysicalMemory,
        pageCount * pageSize,
        (size_t)alignment,
        0,
        &bounce->Allocation
    );
    if (status != OS_EOK) {
        ERROR("__AllocateBouncePages: SystemMemoryAllocateDevice length %zu failed: %u",
              pageCount * pageSize, status);
        return status;
    }

    // MemorySpaceMap takes the pages as a list, one entry per page.
    pages = kmalloc(pageCount * sizeof(paddr_t));
    if (pages == NULL) {
        ERROR("__AllocateBouncePages: failed to allocate a list of %zu pages", pageCount);
        return OS_EOOM;
    }

    for (size_t i = 0; i < pageCount; i++) {
        pages[i] = bounce->Allocation.PhysicalBase + ((paddr_t)i * pageSize);
    }

    if (!bounce->Allocation.Cached) {
        flags |= MAPPING_NOCACHE;
    }

    status = MemorySpaceMap(
        GetCurrentMemorySpace(),
        &(struct MemorySpaceMapOptions) {
            .Pages = pages,
            .Length = bounce->Allocation.Length,
            .Mask = __MASK,
            .Flags = flags,
            .PlacementFlags = MAPPING_PHYSICAL_FIXED | MAPPING_VIRTUAL_GLOBAL
        },
        &bounce->BounceMapping
    );
    if (status != OS_EOK) {
        ERROR("__AllocateBouncePages: MemorySpaceMap length %zu failed: %u",
              bounce->Allocation.Length, status);
        kfree(pages);
        return status;
    }

    // Every bounce page was just allocated, so an incomplete page list here
    // means the allocation itself is broken.
    status = __GatherExtents(
        pages,
        0,
        length,
        pageSize,
        extents,
        extentCountOut
    );
    if (status != OS_EOK) {
        ERROR("__AllocateBouncePages: __GatherExtents bounce base %llx, length %zu failed: %u",
              (unsigned long long)bounce->Allocation.PhysicalBase, length, status);
    }
    kfree(pages);
    return status;
}

/**
 * @brief Prepare bounce pages for a new mapping.
 *
 * Bounce pages are temporary pages that the device can reach. They are used
 * when the device cannot reach the buffer's own addresses, when the buffer
 * does not meet the segment alignment rules, or when cache invalidation would
 * hit neighboring bytes outside the range. The caller must still check the
 * device addresses of the bounce pages. On success, extents describes the
 * bounce pages.
 */
static oserr_t
__PrepareBounce(
    _In_    struct SHMBuffer*                buffer,
    _In_    size_t                           offset,
    _In_    size_t                           length,
    _In_    size_t                           pageSize,
    _In_    const struct DMAMapRequirements* requirements,
    _InOut_ SHMDeviceBounce_t*               bounce,
    _Out_   SHMSG_t*                         extents,
    _Out_   int*                             extentCountOut)
{
    oserr_t status;

    status = __MapSourceView(buffer, offset, length, pageSize, bounce);
    if (status != OS_EOK) {
        return status;
    }

    status = __AllocateBouncePages(
        length,
        pageSize,
        requirements,
        bounce,
        extents,
        extentCountOut
    );
    if (status != OS_EOK) {
        SHMDeviceReleaseBounce(bounce);
    }
    return status;
}

/**
 * @brief Decide whether a range with a cache line conflict can use bounce pages.
 *
 * Only bounce mappings can. Direct-only mappings fail with OS_ENOTSUPPORTED.
 * An address or segment error from the original pages is reported ahead of
 * that, and any other error passes through unchanged.
 */
static oserr_t
__CheckCacheLineFallback(
    _In_  struct SHMDeviceContext*         context,
    _In_  const SHMSG_t*                   extents,
    _In_  int                              extentCount,
    _In_  enum SHMDeviceMappingKind        kind,
    _In_  const struct DMAMapRequirements* requirements)
{
    uint32_t segmentCount = 0;
    oserr_t  status;

    // Check the original pages first, so an address or segment problem is
    // reported ahead of the cache line problem. A bounce mapping can fix all
    // of them, so it carries on.
    status = DMADirectWalkSegments(
        context->AddressSpace,
        context->AddressLimit,
        extents,
        extentCount,
        requirements,
        NULL,
        &segmentCount
    );
    (void)segmentCount;
    if (status != OS_EOK &&
        status != OS_ENOENT &&
        status != OS_ENOTSUPPORTED) {
        ERROR("__CheckCacheLineFallback: DMADirectWalkSegments context %p failed: %u", context, status);
        return status;
    }

    if (kind == SHMDeviceMappingStreamingBounce) {
        return OS_EOK;
    }

    WARNING("__CheckCacheLineFallback: context %p mapping kind %u cannot use bounce pages (walk status %u)",
            context, (unsigned int)kind, status);
    return status == OS_EOK ? OS_ENOTSUPPORTED : status;
}

/**
 * @brief Require whole IOMMU granules that match the requested range exactly.
 *
 * The IOMMU maps only whole granules, so each extent must start on a granule
 * boundary and cover whole granules. Nothing may be rounded up. The caller may
 * have access to only part of a larger buffer, and the extra bytes would
 * expose the rest of it. The extents must also add up to exactly the requested
 * length. Subtracting from the remaining length as we go avoids overflow in the
 * sum.
 *
 * @return OS_EOK, or OS_ENOTSUPPORTED if any extent is not whole granules or
 *         the total differs from length.
 */
static oserr_t
__CheckTranslatedExtents(
    _In_ const SHMSG_t* extents,
    _In_ int            count,
    _In_ size_t         length,
    _In_ uint64_t       granule)
{
    size_t   remaining = length;
    uint64_t mask = granule - 1;

    for (int i = 0; i < count; i++) {
        if ((extents[i].Address & mask) != 0 || extents[i].Length == 0 ||
            (extents[i].Length & mask) != 0 || extents[i].Length > remaining) {
            WARNING("__CheckTranslatedExtents: extent %i (address %llx, length %zu) is not whole granules of %llu",
                    i, (unsigned long long)extents[i].Address, (size_t)extents[i].Length,
                    (unsigned long long)granule);
            return OS_ENOTSUPPORTED;
        }
        remaining -= extents[i].Length;
    }

    if (remaining != 0) {
        WARNING("__CheckTranslatedExtents: extents are %zu bytes short of the requested %zu",
                remaining, length);
        return OS_ENOTSUPPORTED;
    }
    return OS_EOK;
}

/**
 * @brief Check the device addresses of an allocation against the requirements.
 *
 * The pool already checked the physical addresses. The device can see them at
 * different addresses, or split into several pieces, so the requirements are
 * checked again on the device layout before the mapping is made.
 *
 * @return OS_EOK, or OS_ENOTSUPPORTED if the layout is not one segment when
 *         that is required, or breaks the alignment or boundary rule.
 */
static oserr_t
__CheckRequirements(
    _In_ const struct DMAAddressMapping* mapping,
    _In_ const struct DMARequirements*   requirements)
{
    const struct DMASegment* segments;
    uint32_t                 count;

    segments = DMAAddressMappingSegments(mapping, &count);
    if (requirements->Contiguous && count != 1) {
        WARNING("__CheckRequirements: allocation needs one segment but has %u", count);
        return OS_ENOTSUPPORTED;
    }

    // Check the alignment of the first device address.
    if (requirements->Alignment != 0 && (segments[0].Address & (requirements->Alignment - 1)) != 0) {
        WARNING("__CheckRequirements: device address %llx breaks alignment %llu",
                (unsigned long long)segments[0].Address, (unsigned long long)requirements->Alignment);
        return OS_ENOTSUPPORTED;
    }

    // A segment must not cross a boundary. It crosses one when its first and
    // last byte fall in different boundary-sized blocks. The upper address bits
    // tell which block a byte is in.
    for (uint32_t i = 0; requirements->Boundary != 0 && i < count; i++) {
        const struct DMASegment* segment = &segments[i];
        uint64_t                 last = segment->Address + (segment->Length - 1);
        if (((segment->Address ^ last) & ~(requirements->Boundary - 1)) != 0) {
            WARNING("__CheckRequirements: segment %u (address %llx) crosses boundary %llu",
                    i, (unsigned long long)segment->Address, (unsigned long long)requirements->Boundary);
            return OS_ENOTSUPPORTED;
        }
    }
    return OS_EOK;
}

/**
 * @brief Working state while a mapping is being prepared.
 *
 * Extents describes the memory the device will use. That is the buffer's own
 * pages, or the bounce pages when UsesBounce is set. AddressMapping holds the
 * device addresses built from Extents. When preparation fails,
 * __ReleaseLayout frees everything here. On success the mapping takes over
 * Extents, Bounce and AddressMapping.
 */
struct __MappingLayout {
    SHMSG_t*                  Extents;
    int                       ExtentCount;
    bool                      UsesBounce;
    SHMDeviceBounce_t         Bounce;
    struct DMAAddressMapping* AddressMapping;
};

/**
 * @brief Collect the physical extents of the requested buffer range.
 *
 * One entry is reserved for each page the range touches. A bounce copy starts
 * on a page boundary, so it never needs more entries than this. The buffer
 * lock is held while the page list is read.
 *
 * @param offset Start of the range, counted from the start of the handle.
 * @return OS_EOK, OS_EOOM, or OS_EINCOMPLETE if a page was never allocated.
 *         Extents is NULL on failure.
 */
static oserr_t
__GatherBufferExtents(
    _In_    struct SHMBuffer*       buffer,
    _In_    size_t                  offset,
    _In_    size_t                  length,
    _In_    size_t                  pageSize,
    _InOut_ struct __MappingLayout* layout)
{
    size_t  pageCount = DIVUP((buffer->Offset + offset) % pageSize + length, pageSize);
    oserr_t status;

    layout->Extents = kmalloc(pageCount * sizeof(SHMSG_t));
    if (layout->Extents == NULL) {
        ERROR("__GatherBufferExtents: failed to allocate %zu extents for buffer %u", pageCount, buffer->ID);
        return OS_EOOM;
    }

    MutexLock(&buffer->Mutex);
    status = __GatherExtents(
        buffer->Pages,
        buffer->Offset + offset,
        length,
        pageSize,
        layout->Extents,
        &layout->ExtentCount
    );
    MutexUnlock(&buffer->Mutex);
    if (status != OS_EOK) {
        WARNING("__GatherBufferExtents: buffer %u offset %zu, length %zu has unallocated pages: %u",
                buffer->ID, offset, length, status);
        kfree(layout->Extents);
        layout->Extents = NULL;
    }
    return status;
}

/**
 * @brief Decide from the buffer's own pages whether bounce pages are required.
 *
 * Sets UsesBounce when cache lines are shared with neighboring bytes. Nothing
 * is allocated here. A mapping that cannot use bounce pages fails now, before
 * any work is done.
 *
 * @return OS_EOK, or OS_ENOTSUPPORTED if the pages cannot be used as they are
 *         and this mapping cannot use bounce pages. Address and segment errors
 *         from the original pages are returned unchanged.
 */
static oserr_t
__DecideBounceUse(
    _In_    struct SHMBuffer*                buffer,
    _In_    struct SHMDeviceContext*         context,
    _In_    size_t                           offset,
    _In_    size_t                           length,
    _In_    enum DMADirection                direction,
    _In_    enum SHMDeviceMappingKind        kind,
    _In_    const struct DMAMapRequirements* requirements,
    _InOut_ struct __MappingLayout*          layout)
{
    uint64_t granularity = DMAAddressSpaceGetGranularity(context->AddressSpace);
    oserr_t  status;

    layout->UsesBounce = __SharesCacheLines(buffer, context, offset, length, direction);
    if (granularity != 0) {
        status = __CheckTranslatedExtents(
            layout->Extents,
            layout->ExtentCount,
            length,
            granularity
        );
        if (status != OS_EOK) {
            return status;
        }

        // A translated context maps only the pages of the range itself.
        // Rounding up or copying to bounce pages would cover bytes the caller
        // did not ask for, so this is refused.
        if (layout->UsesBounce) {
            WARNING("__DecideBounceUse: buffer %u offset %zu, length %zu shares cache lines in a translated context",
                    buffer->ID, offset, length);
            return OS_ENOTSUPPORTED;
        }
        return OS_EOK;
    }

    if (!layout->UsesBounce) {
        return OS_EOK;
    }

    // Check the original pages before allocating bounce pages for the cache
    // line problem. A mapping kind that cannot use bounce pages fails here.
    return __CheckCacheLineFallback(
        context,
        layout->Extents,
        layout->ExtentCount,
        kind,
        requirements
    );
}

static oserr_t
__PrepareAddressMapping(
    _In_    struct SHMDeviceContext*         context,
    _In_    enum DMADirection                direction,
    _In_    const struct DMAMapRequirements* requirements,
    _InOut_ struct __MappingLayout*          layout)
{
    return DMAAddressMappingPrepare(
        context->AddressSpace,
        layout->Extents,
        layout->ExtentCount,
        context->AddressLimit,
        direction,
        requirements,
        &layout->AddressMapping
    );
}

/**
 * @brief Build the device addresses for the layout, using bounce pages if needed.
 *
 * Without bounce pages, the buffer's own pages are tried first. A
 * SHMDeviceMappingStreamingBounce mapping in a direct context falls back to
 * bounce pages when the device cannot address those pages or their segments
 * break its limits. Any other error is passed through unchanged. After a
 * fallback, Extents is replaced by the extents of the bounce pages.
 */
static oserr_t
__BuildLayout(
    _In_    struct SHMBuffer*                buffer,
    _In_    struct SHMDeviceContext*         context,
    _In_    size_t                           offset,
    _In_    size_t                           length,
    _In_    enum DMADirection                direction,
    _In_    enum SHMDeviceMappingKind        kind,
    _In_    const struct DMAMapRequirements* requirements,
    _InOut_ struct __MappingLayout*          layout)
{
    uint64_t granularity = DMAAddressSpaceGetGranularity(context->AddressSpace);
    oserr_t  status;

    if (!layout->UsesBounce) {
        // Check the physical ranges and build the list of segments as the
        // device sees them.
        status = __PrepareAddressMapping(context, direction, requirements, layout);
        if (status == OS_EOK) {
            return OS_EOK;
        }

        if (granularity != 0 || kind != SHMDeviceMappingStreamingBounce ||
            (status != OS_ENOENT && status != OS_ENOTSUPPORTED)) {
            return status;
        }
        TRACE("__BuildLayout: context %p falls back to bounce pages: %u", context, status);
        layout->UsesBounce = true;
    }

    status = __PrepareBounce(
        buffer,
        offset,
        length,
        GetMemorySpacePageSize(),
        requirements,
        &layout->Bounce,
        layout->Extents,
        &layout->ExtentCount
    );
    if (status != OS_EOK) {
        return status;
    }

    // The address mapping logs its own failures.
    return __PrepareAddressMapping(context, direction, requirements, layout);
}

/**
 * @brief Release the layout of a mapping that failed before it was installed.
 *
 * Installing has not happened, so the device never saw this layout. Releasing
 * a prepared address mapping should never fail. If it does, log it and stop,
 * because the bounce pages and the extent list must not be freed while
 * something may still point at them.
 *
 * @param originalStatus The error that caused the release. Only used for logging.
 */
static void
__ReleaseLayout(
    _InOut_ struct __MappingLayout* layout,
    _In_    oserr_t                 originalStatus)
{
    oserr_t status;

    if (layout->AddressMapping != NULL) {
        status = DMAAddressMappingRelease(&layout->AddressMapping);
        if (status != OS_EOK) {
            ERROR("SHMDeviceMappingPrepare: prepared address release failed: %u (original status %u)",
                  status, originalStatus);
            assert(status == OS_EOK);
        }
    }

    SHMDeviceReleaseBounce(&layout->Bounce);
    if (layout->Extents != NULL) {
        kfree(layout->Extents);
        layout->Extents = NULL;
    }
}

/**
 * @brief Fill in a prepared mapping and hand the layout over to it.
 *
 * The whole mapping is filled in before __MapBuffer links it, because a cancel
 * can find it once it is linked. The references to the source buffer and the
 * context stay valid through installation and any cleanup after a hardware
 * stop. Nobody may use the mapping until __MapBuffer marks it Ready.
 */
static void
__FillMapping(
    _InOut_ struct SHMDeviceMapping*       mapping,
    _In_    struct SHMBuffer*              buffer,
    _In_    struct SHMDeviceContext*       context,
    _In_    size_t                         offset,
    _In_    size_t                         length,
    _In_    enum DMADirection              direction,
    _In_    enum SHMDeviceMappingKind      kind,
    _In_    const struct __MappingLayout*  layout)
{
    SHMAccessInitialize(&mapping->Access, buffer, offset, length);
    mapping->Context = context;
    mapping->Offset = offset;
    mapping->Length = length;
    mapping->Cached = layout->UsesBounce ? layout->Bounce.Allocation.Cached : !SHMBufferUncached(buffer);
    mapping->Kind = kind;
    mapping->State = SHMDeviceMappingPreparing;

    if (__SHMDeviceMappingIsStreaming(mapping)) {
        mapping->Stream = (SHMDeviceStream_t) {
            .Bounce = layout->Bounce
        };
    } else {
        mapping->Persistent.Uses = 0;
    }

    mapping->Direction = direction;
    mapping->Extents = layout->Extents;
    mapping->ExtentCount = layout->ExtentCount;
    mapping->AddressMapping = layout->AddressMapping;
}

/**
 * @brief Prepare and check a buffer range. The device cannot use it yet.
 *
 * The caller, __MapBuffer, already holds the buffer and context references
 * this needs. This function records the physical extents that cache syncs use
 * later and builds the device addresses. A direct context may switch to bounce
 * pages when the device cannot reach the original pages or their layout breaks
 * the device's limits. A translated context (one behind an IOMMU) needs whole
 * granule extents that match the range exactly. It never rounds them up and
 * never copies the data to bounce pages.
 *
 * All allocation and layout work happens here, before any device address is
 * installed. A direct mapping may try again with bounce pages, so installing
 * runs once, on the layout that was finally chosen.
 */
oserr_t
SHMDeviceMappingPrepare(
    _In_  struct SHMBuffer*                buffer,
    _In_  SHMDeviceContext_t*              context,
    _In_  size_t                           offset,
    _In_  size_t                           length,
    _In_  enum DMADirection                direction,
    _In_  enum SHMDeviceMappingKind        kind,
    _In_  const struct DMAMapRequirements* requirements,
    _In_  const struct DMARequirements*    allocationRequirements,
    _Out_ SHMDeviceMapping_t**             mappingOut)
{
    struct SHMDeviceMapping* mapping = NULL;
    struct __MappingLayout   layout = { 0 };
    oserr_t                  status;

    status = __GatherBufferExtents(
        buffer,
        offset,
        length,
        GetMemorySpacePageSize(),
        &layout
    );
    if (status != OS_EOK) {
        return status;
    }

    status = __DecideBounceUse(
        buffer,
        context,
        offset,
        length,
        direction,
        kind,
        requirements,
        &layout
    );
    if (status != OS_EOK) {
        goto cleanup;
    }

    mapping = kmalloc(sizeof(struct SHMDeviceMapping));
    if (mapping == NULL) {
        ERROR("SHMDeviceMappingPrepare: failed to allocate a mapping for buffer %u", buffer->ID);
        status = OS_EOOM;
        goto cleanup;
    }

    memset(mapping, 0, sizeof(struct SHMDeviceMapping));
    atomic_init(&mapping->Lifetime.References, 1);

    status = __BuildLayout(
        buffer,
        context,
        offset,
        length,
        direction,
        kind,
        requirements,
        &layout
    );
    if (status != OS_EOK) {
        goto cleanup;
    }

    if (allocationRequirements != NULL) {
        status = __CheckRequirements(layout.AddressMapping, allocationRequirements);
        if (status != OS_EOK) {
            goto cleanup;
        }
    }

    __FillMapping(mapping, buffer, context, offset, length, direction, kind, &layout);
    *mappingOut = mapping;
    return OS_EOK;

cleanup:
    __ReleaseLayout(&layout, status);
    if (mapping != NULL) {
        kfree(mapping);
    }
    return status;
}
