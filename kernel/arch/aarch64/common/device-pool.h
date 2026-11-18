/**
 * Copyright 2026, Philip Meulengracht
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
 * ARM64 boot device pool selection
 * Picks the physical DMA device pool before the MMU is enabled so early boot
 * can map it uncached, then hands that exact choice to generic memory setup.
 */

#ifndef __ARM64_DEVICE_POOL_H__
#define __ARM64_DEVICE_POOL_H__

#include <arch/mmu.h>

/**
 * @brief Pick and store the ARM64 device pool before CPU page tables are built.
 *
 * Call once on the boot CPU right after Arm64ValidateBootMemory succeeds.
 * Early boot needs the pool range before turning on the MMU so it never gets
 * mapped with CPU caching. Later, MmuGetDevicePool and Arm64SelectedDevicePool
 * both read this saved result.
 *
 * Logging is not available yet when this runs. If selection fails, this saves
 * the error and failure reason so MmuGetDevicePool can print them once logging
 * is up.
 *
 * @param boot Validated boot information holding the memory map and DTB range.
 * @return OS_EOK on success, or an error status if no usable pool fits.
 */
KERNELAPI oserr_t KERNELABI
Arm64PrepareDevicePool(
    _In_ const struct VBoot* boot);

/**
 * @brief Return the device pool saved by Arm64PrepareDevicePool.
 *
 * Length is zero if selection failed or Arm64PrepareDevicePool has not run yet.
 *
 * @return Saved pool descriptor.
 */
KERNELAPI PlatformDevicePool_t KERNELABI
Arm64SelectedDevicePool(void);

#endif //!__ARM64_DEVICE_POOL_H__
