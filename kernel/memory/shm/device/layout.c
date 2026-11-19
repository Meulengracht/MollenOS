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
 *
 * Builds the device layout of a mapping. The layout is everything the device
 * needs to reach a buffer range: the physical extents (blocks of RAM), any
 * temporary bounce pages, and the device segments (the address ranges the
 * driver programs into hardware).
 *
 * Bounce storage comes from the device pool as a list of units. A direct
 * context bounces into one contiguous unit, because there the device address
 * follows the physical address. A translated context (behind an IOMMU) maps
 * whole granules, so its bounce storage is rounded up to whole granules and
 * taken one unit at a time. The device then reaches a padded span, but the
 * driver gets one segment of exactly the requested length.
 *
 * All of this is built while the mapping is still private to the caller and
 * before any device can use it. Checking the caller's access rights, linking
 * the mapping to its context, and installing it in the IOMMU happen in device.c.
 */

#include <shm/device.h>
#include <shm/access.h>
#include <arch/utils.h>
#include <assert.h>
#include <ddk/barrier.h>
#include <debug.h>
#include <heap.h>
#include <limits.h>
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
 * @brief Sizes and placement of the bounce storage for one mapping.
 *
 * The storage is UnitCount pool allocations (units) of UnitLength bytes each,
 * every one starting on a multiple of Alignment. AllocationLength is the total,
 * which is also the length of the kernel view. The device reaches the first
 * MappedLength bytes of that view.
 */
struct __BouncePlan {
    size_t MappedLength;
    size_t UnitLength;
    size_t UnitCount;
    size_t AllocationLength;
    size_t Alignment;
};

// Why a translated mapping cannot use the original pages. The checks run in
// this order, and the first one that fails gives the reason.
enum __BounceReason {
    __BounceNotNeeded,
    // The pages are not whole IOMMU granules that match the range exactly.
    __BouncePartialGranules,
    // A page lies above the highest physical address the IOMMU unit can map.
    __BounceAbovePhysicalLimit,
    // A non-coherent device would write into cache lines that also hold bytes
    // outside the range.
    __BounceSharedCacheLines,
    // Direct context only: the device has no address for one of the pages.
    __BounceUnreachable,
    // Direct context only: the device segments of the pages break the
    // alignment the driver asked for.
    __BounceBreaksAlignment
};

static const char*
__BounceReasonName(
    _In_ enum __BounceReason reason)
{
    switch (reason) {
        case __BouncePartialGranules:
            return "is not whole granules";
        case __BounceAbovePhysicalLimit:
            return "lies above the physical limit";
        case __BounceSharedCacheLines:
            return "shares cache lines";
        case __BounceUnreachable:
            return "is not reachable by the device";
        case __BounceBreaksAlignment:
            return "breaks the device alignment";
        default:
            return "needs no bounce";
    }
}

/**
 * @brief Working state while a mapping is being prepared.
 *
 * Extents describes the memory the device will use. That is the buffer's own
 * pages, or the bounce storage when UsesBounce is set. BouncePlan holds the
 * sizes of that storage once bounce is selected, and BounceReason says why the
 * mapping bounces. BounceReason is only read for logging. AddressMapping holds
 * the device addresses built from Extents. PayloadSegment is the segment a
 * translated bounce mapping publishes. When preparation fails, __ReleaseLayout frees everything here.
 * On success the mapping takes over Extents, Bounce, PayloadSegment and
 * AddressMapping.
 */
struct __MappingLayout {
    SHMSG_t*                  Extents;
    int                       ExtentCount;
    bool                      UsesBounce;
    enum __BounceReason       BounceReason;
    struct __BouncePlan       BouncePlan;
    SHMDeviceBounce_t         Bounce;
    struct DMASegment         PayloadSegment;
    struct DMAAddressMapping* AddressMapping;
};

/**
 * @brief Plan the bounce storage of a direct mapping.
 *
 * Without an IOMMU the device address follows the physical address, so the
 * storage is one contiguous unit rounded up to CPU pages. The device alignment
 * rule therefore applies to the physical start of that unit. The device
 * reaches exactly the requested bytes.
 *
 * @return OS_EOK, OS_ENOTSUPPORTED if the alignment does not fit in size_t, or
 *         OS_EOVERFLOW if rounding the length overflows.
 */
static oserr_t
__PlanDirectBounce(
    _In_  size_t                           length,
    _In_  size_t                           pageSize,
    _In_  const struct DMAMapRequirements* requirements,
    _Out_ struct __BouncePlan*             plan)
{
    uint64_t alignment = requirements != NULL ? requirements->AddressAlignment : 0;

    if (alignment > SIZE_MAX) {
        WARNING("__PlanDirectBounce: alignment %llu is too large", (unsigned long long)alignment);
        return OS_ENOTSUPPORTED;
    }

    if (length > SIZE_MAX - (pageSize - 1)) {
        WARNING("__PlanDirectBounce: length %zu cannot be rounded up to pages of %zu", length, pageSize);
        return OS_EOVERFLOW;
    }

    plan->MappedLength = length;
    plan->UnitLength = DIVUP(length, pageSize) * pageSize;
    plan->UnitCount = 1;
    plan->AllocationLength = plan->UnitLength;
    plan->Alignment = (size_t)alignment;
    return OS_EOK;
}

