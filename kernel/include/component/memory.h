/**
 * Copyright 2018, Philip Meulengracht
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
 * MollenOS System Component Infrastructure 
 * - The Memory component. This component has the task of managing
 *   different memory regions that map to physical components
 */

#ifndef __COMPONENT_MEMORY__
#define __COMPONENT_MEMORY__

#include <os/osdefs.h>
#include <spinlock.h>
#include <utils/memory_stack.h>

// Forward declarations
struct VBoot;

#define MEMORY_MASK_COUNT 5

enum SystemMemoryAttributes {
    SystemMemoryAttributes_REMOVABLE,
    SystemMemoryAttributes_NONVOLATILE
};

typedef struct SystemMemoryRange {
    uintptr_t Start;
    size_t    Length;
} SystemMemoryRange_t;

typedef struct SystemMemoryMap {
    SystemMemoryRange_t Shared;
    SystemMemoryRange_t UserCode;
    SystemMemoryRange_t UserHeap;
    SystemMemoryRange_t ThreadLocal;
} SystemMemoryMap_t;

typedef struct SystemMemoryAllocatorRegion {
    MemoryStack_t Stack;
    Spinlock_t    Lock;
} SystemMemoryAllocatorRegion_t;

typedef struct SystemMemoryAllocator {
    int                           MaskCount;
    size_t                        Masks[MEMORY_MASK_COUNT];
    SystemMemoryAllocatorRegion_t Region[MEMORY_MASK_COUNT];
} SystemMemoryAllocator_t;

/**
 * @brief Physical memory pool reserved at boot for DMA devices.
 *
 * Devices with limited address ranges or uncached buffer needs allocate from
 * this range through a spinlock-guarded stack instead of general RAM.
 */
typedef struct SystemMemoryDeviceRegion {
    paddr_t           Base;
    size_t            Length;
    MemoryStack_t     Stack;
    Spinlock_t        Lock;
    // True when CPU mappings of this region use normal write-back caching.
    bool              Cached;
} SystemMemoryDeviceRegion_t;

typedef struct SystemMemory {
    uintptr_t                  PhysicalBase;
    size_t                     Size;
    size_t                     BlockSize;
    unsigned int               Attributes; // enum SystemMemoryAttributes
    SystemMemoryAllocator_t    Allocator;
    SystemMemoryDeviceRegion_t DeviceMemory;
} SystemMemory_t;

/**
 * @brief Tracks one contiguous allocation taken from the device memory pool.
 *
 * Zero-initialize this record before calling SystemMemoryAllocateDevice, and
 * keep it until SystemMemoryReleaseAllocation frees the pages back to Owner.
 */
struct SystemMemoryAllocation {
    SystemMemory_t* Owner;
    paddr_t         PhysicalBase;
    size_t          Length;
    bool            Cached;
};

/**
 * @brief One inclusive CPU physical address interval (Base through Limit).
 *
 * Used to pass DMA-reachable RAM windows into the device pool allocator. Each
 * pool must fit inside a single entry. Even if two entries touch in physical
 * memory, keep them separate when a bus shifts DMA addresses differently on
 * either side of the boundary.
 */
typedef struct SystemMemoryPhysicalRange {
    uint64_t Base;
    uint64_t Limit;
} SystemMemoryPhysicalRange_t;

/**
 * @brief Initializes the system memory component.
 */
KERNELAPI void KERNELABI
SystemMemoryConstruct(
    _In_ SystemMemory_t* systemMemory,
    _In_ uintptr_t       physicalBase,
    _In_ size_t          size,
    _In_ size_t          blockSize,
    _In_ unsigned int    attributes);

/**
 * @brief Allocate the requested number of physical pages from the default
 * allocators. Higher memory will be preferred.
 */
KERNELAPI oserr_t KERNELABI
SystemMemoryAllocate(
    _In_ SystemMemory_t* systemMemory,
    _In_ size_t          memoryMask,
    _In_ int             pageCount,
    _In_ uintptr_t*      pages);

/**
 * @brief Free the provided physical pages.
 *
 * @param systemMemory Constructed memory component.
 * @param pageCount Number of pages in the list.
 * @param pages List of physical addresses to free.
 * @return OS_EOK if all pages were successfully freed, OS_EINVALPARAMS if any page is invalid, or an appropriate error code otherwise.
 */
KERNELAPI oserr_t KERNELABI
SystemMemoryFree(
    _In_ SystemMemory_t*  systemMemory,
    _In_ int              pageCount,
    _In_ const uintptr_t* pages);

/**
 * @brief Check if the given address is contained within the system memory component.
 *
 * @param systemMemory Constructed memory component.
 * @param address Physical address to check.
 * @return OS_EOK if the address is within the system memory component, OS_EINVALPARAMS otherwise.
 */
KERNELAPI oserr_t KERNELABI
SystemMemoryContainsAddress(
    _In_ SystemMemory_t* systemMemory,
    _In_ uintptr_t       address);

