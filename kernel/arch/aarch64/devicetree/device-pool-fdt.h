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
 * ARM64 Device Tree device pool selection
 * Scans the raw Device Tree blob during early boot to find physical RAM ranges
 * reachable by every active direct-DMA device, then places the device pool there.
 */

#ifndef __ARM64_DT_DEVICE_POOL_H__
#define __ARM64_DT_DEVICE_POOL_H__

#include <arch/mmu.h>

/**
 * @brief Pick a physical device pool reachable by all direct-DMA paths in the DTB.
 *
 * Runs on the boot CPU before the MMU, heap, locks, or logging are up, using
 * static scratch storage instead of dynamic allocation. Requires a boot memory
 * map that already passed Arm64ValidateBootMemory.
 *
 * Walks the Device Tree blob, calculates which CPU RAM ranges each active
 * direct-DMA bus or device can reach, intersects them all, and picks a pool
 * inside one of those shared intervals. Missing dma-ranges properties fail
 * instead of assuming 1:1 access, except where a board rule supplies the route.
 *
 * On failure, @p reasonOut receives a constant string explaining what stopped
 * selection and @p nodeOut receives the offending node's name (pointing into
 * the DTB in firmware RAM, or "" if no node was involved). Both strings stay
 * unchanged on success, and @p poolOut stays unchanged on failure.
 *
 * @param boot      Validated boot information with memory map and DTB bounds.
 * @param size      Fallback pool size in bytes.
 * @param alignment Power-of-two alignment required for both base and length.
 * @param poolOut   Receives the chosen pool base and size on success.
 * @param reasonOut Receives a human-readable failure reason on error.
 * @param nodeOut   Receives the failing node name (or "") on error.
 * @return OS_EOK on success, OS_ENOENT if no RAM fits every route, or an error
 *         if the DTB or any active DMA route is invalid or unsupported.
 */
KERNELAPI oserr_t KERNELABI
Arm64FindDevicePool(
    _In_  const struct VBoot*   boot,
    _In_  size_t                size,
    _In_  size_t                alignment,
    _Out_ PlatformDevicePool_t* poolOut,
    _Out_ const char**          reasonOut,
    _Out_ const char**          nodeOut);

#endif //!__ARM64_DT_DEVICE_POOL_H__
