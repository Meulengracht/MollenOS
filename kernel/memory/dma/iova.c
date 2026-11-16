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

#define __need_minmax
#include "iova.h"
#include <debug.h>
#include <ds/rbtree.h>
#include <heap.h>
#include <inttypes.h>
#include <mutex.h>

/** A used address range. The tree is sorted by the First field of Range. */
struct __DMAIOVAInterval {
    rb_leaf_t      Node;
    DMAIOVARange_t Range;
};

/**
 * The tree holds both the reserved ranges and the live allocations. Free space
 * is not stored. It is simply the gap between two neighboring entries, so
 * releasing an allocation never needs to allocate memory.
 *
 * Lock must be held for a whole search and insert, and for as long as a node
 * returned by a tree lookup is in use. The reserved entries live in the same
 * memory block as the allocator and are only freed when it is destroyed.
 */
struct DMAIOVAAllocator {
    Mutex_t                  Lock;
    rb_tree_t                Intervals;
    DMAIOVARange_t           Bounds;
    uint64_t                 Granule;
    size_t                   ActiveCount;
    struct __DMAIOVAInterval Reserved[];
};

/** One live allocation. The caller keeps it until its addresses are safe to reuse. */
struct DMAIOVAAllocation {
    struct __DMAIOVAInterval Interval;
    DMAIOVAAllocator_t*      Allocator;
};

static int
__CompareAddresses(
    _In_ void* left,
    _In_ void* right)
{
    uint64_t first = *(const uint64_t*)left;
    uint64_t second = *(const uint64_t*)right;

    // The keys are read as 64-bit values, as a pointer-sized key would cut
    // addresses short on i386. Subtracting them is also wrong, as the result
    // may not fit in an int.
    if (first < second) {
        return -1;
    }
    return first > second ? 1 : 0;
}

static bool
__WholeGranules(
    _In_ const DMAIOVARange_t* range,
    _In_ uint64_t              granule)
{
    uint64_t mask = granule - 1;

    // Last is checked directly. Computing Last + 1 would wrap around when
    // Last is UINT64_MAX.
    return range->First <= range->Last &&
        (range->First & mask) == 0 && (range->Last & mask) == mask;
}

static oserr_t
__InsertInterval(
    _In_ DMAIOVAAllocator_t*       allocator,
    _In_ struct __DMAIOVAInterval* interval)
{
    int result;

    RB_LEAF_INIT(&interval->Node, &interval->Range.First, interval);
    result = rb_tree_append(&allocator->Intervals, &interval->Node);
    if (result != 0) {
        ERROR("DMA IOVA: insert [0x%" PRIx64 ", 0x%" PRIx64 "] failed: %i",
              interval->Range.First, interval->Range.Last, result);
        return OS_EUNKNOWN;
    }
    return OS_EOK;
}

static bool
__OverlapsReservation(
    _In_ DMAIOVAAllocator_t* allocator,
    _In_ DMAIOVARange_t*     range)
{
    rb_leaf_t*                node;
    struct __DMAIOVAInterval* interval;

    // Existing entries never overlap each other, so only the two entries next
    // to the new range need checking. One starts at or before the new First,
    // the other starts at or after it. An entry with the same First is
    // reported as an overlap too.
    node = rb_tree_floor(&allocator->Intervals, &range->First);
    if (node != NULL) {
        interval = node->value;
        if (interval->Range.Last >= range->First) {
            return true;
        }
    }

    node = rb_tree_ceiling(&allocator->Intervals, &range->First);
    if (node != NULL) {
        interval = node->value;
        if (interval->Range.First <= range->Last) {
            return true;
        }
    }
    return false;
}

