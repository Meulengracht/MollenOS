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
 * Picks a physical address range from the boot loader's memory map for the
 * system's reserved DMA device memory pool.
 */

#include <component/memory.h>
#include <vboot/vboot.h>

/**
 * @brief Accept a loader-marked range only if it can be used as given.
 *
 * On ARM64, the pool is mapped with uncached 2 MiB blocks. Any range whose start
 * or length is off that alignment would share a block with normal cached RAM,
 * creating mixed cached and uncached mappings for the same memory. The last
 * byte must also stay at or below limit, checked without adding Base + Length
 * so 64-bit address arithmetic cannot wrap around.
 */
static bool
__LoaderRangeUsable(
    _In_ const struct VBootMemoryEntry* entry,
    _In_ paddr_t                        limit,
    _In_ size_t                         alignment)
{
    if (entry->Length == 0 || entry->Length > SIZE_MAX || entry->PhysicalBase > limit) {
        return false;
    }
    if (entry->Length - 1 > limit - entry->PhysicalBase) {
        return false;
    }
    return entry->PhysicalBase % alignment == 0 && entry->Length % alignment == 0;
}

/**
 * @brief Find the highest aligned start for a size-byte pool inside one entry.
 *
 * Tracks the inclusive last usable byte instead of an exclusive end address,
 * so ranges reaching the top of physical memory cannot overflow.
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

    // Clamp the entry's top byte to limit before backing down by size bytes.
    last = entry->PhysicalBase + (entry->Length - 1);
    if (last < entry->PhysicalBase || last > limit) {
        last = limit;
    }
    if (last < size - 1) {
        return false;
    }

    // Round the candidate start downward to a multiple of alignment, then verify
    // it still sits inside this memory map entry.
    base = (last - (size - 1)) & ~(uint64_t)(alignment - 1);
    if (base < entry->PhysicalBase) {
        return false;
    }
    *baseOut = (paddr_t)base;
    return true;
}

oserr_t
SystemMemoryFitDeviceRegion(
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
    if (size == 0 || alignment == 0 || (alignment & (alignment - 1)) != 0 || size % alignment != 0) {
        return OS_EINVALPARAMS;
    }

    // Pass 1: honor a loader-reserved pool (for example, from a device tree
    // shared-dma-pool node) using its own length when it meets our constraints.
    entries = (const struct VBootMemoryEntry*)(uintptr_t)boot->Memory.Entries;
    for (unsigned int i = 0; i < boot->Memory.NumberOfEntries; i++) {
        if (entries[i].Type != VBootMemoryType_DevicePool) {
            continue;
        }
        if (__LoaderRangeUsable(&entries[i], limit, alignment)) {
            *baseOut = (paddr_t)entries[i].PhysicalBase;
            *lengthOut = (size_t)entries[i].Length;
            return OS_EOK;
        }
    }

    // Pass 2: pick the highest aligned fallback block below limit so lower
    // physical addresses remain free for 32-bit or narrower devices.
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
