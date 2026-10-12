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

typedef struct SystemMemoryDeviceRegion {
    paddr_t           Base;
    size_t            Length;
    MemoryStack_t     Stack;
    Spinlock_t        Lock;
    // Whether the device memory region is cached for CPU access.
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

struct SystemMemoryAllocation {
    SystemMemory_t* Owner;
    paddr_t         PhysicalBase;
    size_t          Length;
    bool            Cached;
};

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
 * @brief Decide where the system's device pool goes, from the loader's map.
 *
 * A range the loader marked as VBootMemoryType_DevicePool is prioritized, when it is
 * aligned to alignment. Otherwise the highest available RAM that fits size
 * bytes, starting on a multiple of alignment and ending at or below limit, is
 * used; the highest is chosen to leave the lowest memory for devices with the
 * tightest address limits. This function does not modify the memory map.
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
SystemMemoryFitDeviceRegion(
    _In_  const struct VBoot* boot,
    _In_  size_t              size,
    _In_  paddr_t             limit,
    _In_  size_t              alignment,
    _Out_ paddr_t*            baseOut,
    _Out_ size_t*             lengthOut);

/**
 * @brief Register the component's reserved device region. This must only be called
 * once during boot.
 * 
 * The memory range must be excluded from ordinary free ranges and consistent
 * CPU mappings must be arranged before registration. Storage and the component live for the
 * lifetime of the system. A failed registration leaves the component unchanged.
 * 
 * @param systemMemory Constructed memory component, with a valid BlockSize.
 * @param base First physical byte of the reserved range.
 * @param length Nonzero length in whole pages.
 * @param cached Whether CPU mappings of the range are cached.
 * @param storage Aligned range metadata, kept alive for the region's lifetime.
 * @param storageSize At least MemoryStackCalculateSize(length / BlockSize) bytes.
 * @return OS_EOK, OS_EBUSY if registered, or the backend validation error.
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
 * @brief Allocate memory from the device region.
 * Stores the allocation details in the provided record.
 */
oserr_t
SystemMemoryAllocateDevice(
    _In_    SystemMemory_t*                systemMemory,
    _In_    size_t                         length,
    _In_    size_t                         alignment,
    _In_    size_t                         boundary,
    _InOut_ struct SystemMemoryAllocation* allocation);

/**
 * @brief Free an allocation and clear the record.
 * @return OS_EOK, or a release error with ownership retained for the caller.
 */
oserr_t
SystemMemoryReleaseAllocation(
    _InOut_ struct SystemMemoryAllocation* allocation);

#endif // !__COMPONENT_MEMORY__