/**
 * @brief Plan the bounce storage of a translated mapping and apply the size
 *        limit.
 *
 * The IOMMU maps whole granules, so the device reaches the request rounded up
 * to whole granules. That mapped length is compared with
 * SHM_DEVICE_TRANSLATED_BOUNCE_LIMIT before anything is allocated. Storage is
 * taken in units of the larger of the CPU page and the granule, so each unit
 * is a whole number of both. Each unit is aligned to its own size and nothing
 * more, because the device's alignment rule applies to the IOVA, not to the
 * physical pages behind it. Units do not have to be next to each other: the
 * IOMMU shows them to the device as one IOVA span.
 *
 * Every rounding and multiplication is checked, so the plan is also correct
 * where size_t is 32 bits.
 *
 * @return OS_EOK, OS_ENOTSUPPORTED if the granule does not fit in size_t,
 *         OS_EOVERFLOW if a size overflows, or OS_EBUFFER if the mapped length
 *         is above the limit. On OS_EBUFFER, plan->MappedLength holds the
 *         mapped length that was refused.
 */
static oserr_t
__PlanTranslatedBounce(
    _In_  size_t               length,
    _In_  uint64_t             granularity,
    _In_  size_t               pageSize,
    _Out_ struct __BouncePlan* plan)
{
    size_t granule;
    size_t unit;
    size_t unitCount;

    if (granularity > SIZE_MAX) {
        WARNING("__PlanTranslatedBounce: granule %llu does not fit in size_t", (unsigned long long)granularity);
        return OS_ENOTSUPPORTED;
    }

    granule = (size_t)granularity;
    if (length > SIZE_MAX - (granule - 1)) {
        WARNING("__PlanTranslatedBounce: length %zu cannot be rounded up to granules of %zu", length, granule);
        return OS_EOVERFLOW;
    }

    // A request above the limit is a normal result for the caller. It is not
    // logged here, because the caller reports it once per context.
    plan->MappedLength = DIVUP(length, granule) * granule;
    if (plan->MappedLength > SHM_DEVICE_TRANSLATED_BOUNCE_LIMIT) {
        return OS_EBUFFER;
    }

    unit = granule > pageSize ? granule : pageSize;
    if (length > SIZE_MAX - (unit - 1)) {
        WARNING("__PlanTranslatedBounce: length %zu cannot be rounded up to units of %zu", length, unit);
        return OS_EOVERFLOW;
    }

    unitCount = DIVUP(length, unit);
    if (unitCount > SIZE_MAX / unit) {
        WARNING("__PlanTranslatedBounce: %zu units of %zu bytes do not fit in size_t", unitCount, unit);
        return OS_EOVERFLOW;
    }

    plan->UnitLength = unit;
    plan->UnitCount = unitCount;
    plan->AllocationLength = unitCount * unit;
    plan->Alignment = unit;
    return OS_EOK;
}

/**
 * @brief Log a failed pool allocation of a bounce unit.
 *
 * A translated request that finds the pool full (OS_EOOM) is a normal result
 * for the caller. No driver splits its requests yet, so a busy driver can hit
 * it on every request. The first shortage of a context is a warning and later
 * ones are traced. SHMDeviceContextRelease logs the total. Any other pool
 * error, such as a missing pool, is an error every time.
 *
 * Direct bounce logs every failure as an error, as it did before translated
 * bounce existed. Its requests are not counted per context, so a quieter
 * level would leave no total in the log.
 */
static void
__ReportPoolFailure(
    _In_ SHMDeviceContext_t*        context,
    _In_ const struct __BouncePlan* plan,
    _In_ const SHMDeviceBounce_t*   bounce,
    _In_ size_t                     unit,
    _In_ oserr_t                    status)
{
    if (status != OS_EOOM || !bounce->Translated) {
        ERROR("__TakeBounceUnits: context %p SystemMemoryAllocateDevice unit %zu of %zu, length %zu failed: %u",
              context, unit, plan->UnitCount, plan->UnitLength, status);
        return;
    }

    if (SHMDeviceContextCountBouncePoolShortage(context)) {
        WARNING("__TakeBounceUnits: context %p has no pool storage for translated bounce unit %zu of %zu, length %zu. "
                "Later shortages on this context are only traced",
                context, unit, plan->UnitCount, plan->UnitLength);
        return;
    }

    TRACE("__TakeBounceUnits: context %p has no pool storage for translated bounce unit %zu of %zu, length %zu",
          context, unit, plan->UnitCount, plan->UnitLength);
}

/**
 * @brief Take the planned bounce units from the device pool.
 *
 * The pool is chosen at boot from memory that devices can reach. On ARM64 the
 * kernel views of it are uncached. The caller must still check the device
 * addresses after translating them. If the pool cannot supply a unit, the
 * mapping fails. Ordinary memory is not used as a fallback, because the device
 * may not be able to reach it.
 *
 * Each unit is recorded as soon as it is taken. On failure the record holds
 * exactly the units taken so far, and the caller's SHMDeviceReleaseBounce
 * returns them to the pool. Translated bounce units are counted in the
 * context's held bytes, including after a failure, because release takes them
 * off that count one unit at a time as they go back.
 *
 * @return OS_EOK, OS_EOVERFLOW if the record array size overflows, OS_EOOM if
 *         the record array cannot be allocated, or the pool allocator's error.
 */
