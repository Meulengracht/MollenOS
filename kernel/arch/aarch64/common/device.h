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

#ifndef __VALI_AARCH64_DEVICE_H__
#define __VALI_AARCH64_DEVICE_H__

#include <arch/aarch64/arch.h>

/**
 * @brief Maps physical device registers for access by the kernel.
 *
 * Creates a persistent, shared kernel mapping with device memory settings,
 * without user access or instruction execution. Covers the pages containing
 * the requested range and preserves the starting offset within the first page.
 *
 * @param physical Physical address of the first device byte to access.
 * @param length Number of bytes in the device range to map.
 * @return Kernel virtual address of the requested first byte, or zero if mapping fails.
 */
__EXTERN uintptr_t
Arm64MapDevice(
    _In_ uint64_t physical,
    _In_ uint64_t length);

#endif
