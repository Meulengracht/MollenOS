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
 */

#include "boot-memory.h"

#define BOOT_PAGE_MASK 0xFFF

int
Arm64BootMemoryIsRam(
    _In_ const struct VBootMemoryEntry* entry)
{
    return entry->Type != VBootMemoryType_Reserved &&
        !(entry->Attributes & VBOOT_MEMORY_NO_MAP);
}

oserr_t
Arm64ValidateBootMemory(
    _In_ const struct VBoot* boot,
    _In_ uint64_t            identityLimit)
{
    const struct VBootMemoryEntry* entries;
    uint64_t                       mapLength;

    // This runs before the shared machine initialization, so the version must
    // be checked here. Otherwise a loader that uses the old meaning of Reserved
    // and Firmware could end up affecting the page tables.
    if (!boot || boot->Magic != VBOOT_MAGIC || boot->Version != VBOOT_VERSION) {
        return OS_ENOTSUPPORTED;
    }

    if (boot->Memory.EntrySize != sizeof(*entries) || !boot->Memory.NumberOfEntries) {
        return OS_ENOTSUPPORTED;
    }

    mapLength = (uint64_t)boot->Memory.NumberOfEntries * sizeof(*entries);
    if (!boot->Memory.Entries || boot->Memory.Entries >= identityLimit ||
        mapLength > identityLimit - boot->Memory.Entries) {
        return OS_ENOTSUPPORTED;
    }

    entries = (const void*)(uintptr_t)boot->Memory.Entries;

    for (uint32_t i = 0; i < boot->Memory.NumberOfEntries; i++) {
        const struct VBootMemoryEntry* entry = &entries[i];
        uint64_t                       base;
        uint64_t                       end;
        int                            ram;
        int                            previousRam;
        
        if ((unsigned int)entry->Type > VBootMemoryType_DevicePool ||
            !entry->Length || entry->Length > UINT64_MAX - entry->PhysicalBase) {
            return OS_ENOTSUPPORTED;
        }
        
        ram = Arm64BootMemoryIsRam(entry);
        base = entry->PhysicalBase;
        end = base + entry->Length;
        if (ram) {
            // Only known RAM must fit below the identity limit and is rounded
            // to pages. An MMIO reservation high up that we never map should
            // not take up identity space or fail this check.
            if (end > identityLimit) {
                return OS_ENOTSUPPORTED;
            }
            base &= ~BOOT_PAGE_MASK;
            end = (end + BOOT_PAGE_MASK) & ~BOOT_PAGE_MASK;
        }
        
        if (entry->Type == VBootMemoryType_Available || entry->Type == VBootMemoryType_DevicePool) {
            // The physical allocator works on whole pages and does not know
            // about NO_MAP. So free RAM must be page aligned and mappable, or
            // we reject the map instead of handing out RAM we can't access.
            if (!ram || ((entry->PhysicalBase | entry->Length) & BOOT_PAGE_MASK)) {
                return OS_ENOTSUPPORTED;
            }
        }

        for (uint32_t j = 0; j < i; j++) {
            const struct VBootMemoryEntry* previous = &entries[j];
            uint64_t                       previousBase = previous->PhysicalBase;
            uint64_t                       previousEnd = previousBase + previous->Length;
            
            if (entry->PhysicalBase < previousEnd && previousBase < entry->PhysicalBase + entry->Length) {
                return OS_ENOTSUPPORTED;
            }
            
            previousRam = Arm64BootMemoryIsRam(previous);
            if (previousRam) {
                previousBase &= ~BOOT_PAGE_MASK;
                previousEnd = (previousEnd + BOOT_PAGE_MASK) & ~BOOT_PAGE_MASK;
            }

            if (base >= previousEnd || previousBase >= end) {
                continue;
            }

            // Two entries can be apart byte-wise but still share a page once
            // they are rounded. RAM can only share a page with RAM of the same
            // kind. We must never round RAM into a Reserved or NO_MAP entry, or
            // put device pool (uncached) RAM and cached RAM in the same page.
            if (ram != previousRam ||
                ((entry->Type == VBootMemoryType_DevicePool) !=
                 (previous->Type == VBootMemoryType_DevicePool))) {
                return OS_ENOTSUPPORTED;
            }
        }
    }
    return OS_EOK;
}
