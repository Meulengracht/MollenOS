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

#include <device_pool.h>
#include <limits.h>

static struct DevicePool* g_systemPool;

static bool
__IsPowerOfTwoOrZero(
    _In_ size_t value)
{
    return (value & (value - 1)) == 0;
}

static size_t
__PageCount(
    _In_ const struct DevicePool* pool)
{
    return (size_t)pool->Pages.total;
}

oserr_t
DevicePoolConstruct(
    _In_ struct DevicePool* pool,
    _In_ paddr_t            base,
    _In_ size_t             length,
    _In_ size_t             pageSize,
    _In_ bool               cached,
    _In_ void*              bitmapStorage)
{
    size_t pageCount;

    if (pool == NULL || bitmapStorage == NULL) {
        return OS_EINVALPARAMS;
    }
    if (pageSize == 0 || !__IsPowerOfTwoOrZero(pageSize)) {
        return OS_EINVALPARAMS;
    }

    // Pages are handed out whole, so the pool must start and end on page
    // boundaries. Its last byte must also be a valid physical address.
    if (length == 0 || base % pageSize != 0 || length % pageSize != 0) {
        return OS_EINVALPARAMS;
    }
    if (length - 1 > (paddr_t)~(paddr_t)0 - base) {
        return OS_EINVALPARAMS;
    }

    // The shared bitmap helpers count pages with an int.
    pageCount = length / pageSize;
    if (pageCount > INT_MAX) {
        return OS_ENOTSUPPORTED;
    }

    pool->Base = base;
    pool->PageSize = pageSize;
    pool->Cached = cached;
    bitmap_construct(&pool->Pages, (int)pageCount, bitmapStorage);
    SpinlockConstruct(&pool->Lock);
    return OS_EOK;
}

/**
 * @brief Find the first page whose physical address is a multiple of alignment.
 *
 * The pool's base need not itself be aligned to a large alignment, so the
 * search must start at the first page that is.
 */
static size_t
__FirstAlignedIndex(
    _In_ const struct DevicePool* pool,
    _In_ size_t                   alignment)
{
    size_t misalignment = (size_t)(pool->Base % alignment);

    if (misalignment == 0) {
        return 0;
    }
    return (alignment - misalignment) / pool->PageSize;
}

/**
 * @brief Tell whether length bytes starting at a page cross a boundary.
 *
 * Bytes cross a boundary when the first and last differ in the address bits
 * above the boundary size, the same rule SHMDeviceAllocate uses.
 */
static bool
__CrossesBoundary(
    _In_ const struct DevicePool* pool,
    _In_ size_t                   index,
    _In_ size_t                   length,
    _In_ size_t                   boundary)
{
    paddr_t first;
    paddr_t last;

    if (boundary == 0) {
        return false;
    }
    first = pool->Base + index * pool->PageSize;
    last = first + (length - 1);
    return ((first ^ last) & ~(paddr_t)(boundary - 1)) != 0;
}

oserr_t
DevicePoolAllocate(
    _In_  struct DevicePool* pool,
    _In_  size_t             length,
    _In_  size_t             alignment,
    _In_  size_t             boundary,
    _Out_ paddr_t*           baseOut)
{
    size_t  pageCount;
    size_t  step;
    size_t  index;
    oserr_t status = OS_EOOM;

    if (pool == NULL || baseOut == NULL || length == 0) {
        return OS_EINVALPARAMS;
    }
    if (!__IsPowerOfTwoOrZero(alignment) || !__IsPowerOfTwoOrZero(boundary)) {
        return OS_EINVALPARAMS;
    }

    // A run longer than its boundary would always cross it, and a length close
    // to the largest size would wrap when rounded up to whole pages.
    if (boundary != 0 && length > boundary) {
        return OS_EINVALPARAMS;
    }
    if (length > SIZE_MAX - pool->PageSize) {
        return OS_EINVALPARAMS;
    }

    // Every page already starts on a page boundary, so smaller alignments need
    // no extra care; larger ones are met by stepping whole alignments.
    if (alignment < pool->PageSize) {
        alignment = pool->PageSize;
    }
    pageCount = DIVUP(length, pool->PageSize);
    step = alignment / pool->PageSize;

    SpinlockAcquireIrq(&pool->Lock);
    for (index = __FirstAlignedIndex(pool, alignment);
         index < __PageCount(pool) && pageCount <= __PageCount(pool) - index;
         index += step) {
        if (__CrossesBoundary(pool, index, length, boundary)) {
            continue;
        }
        if (!bitmap_bits_clear(&pool->Pages, (int)index, (int)pageCount)) {
            continue;
        }

        bitmap_set(&pool->Pages, (int)index, (int)pageCount);
        *baseOut = pool->Base + index * pool->PageSize;
        status = OS_EOK;
        break;
    }
    SpinlockReleaseIrq(&pool->Lock);
    return status;
}