/**
 * @brief Choose where the boot-time device memory pool sits in physical RAM.
 *
 * Devices often need bounce buffers or uncached shared pages in a fixed pool.
 * If the boot loader marked a VBootMemoryType_DevicePool range whose start and
 * length are multiples of alignment and whose last byte is at or below limit,
 * this picks that range with its own length. Otherwise it picks a size-byte
 * aligned block as high as possible at or below limit. Putting the pool high
 * leaves lower physical addresses free for devices that can only reach low RAM.
 * The boot memory map itself is left unchanged.
 *
 * @param boot      Boot loader state carrying the physical memory map.
 * @param size      Fallback pool size in bytes when no loader pool qualifies.
 *                  Must be nonzero and a multiple of alignment.
 * @param limit     Highest physical byte address the pool may reach, inclusive.
 * @param alignment Power-of-two alignment required for both start and length.
 * @param baseOut   Receives the chosen first physical address on success.
 * @param lengthOut Receives the chosen size in bytes on success.
 * @return OS_EOK on success, OS_EINVALPARAMS for NULL pointers or invalid size
 *         or alignment, or OS_ENOENT if no range fits below limit.
 */
oserr_t
SystemMemoryFitDeviceRegion(
    _In_  const struct VBoot* boot,
    _In_  size_t              size,
    _In_  paddr_t             limit,
    _In_  size_t              alignment,
    _Out_ paddr_t*            baseOut,
    _Out_ size_t*             lengthOut);

/**
 * @brief Pick a device pool inside one of the supplied CPU physical intervals.
 *
 * Checks loader-reserved pools (such as Device Tree shared-dma-pool regions)
 * first if an entire entry fits inside one of @p ranges. Otherwise picks the
 * highest aligned @p size-byte block from available RAM inside @p ranges.
 * Picking high addresses keeps low RAM free for devices with narrow address
 * limits. An empty range list (@p rangeCount == 0) means no memory is reachable
 * and fails with OS_ENOENT instead of falling back to all RAM.
 *
 * Allocates no memory, logs nothing, and leaves outputs and the boot map
 * unchanged on failure.
 *
 * @param boot       Loader boot state holding the physical memory map.
 * @param size       Fallback pool size in bytes when no loader pool fits.
 * @param alignment  Power-of-two alignment required for both start and size.
 * @param ranges     Permitted physical intervals the pool must stay inside.
 * @param rangeCount Number of intervals in @p ranges.
 * @param baseOut    Receives the chosen physical start address on success.
 * @param lengthOut  Receives the chosen pool length in bytes on success.
 * @return OS_EOK on success, OS_ENOENT if nothing fits, or OS_EINVALPARAMS for
 *         invalid inputs.
 */
KERNELAPI oserr_t KERNELABI
SystemMemoryFitDeviceRegionInRanges(
    _In_  const struct VBoot*                boot,
    _In_  size_t                             size,
    _In_  size_t                             alignment,
    _In_  const SystemMemoryPhysicalRange_t* ranges,
    _In_  uint32_t                           rangeCount,
    _Out_ paddr_t*                           baseOut,
    _Out_ size_t*                            lengthOut);

/**
 * @brief Register the reserved device memory pool on this component.
 *
 * Call this once during boot after excluding the range from normal page
 * allocators. When cached is false, all CPU page tables covering this range
 * must already map it uncached so CPU accesses match the pool policy. Both
 * systemMemory and storage must stay alive for the lifetime of the system. If
 * registration fails, systemMemory stays unchanged.
 *
 * @param systemMemory Constructed memory component with a power-of-two BlockSize.
 * @param base         First physical byte of the reserved pool, page-aligned.
 * @param length       Nonzero pool size in bytes, a whole number of pages.
 * @param cached       True if CPU mappings of this pool use caching.
 * @param storage      Backing metadata buffer for the page stack, kept alive forever.
 * @param storageSize  Size of storage in bytes. Must be at least
 *                     MemoryStackCalculateSize(length / BlockSize).
 * @return OS_EOK on success, OS_EBUSY if already registered, OS_ENOTSUPPORTED if
 *         the pool is too large, or OS_EINVALPARAMS for invalid geometry or storage.
 */
oserr_t
SystemMemoryInitializeDeviceRegion(
    _In_ SystemMemory_t* systemMemory,
    _In_ paddr_t         base,
    _In_ size_t          length,
    _In_ bool            cached,
    _In_ void*           storage,
    _In_ size_t          storageSize);

/**
 * @brief Allocate contiguous pages from the reserved device memory pool.
 *
 * Rounds length up to whole BlockSize pages and fills allocation on success.
 *
 * @param systemMemory Memory component with an initialized device pool.
 * @param length       Requested byte count.
 * @param alignment    Required start alignment in bytes, or 0 for page alignment.
 * @param boundary     Power-of-two address boundary the block must not cross, or 0 for none.
 * @param allocation   Caller record with Owner set to NULL. Filled on success.
 * @return OS_EOK on success, OS_EBUSY if allocation is already in use,
 *         OS_ENOTSUPPORTED if no device pool is registered, or an allocator error.
 */
oserr_t
SystemMemoryAllocateDevice(
    _In_    SystemMemory_t*                systemMemory,
    _In_    size_t                         length,
    _In_    size_t                         alignment,
    _In_    size_t                         boundary,
    _InOut_ struct SystemMemoryAllocation* allocation);

/**
 * @brief Return pages allocated by SystemMemoryAllocateDevice to the pool.
 *
 * A zeroed record (Owner == NULL) succeeds right away. If freeing fails,
 * allocation stays unchanged so the caller can retry later.
 *
 * @param allocation Allocation record filled by SystemMemoryAllocateDevice.
 *                   Cleared on success.
 * @return OS_EOK on success, OS_EINVALPARAMS for NULL, or a stack push error.
 */
oserr_t
SystemMemoryReleaseAllocation(
    _InOut_ struct SystemMemoryAllocation* allocation);

#endif // !__COMPONENT_MEMORY__