oserr_t
DMAIOVAAllocatorCreate(
    _In_    const DMAIOVAConfig_t* config,
    _InOut_ DMAIOVAAllocator_t**   allocatorOut)
{
    DMAIOVAAllocator_t* allocator;
    size_t              maxReserved;
    size_t              bytes;
    oserr_t             status;

    if (config == NULL || allocatorOut == NULL) {
        return OS_EINVALPARAMS;
    }

    if (*allocatorOut != NULL) {
        return OS_EBUSY;
    }

    if (config->Granule == 0 || (config->Granule & (config->Granule - 1)) != 0 ||
        !__WholeGranules(&config->Bounds, config->Granule) ||
        (config->ReservedCount != 0 && config->Reserved == NULL)) {
        return OS_EINVALPARAMS;
    }

    maxReserved = (SIZE_MAX - sizeof(DMAIOVAAllocator_t)) / sizeof(struct __DMAIOVAInterval);
    if (config->ReservedCount > maxReserved) {
        return OS_EOVERFLOW;
    }

    bytes = sizeof(DMAIOVAAllocator_t) + config->ReservedCount * sizeof(struct __DMAIOVAInterval);
    allocator = kmalloc(bytes);
    if (allocator == NULL) {
        return OS_EOOM;
    }

    MutexConstruct(&allocator->Lock, MUTEX_FLAG_PLAIN);
    rb_tree_construct_cmp(&allocator->Intervals, __CompareAddresses);
    allocator->Bounds = config->Bounds;
    allocator->Granule = config->Granule;
    allocator->ActiveCount = 0;

    // Nobody else can see the allocator yet, so no lock is needed here. The
    // reserved entries are part of the same memory block, so on failure there
    // is nothing else to free.
    for (uint32_t i = 0; i < config->ReservedCount; i++) {
        struct __DMAIOVAInterval* interval = &allocator->Reserved[i];

        interval->Range = config->Reserved[i];
        if (!__WholeGranules(&interval->Range, allocator->Granule) ||
            interval->Range.First < allocator->Bounds.First ||
            interval->Range.Last > allocator->Bounds.Last ||
            __OverlapsReservation(allocator, &interval->Range)) {
            status = OS_EINVALPARAMS;
            goto cleanup;
        }

        status = __InsertInterval(allocator, interval);
        if (status != OS_EOK) {
            goto cleanup;
        }
    }

    *allocatorOut = allocator;
    return OS_EOK;

cleanup:
    MutexDestruct(&allocator->Lock);
    kfree(allocator);
    return status;
}

static bool
__AlignAddress(
    _In_  uint64_t  address,
    _In_  uint64_t  alignment,
    _Out_ uint64_t* alignedOut)
{
    uint64_t mask = alignment - 1;
    uint64_t padding = (alignment - (address & mask)) & mask;

    if (padding > UINT64_MAX - address) {
        return false;
    }
    *alignedOut = address + padding;
    return true;
}

/**
 * Try to place the request inside the free gap from first to last (both included).
 * The request values must already be validated. Returns true and sets rangeOut
 * when the block fits.
 */
static bool
__FitGap(
    _In_  uint64_t                first,
    _In_  uint64_t                last,
    _In_  uint64_t                alignment,
    _In_  const DMAIOVARequest_t* request,
    _Out_ DMAIOVARange_t*         rangeOut)
{
    uint64_t candidate;

    if (!__AlignAddress(first, alignment, &candidate)) {
        return false;
    }

    if (request->Boundary != 0) {
        uint64_t remaining = request->Boundary - 1 - (candidate & (request->Boundary - 1));

        if (request->Length - 1 > remaining) {
            uint64_t advance = remaining + 1;

            if (advance > UINT64_MAX - candidate) {
                return false;
            }

            // The block would cross a boundary, so move to the start of the
            // next one and align again. Alignment and Boundary are both powers
            // of two, so the larger is a multiple of the smaller. Aligning
            // therefore lands on a boundary start again and the block fits.
            if (!__AlignAddress(candidate + advance, alignment, &candidate)) {
                return false;
            }
        }
    }

    if (candidate > last || request->Length - 1 > last - candidate) {
        return false;
    }

    rangeOut->First = candidate;
    rangeOut->Last = candidate + (request->Length - 1);
    return true;
}

/**
 * Find the lowest free block that matches the request. The caller must hold
 * Lock during this search and during the insert that uses the result.
 */
static bool
__FindRange(
    _In_  DMAIOVAAllocator_t*     allocator,
    _In_  const DMAIOVARequest_t* request,
    _Out_ DMAIOVARange_t*         rangeOut)
{
    uint64_t   first = allocator->Bounds.First;
    uint64_t   limit = MIN(allocator->Bounds.Last, request->Limit);
    uint64_t   alignment = MAX(allocator->Granule, request->Alignment);
    rb_leaf_t* node;

    if (first > limit || (request->Boundary != 0 && request->Length > request->Boundary)) {
        return false;
    }

    // Walk the used ranges from lowest to highest and try each gap in between.
    // Gaps are not stored, so two neighboring free gaps join up on their own
    // when an allocation is released.
    node = rb_tree_minimum(&allocator->Intervals);
    while (node != NULL) {
        struct __DMAIOVAInterval* interval = node->value;

        if (first < interval->Range.First &&
            __FitGap(first, MIN(limit, interval->Range.First - 1), alignment, request, rangeOut)) {
            return true;
        }

        if (interval->Range.Last >= limit) {
            // No room left below the limit. Stopping here also avoids adding
            // one to UINT64_MAX below, which would wrap around.
            return false;
        }

        first = interval->Range.Last + 1;
        node = rb_tree_next(&allocator->Intervals, node);
    }
    return __FitGap(first, limit, alignment, request, rangeOut);
}

