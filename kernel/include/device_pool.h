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

#ifndef __DEVICE_POOL_H__
#define __DEVICE_POOL_H__

#include <os/osdefs.h>
#include <ds/bitmap.h>
#include <spinlock.h>
#include <vboot/vboot.h>

/**
 * One block of physical memory set aside at boot for devices. It never enters
 * the general page allocator, so it can hand out neighbouring, aligned pages
 * on request, which the general allocator cannot promise. A set bit in Pages
 * means that page is in use. Cached tells how every view of the memory must
 * be mapped: uncached where devices do not see the CPU cache.
 */
struct DevicePool {
    paddr_t    Base;
    size_t     PageSize;
    bool       Cached;
    bitmap_t   Pages;
    Spinlock_t Lock;
};

// Bytes of bitmap storage DevicePoolConstruct needs for this many pages.
#define DEVICE_POOL_BITMAP_BYTES(pageCount) (BITMAP_SIZE(pageCount) * sizeof(uint32_t))

/**
 * @brief Start tracking a block of reserved physical memory, all of it free.
 *
 * The pool only keeps track of which pages are used. It never reads or writes
 * the memory itself, so it needs no mapping of it and does not zero it.
 *
 * @param pool Pool to set up.
 * @param base First physical byte; must be a multiple of pageSize.
 * @param length Nonzero size in bytes; must be a multiple of pageSize.
 * @param pageSize Power of two; the unit everything is handed out in.
 * @param cached Whether views of the memory may be cached.
 * @param bitmapStorage At least DEVICE_POOL_BITMAP_BYTES(length / pageSize)
 *                      bytes, kept alive as long as the pool.
 * @return OS_EOK; OS_EINVALPARAMS for NULL arguments, a bad page size, a range
 *         not on page boundaries, or a range running past the largest
 *         physical address; OS_ENOTSUPPORTED for more pages than the bitmap
 *         can count.
 */
oserr_t
DevicePoolConstruct(
    _In_ struct DevicePool* pool,
    _In_ paddr_t            base,
    _In_ size_t             length,
    _In_ size_t             pageSize,
    _In_ bool               cached,
    _In_ void*              bitmapStorage);

/**
 * @brief Take a run of neighbouring pages out of the pool.
 *
 * The run starts on a multiple of alignment and its first length bytes do not
 * cross a multiple of boundary, both measured in physical addresses. The first
 * fitting run is used. When the pool cannot satisfy the request this fails
 * instead of falling back to other memory.
 *
 * @param pool Pool to allocate from.
 * @param length Nonzero number of bytes; rounded up to whole pages.
 * @param alignment Zero or a power of two. Anything up to a page is met by
 *                  every page already.
 * @param boundary Zero for none, or a power of two at least as large as length.
 * @param baseOut Receives the physical address of the first page on success.
 * @return OS_EOK; OS_EINVALPARAMS for NULL arguments, a zero or oversized
 *         length, or an alignment or boundary that is not a power of two or
 *         that length could never fit; OS_EOOM if no free run fits.
 */
oserr_t
DevicePoolAllocate(
    _In_  struct DevicePool* pool,
    _In_  size_t             length,
    _In_  size_t             alignment,
    _In_  size_t             boundary,
    _Out_ paddr_t*           baseOut);

/**
 * @brief Return pages taken by DevicePoolAllocate.
 *
 * @param pool Pool the pages came from.
 * @param base Address DevicePoolAllocate returned.
 * @param length The length passed to DevicePoolAllocate.
 * @return OS_EOK, or OS_EINVALPARAMS if the range is not inside the pool, not
 *         on a page boundary, or contains pages that are not in use. Nothing
 *         is freed then, so a double free cannot release someone else's pages.
 */
oserr_t
DevicePoolFree(
    _In_ struct DevicePool* pool,
    _In_ paddr_t            base,
    _In_ size_t             length);

/**
 * @brief Decide where the system's device pool goes, from the loader's map.
 *
 * A range the loader marked as VBootMemoryType_DevicePool wins, when it is
 * aligned to alignment. Otherwise the highest available RAM that fits size
 * bytes, starting on a multiple of alignment and ending at or below limit, is
 * used; the highest is chosen to leave the lowest memory for devices with the
 * tightest address limits. This reads only the map and changes nothing, so
 * early boot code and generic memory setup can both call it and always agree.
 *
 * @param boot Loader information with the physical memory map.
 * @param size Default pool size; a multiple of alignment.
 * @param limit Last physical address the default pool may use, inclusive.
 * @param alignment Power of two the pool's start and size must be multiples of.
 * @param baseOut Receives the pool's first physical address.
 * @param lengthOut Receives the pool's size in bytes.
 * @return OS_EOK; OS_EINVALPARAMS for NULL arguments or a bad size or
 *         alignment; OS_ENOENT if no range fits.
 */
oserr_t
DevicePoolChooseRange(
    _In_  const struct VBoot* boot,
    _In_  size_t              size,
    _In_  paddr_t             limit,
    _In_  size_t              alignment,
    _Out_ paddr_t*            baseOut,
    _Out_ size_t*             lengthOut);

/**
 * @brief Make a constructed pool the one SHM uses for device memory.
 *
 * Called once by memory setup. Until then, and when no pool could be set up,
 * DevicePoolSystem returns NULL and device allocations fail.
 */
void
DevicePoolSetSystem(
    _In_ struct DevicePool* pool);

/**
 * @brief Return the system's device pool, or NULL if there is none.
 */
struct DevicePool*
DevicePoolSystem(void);

#endif //!__DEVICE_POOL_H__
