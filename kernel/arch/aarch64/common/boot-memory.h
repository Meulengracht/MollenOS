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
 * @brief Reports whether a validated boot entry permits a Normal RAM mapping.
 *
 * Reserved says nothing about the underlying storage. In particular, it may
 * contain device registers, which must never receive a speculative RAM view.
 * NO_MAP excludes even known RAM from the boot identity map.
 *
 * @param entry Entry whose type was checked by Arm64ValidateBootMemory.
 * @return Nonzero for RAM that may be mapped by early translation setup.
 */
int
Arm64BootMemoryIsRam(
    _In_ const struct VBootMemoryEntry* entry);

/**
 * @brief Validates the physical memory contract before constructing page tables.
 *
 * Runs with translation disabled and needs no allocation or synchronization.
 * The loader must supply readable boot storage. Validates descriptor arithmetic,
 * types, overlaps and page-rounded mapping boundaries before the first mapping.
 *
 * @param boot Physical boot information supplied by the loader.
 * @param identityLimit Exclusive end of the kernel's boot identity window.
 * @return OS_EOK for a supported map, otherwise OS_ENOTSUPPORTED.
 */
oserr_t
Arm64ValidateBootMemory(
    _In_ const struct VBoot* boot,
    _In_ uint64_t            identityLimit);

#endif //!__ARM64_BOOT_MEMORY_H__
