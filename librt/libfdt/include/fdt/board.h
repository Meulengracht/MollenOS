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
 * Board-specific Device Tree DMA fallbacks
 * Supplies DMA routes for boards whose firmware leaves out a bus's dma-ranges
 * property even though the hardware can reach RAM directly.
 */

#ifndef __FDT_BOARD_H__
#define __FDT_BOARD_H__

#include <fdt/dma.h>

/**
 * @brief Device and bus details needed to check board DMA fallback rules.
 *
 * Some firmware trees omit dma-ranges on a bus that actually has a direct DMA
 * path. To keep board quirks out of every walker, callers fill this struct
 * from their own tree representation for a three-level path: root -> bus ->
 * device.
 *
 * Compatible strings point directly to the raw "compatible" property bytes
 * (zero-terminated strings packed back-to-back), or NULL when missing.
 * BusAddressCells and BusSizeCells are the counts declared by the bus for its
 * children. RegisterBase and RegisterLength give the device's first MMIO
 * register block as a CPU physical address, which rules use to tell apart
 * controllers that share the same compatible string.
 */
typedef struct FdtDmaBoardPath {
    const void* RootCompatible;
    uint32_t    RootCompatibleLength;
    const void* BusCompatible;
    uint32_t    BusCompatibleLength;
    uint32_t    BusAddressCells;
    uint32_t    BusSizeCells;
    const void* DeviceCompatible;
    uint32_t    DeviceCompatibleLength;
    uint64_t    RegisterBase;
    uint64_t    RegisterLength;
} FdtDmaBoardPath_t;

/**
 * @brief Supply a DMA route when board firmware omits dma-ranges on a bus.
 *
 * Call this only when the device's parent bus has no dma-ranges property at
 * all. An explicit property in the tree (even an empty one) always takes
 * priority. The path must be root, one bus directly under the root, and the
 * device directly under that bus. Before calling, validate the device with
 * FdtDmaDeviceIsDirect and both the bus and root with FdtDmaBusIsDirect.
 *
 * Output stays unchanged if no rule matches. Currently, only the Raspberry Pi 5
 * SDIO1 controller has a fallback rule (direct DMA to memory below 4 GiB).
 *
 * @param path    Root, bus, and device details describing the three-node path.
 * @param windows Receives the fallback bus windows on success.
 * @return OS_EOK when a board rule matched, OS_ENOTSUPPORTED when no rule
 *         applies, or OS_EINVALPARAMS for NULL pointers.
 */
__EXTERN oserr_t
FdtDmaBoardRoute(
    _In_  const FdtDmaBoardPath_t* path,
    _Out_ FdtDmaBusWindows_t*      windows);

#endif //!__FDT_BOARD_H__