oserr_t
DevicePoolFree(
    _In_ struct DevicePool* pool,
    _In_ paddr_t            base,
    _In_ size_t             length)
{
    size_t  index;
    size_t  pageCount;
    oserr_t status = OS_EOK;

    if (pool == NULL || length == 0 || base < pool->Base) {
        return OS_EINVALPARAMS;
    }
    if ((base - pool->Base) % pool->PageSize != 0 || length > SIZE_MAX - pool->PageSize) {
        return OS_EINVALPARAMS;
    }

    // Use subtraction so a huge range cannot wrap past the end of the pool.
    index = (size_t)((base - pool->Base) / pool->PageSize);
    pageCount = DIVUP(length, pool->PageSize);
    if (index >= __PageCount(pool) || pageCount > __PageCount(pool) - index) {
        return OS_EINVALPARAMS;
    }

    // Freeing pages that are not in use means the caller lost track of its
    // memory. Refuse, so pages someone else now owns are not freed under them.
    SpinlockAcquireIrq(&pool->Lock);
    if (bitmap_bits_set(&pool->Pages, (int)index, (int)pageCount)) {
        bitmap_clear(&pool->Pages, (int)index, (int)pageCount);
    } else {
        status = OS_EINVALPARAMS;
    }
    SpinlockReleaseIrq(&pool->Lock);
    return status;
}

/**
 * @brief Accept a loader-marked range only if it can be used as given.
 *
 * ARM64 maps the pool in whole 2 MiB uncached blocks, so a range off the
 * required alignment would leave part of it with a cached view.
 */
static bool
__LoaderRangeUsable(
    _In_ const struct VBootMemoryEntry* entry,
    _In_ size_t                         alignment)
{
    if (entry->Length == 0) {
        return false;
    }
    return entry->PhysicalBase % alignment == 0 && entry->Length % alignment == 0;
}

/**
 * @brief Find the highest start in one available range for the default pool.
 *
 * Works with the last usable byte rather than an end address, so a range
 * reaching the top of the physical address space cannot wrap.
 */
static bool
__HighestStart(
    _In_  const struct VBootMemoryEntry* entry,
    _In_  size_t                         size,
    _In_  paddr_t                        limit,
    _In_  size_t                         alignment,
    _Out_ paddr_t*                       baseOut)
{
    uint64_t last;
    uint64_t base;

    if (entry->Length == 0 || entry->PhysicalBase > limit) {
        return false;
    }

    last = entry->PhysicalBase + (entry->Length - 1);
    if (last < entry->PhysicalBase || last > limit) {
        last = limit;
    }
    if (last < size - 1) {
        return false;
    }

    base = (last - (size - 1)) & ~(uint64_t)(alignment - 1);
    if (base < entry->PhysicalBase) {
        return false;
    }
    *baseOut = (paddr_t)base;
    return true;
}

oserr_t
DevicePoolChooseRange(
    _In_  const struct VBoot* boot,
    _In_  size_t              size,
    _In_  paddr_t             limit,
    _In_  size_t              alignment,
    _Out_ paddr_t*            baseOut,
    _Out_ size_t*             lengthOut)
{
    const struct VBootMemoryEntry* entries;
    paddr_t                        candidate;
    paddr_t                        best = 0;
    bool                           found = false;

    if (boot == NULL || baseOut == NULL || lengthOut == NULL) {
        return OS_EINVALPARAMS;
    }
    if (size == 0 || alignment == 0 || !__IsPowerOfTwoOrZero(alignment) || size % alignment != 0) {
        return OS_EINVALPARAMS;
    }

    // A range the loader chose, for example from a device tree
    // shared-dma-pool node, takes priority over the default choice.
    entries = (const struct VBootMemoryEntry*)(uintptr_t)boot->Memory.Entries;
    for (unsigned int i = 0; i < boot->Memory.NumberOfEntries; i++) {
        if (entries[i].Type != VBootMemoryType_DevicePool) {
            continue;
        }
        if (__LoaderRangeUsable(&entries[i], alignment)) {
            *baseOut = (paddr_t)entries[i].PhysicalBase;
            *lengthOut = (size_t)entries[i].Length;
            return OS_EOK;
        }
    }

    for (unsigned int i = 0; i < boot->Memory.NumberOfEntries; i++) {
        if (entries[i].Type != VBootMemoryType_Available) {
            continue;
        }
        if (!__HighestStart(&entries[i], size, limit, alignment, &candidate)) {
            continue;
        }
        if (!found || candidate > best) {
            best = candidate;
            found = true;
        }
    }
    if (!found) {
        return OS_ENOENT;
    }

    *baseOut = best;
    *lengthOut = size;
    return OS_EOK;
}

void
DevicePoolSetSystem(
    _In_ struct DevicePool* pool)
{
    g_systemPool = pool;
}

struct DevicePool*
DevicePoolSystem(void)
{
    return g_systemPool;
}