static oserr_t
__TakeBounceUnits(
    _In_    SHMDeviceContext_t*        context,
    _In_    const struct __BouncePlan* plan,
    _InOut_ SHMDeviceBounce_t*         bounce)
{
    size_t  recordBytes;
    size_t  taken;
    oserr_t status = OS_EOK;

    if (plan->UnitCount > SIZE_MAX / sizeof(struct SystemMemoryAllocation)) {
        ERROR("__TakeBounceUnits: a record array for %zu units is too large", plan->UnitCount);
        return OS_EOVERFLOW;
    }

    recordBytes = plan->UnitCount * sizeof(struct SystemMemoryAllocation);
    bounce->Units = kmalloc(recordBytes);
    if (bounce->Units == NULL) {
        ERROR("__TakeBounceUnits: failed to allocate records for %zu units", plan->UnitCount);
        return OS_EOOM;
    }

    // The pool requires zeroed records, and release skips zeroed ones.
    memset(bounce->Units, 0, recordBytes);
    bounce->UnitCount = plan->UnitCount;
    for (taken = 0; taken < plan->UnitCount; taken++) {
        status = SystemMemoryAllocateDevice(
            &GetMachine()->PhysicalMemory,
            plan->UnitLength,
            plan->Alignment,
            0,
            &bounce->Units[taken]
        );
        if (status != OS_EOK) {
            __ReportPoolFailure(context, plan, bounce, taken, status);
            break;
        }
    }

    if (bounce->Translated) {
        SHMDeviceContextAddBounceStorage(context, taken * plan->UnitLength);
    }
    if (status != OS_EOK) {
        return status;
    }

    // Every unit comes from the same pool and shares its cache setting.
    bounce->Cached = bounce->Units[0].Cached;
    return OS_EOK;
}

/**
 * @brief Make the zeroed bounce storage what the device sees, before any
 *        translation to it exists.
 *
 * MAPPING_CLEAN zeroed the storage through the CPU. In cached storage those
 * zeros can still sit in the CPU cache, while a non-coherent device reads RAM
 * directly. Cleaning writes them to RAM, and invalidating drops the lines, so
 * no stale line can later be written back over bytes the device wrote. This
 * covers every unit in full, the padding included, because the device can
 * reach all of the mapped granules. The barrier then orders the zeroing
 * before the installation. Uncached and coherent storage need the barrier too.
 */
static void
__PublishBounceStorage(
    _In_ const SHMDeviceContext_t* context,
    _In_ const SHMDeviceBounce_t*  bounce)
{
    if (bounce->Cached && context->CachePolicy == SHMDeviceCacheNonCoherent) {
        for (size_t i = 0; i < bounce->UnitCount; i++) {
            CpuDataCacheCleanInvalidate(bounce->Units[i].PhysicalBase, bounce->Units[i].Length);
        }
    }
    dma_mb();
}

/**
 * @brief Map every bounce unit into one kernel view, in record order.
 *
 * MemorySpaceMap takes the pages as a list with one entry per CPU page. The
 * units can lie anywhere in the pool, so the list is filled unit by unit. The
 * view uses the pool's cache setting. MAPPING_CLEAN zeroes the whole storage
 * through the view, so a device cannot read data left by an earlier user.
 *
 * @return OS_EOK, OS_EOVERFLOW if the page list size overflows, OS_EOOM if the
 *         page list cannot be allocated, or the MemorySpaceMap error.
 */
static oserr_t
__MapBounceView(
    _In_    const struct __BouncePlan* plan,
    _In_    size_t                     pageSize,
    _InOut_ SHMDeviceBounce_t*         bounce)
{
    paddr_t*     pages;
    size_t       pageCount = plan->AllocationLength / pageSize;
    size_t       unitPages = plan->UnitLength / pageSize;
    unsigned int flags = MAPPING_COMMIT | MAPPING_PERSISTENT | MAPPING_CLEAN;
    oserr_t      status;

    if (pageCount > SIZE_MAX / sizeof(paddr_t)) {
        ERROR("__MapBounceView: a list of %zu pages is too large", pageCount);
        return OS_EOVERFLOW;
    }

    pages = kmalloc(pageCount * sizeof(paddr_t));
    if (pages == NULL) {
        ERROR("__MapBounceView: failed to allocate a list of %zu pages", pageCount);
        return OS_EOOM;
    }

    for (size_t i = 0; i < pageCount; i++) {
        const struct SystemMemoryAllocation* unit = &bounce->Units[i / unitPages];

        pages[i] = unit->PhysicalBase + ((paddr_t)(i % unitPages) * pageSize);
    }

    if (!bounce->Cached) {
        flags |= MAPPING_NOCACHE;
    }

    status = MemorySpaceMap(
        GetCurrentMemorySpace(),
        &(struct MemorySpaceMapOptions) {
            .Pages = pages,
            .Length = plan->AllocationLength,
            .Mask = __MASK,
            .Flags = flags,
            .PlacementFlags = MAPPING_PHYSICAL_FIXED | MAPPING_VIRTUAL_GLOBAL
        },
        &bounce->BounceMapping
    );
    kfree(pages);
    if (status != OS_EOK) {
        ERROR("__MapBounceView: MemorySpaceMap %zu units, length %zu failed: %u",
              plan->UnitCount, plan->AllocationLength, status);
        return status;
    }
    bounce->BounceMappingLength = plan->AllocationLength;
    return OS_EOK;
}

