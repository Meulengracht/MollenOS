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

#ifndef __UEFI_MEMORY_H__
#define __UEFI_MEMORY_H__

#include <Uefi.h>
#include <vboot/vboot.h>

/**
 * @brief Converts the type of a UEFI memory descriptor from the final memory
 * map into the matching VBoot memory type.
 *
 * Loader allocations are still in use after ExitBootServices. So loader and
 * boot services RAM stays as Firmware until we have a way to release the
 * allocations one by one. Runtime services RAM also stays as Firmware, even if
 * runtime services are never called. Cache and protection attributes are not
 * handled here, they are copied from the descriptor separately.
 *
 * @param type The UEFI memory type. This can also be an unknown vendor or
 *             future value.
 * @return The VBoot memory type. Only conventional memory becomes Available.
 */
static inline enum VBootMemoryType
__ToVBootMemoryType(
    IN UINT32 type)
{
    switch (type) {
        case EfiConventionalMemory:
            return VBootMemoryType_Available;
        case EfiLoaderCode:
        case EfiLoaderData:
        case EfiBootServicesCode:
        case EfiBootServicesData:
        case EfiRuntimeServicesCode:
        case EfiRuntimeServicesData:
            return VBootMemoryType_Firmware;
        case EfiACPIReclaimMemory:
            return VBootMemoryType_ACPI;
        case EfiACPIMemoryNVS:
            return VBootMemoryType_NVS;
        default:
            // MMIO must never be mapped as Normal memory, as the CPU could then
            // access it speculatively. Unknown, unusable and persistent memory
            // need explicit support before the kernel can access them or
            // allocate pages from them.
            return VBootMemoryType_Reserved;
    }
}

#endif //!__UEFI_MEMORY_H__
