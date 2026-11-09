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

#include <arch/aarch64/arch.h>
#include <machine.h>
#include <arch/mmu.h>
#include <memoryspace.h>
#include "device.h"

uintptr_t
Arm64MapDevice(
    _In_ uint64_t physical,
    _In_ uint64_t length)
{
    uintptr_t    offset = physical & (ARM64_PAGE_SIZE - 1);
    uintptr_t    virtual;
    uint64_t     base = physical - offset;
    uint64_t     mappedLength;
    uint64_t     current;
    unsigned int attributes;
    int          retrieved;
    oserr_t      status;

    // Our page table entries can only hold physical address bits 47:12. So
    // before we round the range to pages, reject a zero length and any range
    // that reaches past bit 47, as it would spill over into the attribute bits.
    if (!length || physical >= (1ULL << 48) || length > (1ULL << 48) - physical) {
        return 0;
    }
    mappedLength = (length + offset + ARM64_PAGE_SIZE - 1) & ~(ARM64_PAGE_SIZE - 1ULL);

    // Firmware tables can be wrong, so we can't be sure the address really
    // holds device registers. If it is RAM that is already in the identity
    // map, a Device mapping of it would conflict with the Normal memory
    // attributes it already has. So every page is checked before we map
    // anything. Pages above the boot window are skipped, as there is no
    // identity mapped RAM up there.
    for (current = base; current < base + mappedLength && current < MEMORY_LOCATION_RING3_CODE; current += ARM64_PAGE_SIZE) {
        status = ArchMmuGetPageAttributes(
            &GetMachine()->SystemSpace, current, 1, &attributes, &retrieved
        );
        if (status != OS_EOK || retrieved != 1 || (attributes & MAPPING_COMMIT)) {
            return 0;
        }
    }

    status = MemorySpaceMap(
        GetDomainMemorySpace(),
        &(struct MemorySpaceMapOptions) {
            .PhysicalStart = base,
            .Length = mappedLength,
            .Flags = MAPPING_COMMIT | MAPPING_NOCACHE | MAPPING_DEVICE | MAPPING_PERSISTENT,
            .PlacementFlags = MAPPING_PHYSICAL_CONTIGUOUS | MAPPING_VIRTUAL_GLOBAL
        },
        &virtual
    );
    if (status != OS_EOK) {
        return 0;
    }

    return virtual + offset;
}
