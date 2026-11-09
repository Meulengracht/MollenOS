/**
 * Copyright 2017, Philip Meulengracht
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

#include <arch/acpi.h>
#include <arch/aarch64/arch.h>
#include <arch/mmu.h>
#include <machine.h>
#include <memoryspace.h>

oserr_t
ArchAcpiGetMappingFlags(
    _In_  uint64_t      physical,
    _In_  size_t        length,
    _Out_ unsigned int* flags)
{
    MemorySpace_t* space = &GetMachine()->SystemSpace;
    uint64_t       current;
    uint64_t       end;
    unsigned int   attributes;
    unsigned int   cacheFlags = 0;
    int            retrieved;
    oserr_t        status;

    // The boot identity window only holds validated Normal RAM, as Reserved
    // and NO_MAP pages were left out before translation was enabled. We look
    // up the physical address as if it was a virtual one, which only works
    // inside that window. Above it, the same address may belong to a process
    // or kernel mapping that has nothing to do with this physical range.
    if (physical >= MEMORY_LOCATION_RING3_CODE || length > MEMORY_LOCATION_RING3_CODE - physical) {
        return OS_ENOTSUPPORTED;
    }

    end = physical + length;
    for (current = physical; current < end; current += ARM64_PAGE_SIZE) {
        status = ArchMmuGetPageAttributes(space, current, 1, &attributes, &retrieved);
        if (status != OS_EOK) {
            return status;
        }

        if (retrieved != 1 || !(attributes & MAPPING_COMMIT) || (attributes & MAPPING_DEVICE)) {
            return OS_ENOTSUPPORTED;
        }

        // The new mapping must use the same cache settings as the identity
        // mapping of the same pages. The DMA pool is uncached Normal RAM,
        // while firmware tables are usually in cached (write-back) Normal RAM.
        // One OSL mapping can't be both, so a range that crosses from one to
        // the other is rejected.
        attributes &= MAPPING_NOCACHE;
        if (current == physical) {
            cacheFlags = attributes;
        } else if (attributes != cacheFlags) {
            return OS_ENOTSUPPORTED;
        }
    }
    
    *flags = cacheFlags;
    return OS_EOK;
}