oserr_t
DMAIOVAAllocate(
    _In_    DMAIOVAAllocator_t*     allocator,
    _In_    const DMAIOVARequest_t* request,
    _InOut_ DMAIOVAAllocation_t**   allocationOut)
{
    DMAIOVAAllocation_t* allocation;
    oserr_t              status;

    if (allocator == NULL || request == NULL || allocationOut == NULL) {
        return OS_EINVALPARAMS;
    }

    if (*allocationOut != NULL) {
        return OS_EBUSY;
    }

    if (request->Length == 0 || (request->Length & (allocator->Granule - 1)) != 0 ||
        (request->Alignment & (request->Alignment - 1)) != 0 ||
        (request->Boundary & (request->Boundary - 1)) != 0) {
        return OS_EINVALPARAMS;
    }

    // Allocate before taking the lock so the heap is not called with it held.
    // This also means a memory failure is reported before a no-fit error.
    allocation = kmalloc(sizeof(DMAIOVAAllocation_t));
    if (allocation == NULL) {
        return OS_EOOM;
    }

    allocation->Allocator = allocator;

    MutexLock(&allocator->Lock);
    if (allocator->ActiveCount == SIZE_MAX) {
        status = OS_EOVERFLOW;
        goto cleanup;
    }

    if (!__FindRange(allocator, request, &allocation->Interval.Range)) {
        status = OS_ENOENT;
        goto cleanup;
    }

    status = __InsertInterval(allocator, &allocation->Interval);
    if (status != OS_EOK) {
        goto cleanup;
    }
    allocator->ActiveCount++;
    MutexUnlock(&allocator->Lock);

    *allocationOut = allocation;
    return OS_EOK;

cleanup:
    MutexUnlock(&allocator->Lock);
    kfree(allocation);
    return status;
}

const DMAIOVARange_t*
DMAIOVAAllocationRange(
    _In_ const DMAIOVAAllocation_t* allocation)
{
    return &allocation->Interval.Range;
}

oserr_t
DMAIOVAAllocationRelease(
    _InOut_ DMAIOVAAllocation_t** allocation)
{
    DMAIOVAAllocation_t* released;
    DMAIOVAAllocator_t*  allocator;
    rb_leaf_t*           node;

    if (allocation == NULL || *allocation == NULL) {
        return OS_EOK;
    }

    released = *allocation;
    allocator = released->Allocator;

    MutexLock(&allocator->Lock);
    node = rb_tree_lookup(&allocator->Intervals, &released->Interval.Range.First);
    if (node != &released->Interval.Node || allocator->ActiveCount == 0) {
        ERROR("DMA IOVA: release [0x%" PRIx64 ", 0x%" PRIx64 "] does not match a live interval",
              released->Interval.Range.First, released->Interval.Range.Last);
        MutexUnlock(&allocator->Lock);
        return OS_EUNKNOWN;
    }

    node = rb_tree_remove(&allocator->Intervals, &released->Interval.Range.First);
    if (node != &released->Interval.Node) {
        ERROR("DMA IOVA: remove [0x%" PRIx64 ", 0x%" PRIx64 "] failed",
              released->Interval.Range.First, released->Interval.Range.Last);
        MutexUnlock(&allocator->Lock);
        return OS_EUNKNOWN;
    }
    allocator->ActiveCount--;
    MutexUnlock(&allocator->Lock);

    // The caller has already made sure the addresses are safe to reuse. Free
    // the memory after dropping the lock. The allocator must not be destroyed
    // until this call has returned.
    kfree(released);
    *allocation = NULL;
    return OS_EOK;
}

oserr_t
DMAIOVAAllocatorDestroy(
    _InOut_ DMAIOVAAllocator_t** allocator)
{
    DMAIOVAAllocator_t* released;

    if (allocator == NULL || *allocator == NULL) {
        return OS_EOK;
    }

    released = *allocator;

    MutexLock(&released->Lock);
    if (released->ActiveCount != 0) {
        MutexUnlock(&released->Lock);
        return OS_EBUSY;
    }
    MutexUnlock(&released->Lock);

    // Only the reserved entries are left in the tree, and they are freed with
    // the allocator. No other thread may still be inside an allocator call,
    // including a release that has updated the count but not yet returned.
    MutexDestruct(&released->Lock);
    kfree(released);

    *allocator = NULL;
    return OS_EOK;
}
