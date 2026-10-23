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
#include <memoryspace.h>
#include "device.h"

uintptr_t
Arm64MapDevice(
    _In_ uint64_t physical,
    _In_ uint64_t length)
{
    uintptr_t offset = physical & (ARM64_PAGE_SIZE - 1);
    uintptr_t virtual;
    oserr_t   status;

    status = MemorySpaceMap(
        GetDomainMemorySpace(),
        &(struct MemorySpaceMapOptions) {
            .PhysicalStart = physical - offset,
            .Length = (length + offset + ARM64_PAGE_SIZE - 1) & ~(ARM64_PAGE_SIZE - 1ULL),
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