/**
 * @brief Replace the source extents with extents of the bounce storage.
 *
 * From here on the device uses the bounce storage and never the buffer's own
 * pages. The new extents cover the planned MappedLength bytes of the units, in
 * the order the kernel view maps them. Units that follow each other in
 * physical memory are joined into one extent, so one entry per unit is always
 * enough. The source extents are sized for the source range and can be too
 * short, so a separate array is allocated and the old one is freed.
 *
 * @return OS_EOK, OS_EOVERFLOW if the array size overflows, or OS_EOOM.
 */
static oserr_t
__UseBounceExtents(
    _InOut_ struct __MappingLayout* layout)
{
    SHMDeviceBounce_t* bounce = &layout->Bounce;
    SHMSG_t*           extents;
    size_t             remaining = layout->BouncePlan.MappedLength;
    int                count = 0;

    if (bounce->UnitCount > INT_MAX || bounce->UnitCount > SIZE_MAX / sizeof(SHMSG_t)) {
        ERROR("__UseBounceExtents: %zu units are too many extents", bounce->UnitCount);
        return OS_EOVERFLOW;
    }

    extents = kmalloc(bounce->UnitCount * sizeof(SHMSG_t));
    if (extents == NULL) {
        ERROR("__UseBounceExtents: failed to allocate %zu extents", bounce->UnitCount);
        return OS_EOOM;
    }

    for (size_t i = 0; i < bounce->UnitCount && remaining != 0; i++) {
        const struct SystemMemoryAllocation* unit = &bounce->Units[i];
        size_t                               chunk = unit->Length < remaining ? unit->Length : remaining;

        if (count != 0 && extents[count - 1].Address + extents[count - 1].Length == unit->PhysicalBase) {
            extents[count - 1].Length += chunk;
        } else {
            extents[count] = (SHMSG_t){ .Address = unit->PhysicalBase, .Length = chunk };
            count++;
        }
        remaining -= chunk;
    }

    kfree(layout->Extents);
    layout->Extents = extents;
    layout->ExtentCount = count;
    bounce->MappedLength = layout->BouncePlan.MappedLength;
    return OS_EOK;
}

/**
 * @brief Prepare bounce storage for a mapping in a direct context.
 *
 * Bounce pages are temporary pages that the device can reach. They are used
 * when the device cannot reach the buffer's own addresses, when the buffer
 * does not meet the segment alignment rules, or when cache invalidation would
 * hit neighboring bytes outside the range. The steps run in this order: plan
 * the storage, map the source view, take the pool unit, map and zero the
 * bounce view, then switch the extents to the bounce storage. Each step
 * records what it created in the layout, so __ReleaseLayout can undo a
 * failure at any point. The caller must still check the device addresses of
 * the bounce storage.
 */
static oserr_t
__PrepareBounce(
    _In_    struct SHMBuffer*                buffer,
    _In_    struct SHMDeviceContext*         context,
    _In_    size_t                           offset,
    _In_    size_t                           length,
    _In_    size_t                           pageSize,
    _In_    const struct DMAMapRequirements* requirements,
    _InOut_ struct __MappingLayout*          layout)
{
    oserr_t status;

    status = __PlanDirectBounce(length, pageSize, requirements, &layout->BouncePlan);
    if (status != OS_EOK) {
        return status;
    }

    status = __MapSourceView(buffer, offset, length, pageSize, &layout->Bounce);
    if (status != OS_EOK) {
        return status;
    }

    status = __TakeBounceUnits(context, &layout->BouncePlan, &layout->Bounce);
    if (status != OS_EOK) {
        return status;
    }

    status = __MapBounceView(&layout->BouncePlan, pageSize, &layout->Bounce);
    if (status != OS_EOK) {
        return status;
    }
    return __UseBounceExtents(layout);
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

    // The caller asked for the original pages only, and they cannot be used.
    // This is an expected answer, and ScDmaMapShm logs it when it is an error.
    TRACE("__CheckCacheLineFallback: context %p mapping kind %u cannot use bounce pages (walk status %u)",
          context, (unsigned int)kind, status);
    return status == OS_EOK ? OS_ENOTSUPPORTED : status;
}

/**
 * @brief Check whether the original pages are whole IOMMU granules that match
 *        the requested range exactly.
 *
 * The IOMMU maps only whole granules, so each extent must start on a granule
 * boundary and cover whole granules. The original pages are never rounded up.
 * The caller may have access to only part of a larger buffer, and the extra
 * bytes would expose the rest of it. The extents must also add up to exactly
 * the requested length. Subtracting from the remaining length as we go avoids
 * overflow in the sum. Nothing is logged here, because a range that is not
 * whole granules is a normal reason to bounce.
 *
 * @return true if the extents are whole granules covering exactly length bytes.
 */
static bool
__IsWholeGranules(
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
            return false;
        }
        remaining -= extents[i].Length;
    }
    return remaining == 0;
}

/**
 * @brief Check whether any extent reaches above an inclusive physical limit.
 *
 * The last byte is compared by subtracting from the limit, so the check works
 * for an extent that ends at the very top of the address space.
 */
