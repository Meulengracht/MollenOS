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

#ifndef __ARM64_BOOT_MEMORY_H__
#define __ARM64_BOOT_MEMORY_H__

#include <os/osdefs.h>
#include <vboot/vboot.h>

/**
 * @brief Checks whether a boot memory entry may be mapped as Normal RAM.
 *
 * A Reserved entry tells us nothing about what is behind it. It may contain
 * device registers, and those must never be mapped as RAM, as the CPU could
 * then access them speculatively. NO_MAP keeps an entry out of the boot
 * identity map, even when it is known RAM.
 *
 * @param entry The entry to check. Its type must already be validated by
 *              Arm64ValidateBootMemory.
 * @return Nonzero if the entry is RAM that early translation setup may map.
 */
int
Arm64BootMemoryIsRam(
    _In_ const struct VBootMemoryEntry* entry);

/**
 * @brief Validates the memory map from the loader before any page tables are
 * built.
 *
 * This runs with translation disabled, and does not allocate or take any locks.
 * The boot information and the memory map must be readable at their physical
 * addresses. The magic and version are checked first. Then each entry is
 * checked for a known type, a base and length that don't overflow, overlaps
 * with other entries, and conflicts once the ranges are rounded to pages.
 *
 * @param boot The boot information from the loader, at its physical address.
 * @param identityLimit The end (exclusive) of the boot identity map. All RAM
 *                      must be below this address.
 * @return OS_EOK if the map can be used, otherwise OS_ENOTSUPPORTED.
 */
oserr_t
Arm64ValidateBootMemory(
    _In_ const struct VBoot* boot,
    _In_ uint64_t            identityLimit);

#endif //!__ARM64_BOOT_MEMORY_H__