static bool
__ReachesAbove(
    _In_ const SHMSG_t* extents,
    _In_ int            count,
    _In_ uint64_t       limit)
{
    for (int i = 0; i < count; i++) {
        uint64_t address = extents[i].Address;

        if (address > limit || extents[i].Length - 1 > limit - address) {
            return true;
        }
    }
    return false;
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
 * @brief Decide from the buffer's own pages whether a direct mapping needs
 *        bounce pages.
 *
 * Sets UsesBounce when cache lines are shared with neighboring bytes. Nothing
 * is allocated here. A mapping that cannot use bounce pages fails now, before
 * any work is done. Other reasons to bounce in a direct context show up only
 * when the device addresses are built, see __BuildLayout.
 *
 * @return OS_EOK, or OS_ENOTSUPPORTED if the pages cannot be used as they are
 *         and this mapping cannot use bounce pages. Address and segment errors
 *         from the original pages are returned unchanged.
 */
static oserr_t
__DecideDirectBounce(
    _In_    struct SHMBuffer*                buffer,
    _In_    struct SHMDeviceContext*         context,
    _In_    size_t                           offset,
    _In_    size_t                           length,
    _In_    enum DMADirection                direction,
    _In_    enum SHMDeviceMappingKind        kind,
    _In_    const struct DMAMapRequirements* requirements,
    _InOut_ struct __MappingLayout*          layout)
{
    layout->UsesBounce = __SharesCacheLines(buffer, context, offset, length, direction);
    if (!layout->UsesBounce) {
        return OS_EOK;
    }
    layout->BounceReason = __BounceSharedCacheLines;

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

// Find the first reason the original pages cannot be used in a translated
// context, or __BounceNotNeeded when they can.
static enum __BounceReason
__FindTranslatedBounceReason(
    _In_ struct SHMBuffer*        buffer,
    _In_ struct SHMDeviceContext* context,
    _In_ size_t                   offset,
    _In_ size_t                   length,
    _In_ enum DMADirection        direction,
    _In_ uint64_t                 granularity,
    _In_ const SHMSG_t*           extents,
    _In_ int                      extentCount)
{
    if (!__IsWholeGranules(extents, extentCount, length, granularity)) {
        return __BouncePartialGranules;
    }

    if (__ReachesAbove(extents, extentCount, DMAAddressSpaceGetPhysicalLimit(context->AddressSpace))) {
        return __BounceAbovePhysicalLimit;
    }

    // An exact granule layout starts on a granule boundary and covers whole
    // granules, and a granule is never smaller than a cache line. This check
    // stays as a guard in case either of those ever changes.
    if (__SharesCacheLines(buffer, context, offset, length, direction)) {
        return __BounceSharedCacheLines;
    }
    return __BounceNotNeeded;
}

/**
 * @brief Log a translated bounce request refused by the size limit.
 *
 * The refusal is a normal result for the caller. No driver splits its
 * requests yet, so a driver that sends large unaligned requests is refused on
 * each of them. The first refusal of a context is a warning and later ones
 * are traced. SHMDeviceContextRelease logs the total.
 */
static void
__ReportLimitRefusal(
    _In_ SHMDeviceContext_t*        context,
    _In_ size_t                     length,
    _In_ const struct __BouncePlan* plan)
{
    if (SHMDeviceContextCountBounceLimitRefusal(context)) {
        WARNING("__DecideTranslatedBounce: context %p length %zu maps to %zu bytes, above the bounce limit of %u. "
                "Later refusals on this context are only traced",
                context, length, plan->MappedLength, (unsigned int)SHM_DEVICE_TRANSLATED_BOUNCE_LIMIT);
        return;
    }

    TRACE("__DecideTranslatedBounce: context %p length %zu maps to %zu bytes, above the bounce limit of %u",
          context, length, plan->MappedLength, (unsigned int)SHM_DEVICE_TRANSLATED_BOUNCE_LIMIT);
}

/**
 * @brief Decide from the buffer's own pages whether a translated mapping needs
 *        bounce storage.
 *
 * The original pages can be used when they are whole granules that match the
 * range, lie at or below the IOMMU unit's physical limit, and share no cache
 * line with bytes outside the range. Otherwise a streaming mapping that allows
 * bounce copies the whole range through private storage. Its storage is
 * planned here, so the size limit applies before anything is allocated. This
 * is the only place a translated layout is chosen. A failure after this point
 * is returned to the caller and never retried with another layout.
 *
 * Other mapping kinds cannot bounce. Pages that are not whole granules or that
 * share cache lines fail with OS_ENOTSUPPORTED. Pages above the physical limit
 * fail with OS_ENOENT for a streaming request for the original pages only
 * (SHMDeviceMappingStreamingDirect), because the device cannot reach them.
 * Persistent and region mappings of such pages are left to the address core,
 * which rejects them with OS_EINVALPARAMS when it prepares them.
 *
 * @return OS_EOK, OS_ENOTSUPPORTED for a kind that cannot bounce, OS_ENOENT
 *         for a streaming direct request above the physical limit, or the
 *         error from planning the bounce storage, including OS_EBUFFER for a
 *         mapping above SHM_DEVICE_TRANSLATED_BOUNCE_LIMIT.
 */
static oserr_t
__DecideTranslatedBounce(
    _In_    struct SHMBuffer*         buffer,
    _In_    struct SHMDeviceContext*  context,
    _In_    size_t                    offset,
    _In_    size_t                    length,
    _In_    enum DMADirection         direction,
    _In_    enum SHMDeviceMappingKind kind,
    _In_    uint64_t                  granularity,
    _InOut_ struct __MappingLayout*   layout)
{
    enum __BounceReason reason;
    oserr_t             status;

    reason = __FindTranslatedBounceReason(
        buffer,
        context,
        offset,
        length,
        direction,
        granularity,
        layout->Extents,
        layout->ExtentCount
    );
    if (reason == __BounceNotNeeded) {
        return OS_EOK;
    }

    if (kind == SHMDeviceMappingStreamingBounce) {
        layout->UsesBounce = true;
        layout->BounceReason = reason;
        status = __PlanTranslatedBounce(length, granularity, GetMemorySpacePageSize(), &layout->BouncePlan);
        if (status == OS_EBUFFER) {
            __ReportLimitRefusal(context, length, &layout->BouncePlan);
        }
        return status;
    }

    // A streaming request for the original pages only gets OS_ENOENT, the
    // answer DMAMapSHMDirect gives when the device cannot reach the pages.
    // DMAMapSHMChunk falls back to a bounce mapping on it. Persistent and
    // region mappings go on to the address core, which rejects the pages.
    if (reason == __BounceAbovePhysicalLimit) {
        if (kind != SHMDeviceMappingStreamingDirect) {
            return OS_EOK;
        }
        TRACE("__DecideTranslatedBounce: buffer %u offset %zu, length %zu %s, and the request allows no bounce",
              buffer->ID, offset, length, __BounceReasonName(reason));
        return OS_ENOENT;
    }

    // The caller asked for the original pages only, and they cannot be used.
    // This is an expected answer, and ScDmaMapShm logs it when it is an error.
    TRACE("__DecideTranslatedBounce: buffer %u offset %zu, length %zu %s, and mapping kind %u cannot bounce",
          buffer->ID, offset, length, __BounceReasonName(reason), (unsigned int)kind);
    return OS_ENOTSUPPORTED;
}

/**
 * @brief Decide from the buffer's own pages whether bounce storage is required.
 *
 * Translated and direct contexts have different rules, see
 * __DecideTranslatedBounce and __DecideDirectBounce.
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

    if (granularity != 0) {
        return __DecideTranslatedBounce(
            buffer,
            context,
            offset,
            length,
            direction,
            kind,
            granularity,
            layout
        );
    }

    return __DecideDirectBounce(
        buffer,
        context,
        offset,
        length,
        direction,
        kind,
        requirements,
        layout
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
 * @brief Prepare the device addresses of translated bounce storage.
 *
 * The address mapping covers whole granules, so it is longer than the
 * request. LengthAlignment was already checked against the request, which is
 * the length the driver programs, so it is left out here. The padded length is
 * a whole number of granules and needs no such check. AddressAlignment still
 * applies, to the IOVA of the span.
 */
static oserr_t
__PrepareBounceAddressMapping(
    _In_    struct SHMDeviceContext*         context,
    _In_    enum DMADirection                direction,
    _In_    const struct DMAMapRequirements* requirements,
    _InOut_ struct __MappingLayout*          layout)
{
    struct DMAMapRequirements padded = { 0 };

    if (requirements != NULL) {
        padded.AddressAlignment = requirements->AddressAlignment;
    }
    return __PrepareAddressMapping(context, direction, &padded, layout);
}

/**
 * @brief Build the one device segment a translated bounce mapping publishes.
 *
 * The address mapping is one IOVA span over whole granules. The segment
 * starts at the payload inside that span and is exactly as long as the
 * request, so the driver never programs the padding. The segment is checked
 * against the span and against the caller's requirements before use.
 *
 * @return OS_EOK, or OS_ENOTSUPPORTED if the segment does not fit the span or
 *         does not meet the requirements.
 */
static oserr_t
__BuildPayloadSegment(
    _In_    size_t                           length,
    _In_    const struct DMAMapRequirements* requirements,
    _InOut_ struct __MappingLayout*          layout)
{
    const struct DMASegment* span;
    uint32_t                 count;
    size_t                   offset = __SHMDeviceBouncePayloadOffset(&layout->Bounce);
    uint64_t                 addressAlignment = requirements != NULL ? requirements->AddressAlignment : 0;
    uint64_t                 lengthAlignment = requirements != NULL ? requirements->LengthAlignment : 0;

    span = DMAAddressMappingSegments(layout->AddressMapping, &count);
    if (count != 1 || offset > span->Length || length > span->Length - offset) {
        ERROR("__BuildPayloadSegment: payload at %zu, length %zu does not fit %u segments",
              offset, length, count);
        return OS_ENOTSUPPORTED;
    }

    layout->PayloadSegment.Address = span->Address + offset;
    layout->PayloadSegment.Length = length;
    if ((addressAlignment != 0 && (layout->PayloadSegment.Address & (addressAlignment - 1)) != 0) ||
        (lengthAlignment != 0 && (layout->PayloadSegment.Length & (lengthAlignment - 1)) != 0)) {
        ERROR("__BuildPayloadSegment: payload %llx, length %zu breaks alignment %llu/%llu",
              (unsigned long long)layout->PayloadSegment.Address, length,
              (unsigned long long)addressAlignment, (unsigned long long)lengthAlignment);
        return OS_ENOTSUPPORTED;
    }
    return OS_EOK;
}

/**
 * @brief Prepare translated bounce storage for a mapping and its device
 *        addresses.
 *
 * The storage was planned, and the size limit applied, when bounce was
 * selected. The steps run in this order: map the source view, take the units
 * from the pool, map and zero the bounce view, make the zeros visible to the
 * device, switch the extents to the bounce storage, prepare the address mapping
 * over whole granules, then build the payload segment. The storage is
 * initialized before the address mapping exists, so the device can never read
 * anything but zeros or this mapping's own bytes. Each step records what it
 * created in the layout, so __ReleaseLayout can undo a failure at any point.
 * Nothing is copied from the source here. Copies need the range reservation
 * that a transfer begin takes.
 */
static oserr_t
__PrepareTranslatedBounce(
    _In_    struct SHMBuffer*                buffer,
    _In_    struct SHMDeviceContext*         context,
    _In_    size_t                           offset,
    _In_    size_t                           length,
    _In_    enum DMADirection                direction,
    _In_    const struct DMAMapRequirements* requirements,
    _InOut_ struct __MappingLayout*          layout)
{
    size_t  pageSize = GetMemorySpacePageSize();
    oserr_t status;

    layout->Bounce.Translated = true;
    status = __MapSourceView(buffer, offset, length, pageSize, &layout->Bounce);
    if (status != OS_EOK) {
        return status;
    }

    status = __TakeBounceUnits(context, &layout->BouncePlan, &layout->Bounce);
    if (status != OS_EOK) {
        return status;
    }

    status = __MapBounceView(&layout->BouncePlan, pageSize, &layout->Bounce);
    if (status != OS_EOK) {
        return status;
    }
    __PublishBounceStorage(context, &layout->Bounce);

    status = __UseBounceExtents(layout);
    if (status != OS_EOK) {
        return status;
    }

    // The address mapping logs its own failures.
    status = __PrepareBounceAddressMapping(context, direction, requirements, layout);
    if (status != OS_EOK) {
        return status;
    }
    return __BuildPayloadSegment(length, requirements, layout);
}

/**
 * @brief Build the device addresses for the layout, using bounce storage if
 *        needed.
 *
 * A translated mapping that bounces was planned by __DecideBounceUse, before
 * anything was allocated, and is prepared here directly. Otherwise the
 * buffer's own pages are tried first. A SHMDeviceMappingStreamingBounce
 * mapping in a direct context falls back to bounce pages when the device
 * cannot address those pages or their segments break its limits. A
 * translated context never falls back: once the original pages were chosen,
 * a failure such as IOVA exhaustion is returned as it is. After a fallback,
 * Extents is replaced by the extents of the bounce pages.
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

    if (layout->UsesBounce && granularity != 0) {
        return __PrepareTranslatedBounce(
            buffer,
            context,
            offset,
            length,
            direction,
            requirements,
            layout
        );
    }

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
        layout->BounceReason = status == OS_ENOENT ? __BounceUnreachable : __BounceBreaksAlignment;
    }

    status = __PrepareBounce(
        buffer,
        context,
        offset,
        length,
        GetMemorySpacePageSize(),
        requirements,
        layout
    );
    if (status != OS_EOK) {
        return status;
    }

    // The address mapping logs its own failures.
    return __PrepareAddressMapping(context, direction, requirements, layout);
}

// Count the bounce bytes whose units are still out of the pool.
static size_t
__BounceBytesAllocated(
    _In_ const SHMDeviceBounce_t* bounce)
{
    size_t bytes = 0;

    for (size_t i = 0; i < bounce->UnitCount; i++) {
        if (bounce->Units[i].Owner != NULL) {
            bytes += bounce->Units[i].Length;
        }
    }
    return bytes;
}

/**
 * @brief Release the layout of a mapping that failed before it was installed.
 *
 * Installing has not happened, so the device never saw this layout. Releasing
 * a prepared address mapping should never fail. If it does, log it and stop,
 * because the bounce pages and the extent list must not be freed while
 * something may still point at them.
 *
 * The bounce storage is released next. Its kernel views and pool calls cannot
 * fail in a healthy kernel. If one does, the units that could not be freed are
 * quarantined. They stay out of the pool for good, so no later pool user can
 * share pages with a view that may still exist, and they keep counting in the
 * context's held bytes. Their record array is not freed either. The layout goes
 * away with this call, so nothing refers to them afterwards. The pages are lost
 * on purpose and can never be handed out again. No device ever had a
 * translation to them, so nothing has to be stopped, the binding stays open and
 * the context stays usable.
 *
 * @param originalStatus The error that caused the release. Only used for logging.
 */
static void
__ReleaseLayout(
    _In_    SHMDeviceContext_t*     context,
    _InOut_ struct __MappingLayout* layout,
    _In_    oserr_t                 originalStatus)
{
    oserr_t status;

    if (layout->AddressMapping != NULL) {
        status = DMAAddressMappingRelease(&layout->AddressMapping);
        if (status != OS_EOK) {
            // Everything behind the address mapping stays allocated: the
            // bounce storage keeps counting in the held bytes, and the views
            // and the extent list are left as they are.
            ERROR("SHMDeviceMappingPrepare: context %p prepared address release failed, bounce storage and extents are kept: %u (original status %u)",
                  context, status, originalStatus);
            assert(status == OS_EOK);
            return;
        }
    }

    status = SHMDeviceReleaseBounce(context, &layout->Bounce);
    if (status != OS_EOK) {
        ERROR("SHMDeviceMappingPrepare: context %p bounce release failed, %zu bytes left allocated: %u (original status %u)",
              context, __BounceBytesAllocated(&layout->Bounce), status, originalStatus);
    }

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
    mapping->Cached = layout->UsesBounce ? layout->Bounce.Cached : !SHMBufferUncached(buffer);
    mapping->Kind = kind;
    mapping->State = SHMDeviceMappingPreparing;

    if (__SHMDeviceMappingIsStreaming(mapping)) {
        mapping->Stream = (SHMDeviceStream_t) {
            .Bounce = layout->Bounce,
            .PayloadSegment = layout->PayloadSegment
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
 * @brief Count a prepared translated bounce mapping and report it.
 *
 * Each copied request costs pool storage and two copies, so a context never
 * bounces without a record. This runs when preparation succeeds, before the
 * device addresses are installed, so a mapping that later fails installation
 * is still counted and reported. The first translated bounce mapping of a
 * context is logged as a warning with the reason, the requested length, the
 * granule and the mapped length. Later ones are traced, because a storage
 * driver can bounce every request and a warning for each would flood the log.
 */
static void
__ReportTranslatedBounce(
    _In_ SHMDeviceContext_t*           context,
    _In_ size_t                        length,
    _In_ const struct __MappingLayout* layout)
{
    if (SHMDeviceContextCountBounceMapping(context)) {
        WARNING("SHMDeviceMappingPrepare: context %p copies a range that %s through translated bounce storage: "
                "length %zu, granule %llu, mapped %zu. Later copies on this context are only traced",
                context, __BounceReasonName(layout->BounceReason), length,
                (unsigned long long)DMAAddressSpaceGetGranularity(context->AddressSpace),
                layout->Bounce.MappedLength);
        return;
    }

    TRACE("SHMDeviceMappingPrepare: context %p copies a range that %s through translated bounce storage: "
          "length %zu, granule %llu, mapped %zu",
          context, __BounceReasonName(layout->BounceReason), length,
          (unsigned long long)DMAAddressSpaceGetGranularity(context->AddressSpace),
          layout->Bounce.MappedLength);
}

/**
 * @brief Report the first bounce mapping of a direct context.
 *
 * Without an IOMMU a bounce mapping copies the range through one contiguous
 * piece of the device pool. Drivers ask for the original pages first, so the
 * refusals before a bounce are only traced. This line is what tells an
 * operator that the device bounces, and why. It is logged as a warning once
 * per context. Later bounce mappings on the context are only traced, because
 * a storage driver can bounce every request.
 */
static void
__ReportDirectBounce(
    _In_ SHMDeviceContext_t*           context,
    _In_ const struct __MappingLayout* layout)
{
    // A kernel log line holds at most 127 characters. The longest reason
    // still fits with a 64-bit pointer and limit.
    if (SHMDeviceContextCountDirectBounceMapping(context)) {
        WARNING("SHMDeviceMappingPrepare: context %p bounces: range %s, limit 0x%llx",
                context, __BounceReasonName(layout->BounceReason),
                (unsigned long long)context->AddressLimit);
        return;
    }

    TRACE("SHMDeviceMappingPrepare: context %p bounces: range %s",
          context, __BounceReasonName(layout->BounceReason));
}

/**
 * @brief Prepare and check a buffer range. The device cannot use it yet.
 *
 * The caller, __MapBuffer, already holds the buffer and context references
 * this needs. This function records the physical extents that cache syncs use
 * later and builds the device addresses. A direct context may switch to bounce
 * pages when the device cannot reach the original pages or their layout breaks
 * the device's limits. A translated context (one behind an IOMMU) uses the
 * original pages only when they are whole granules that match the range
 * exactly. They are never rounded up. Otherwise a streaming mapping that
 * allows bounce copies the range through private storage of whole granules,
 * chosen from the original pages before anything is allocated.
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

    __FillMapping(
        mapping,
        buffer,
        context,
        offset,
        length,
        direction,
        kind,
        &layout
    );
    if (kind == SHMDeviceMappingRegions) {
        // Look up the registered regions covering this range, verify each
        // region's segments meet alignment rules, and set up fault tracking.
        status = SHMDeviceRegionsPrepare(mapping, requirements);
        if (status != OS_EOK) {
            goto cleanup;
        }
    }

    if (layout.Bounce.Translated) {
        __ReportTranslatedBounce(context, length, &layout);
    } else if (layout.UsesBounce) {
        __ReportDirectBounce(context, &layout);
    }

    *mappingOut = mapping;
    return OS_EOK;

cleanup:
    __ReleaseLayout(context, &layout, status);
    if (mapping != NULL) {
        kfree(mapping);
    }
    return status;
}
