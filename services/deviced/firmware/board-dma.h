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
 * Board-specific Device Tree DMA fallback wrapper for deviced
 */

#ifndef __FIRMWARE_BOARD_DMA_H__
#define __FIRMWARE_BOARD_DMA_H__

#include <firmware/dma.h>
#include <firmware/tree.h>

/**
 * @brief Look up a board DMA fallback route when a device's parent bus omits dma-ranges.
 *
 * Gathers the root, bus, and device details from deviced's parsed tree and
 * forwards them to FdtDmaBoardRoute. Before calling this, verify that the
 * path has no disabled or malformed nodes, PCI nodes, IOMMU properties,
 * dma-parent, or memory-region bindings. Explicit dma-ranges in the tree
 * always win over board fallbacks.
 *
 * Only three-level paths (root -> bus -> device) are eligible. Callers still
 * need to intersect the returned route with installed RAM before building a
 * device map. Touches no hardware.
 *
 * @param tree   Parsed firmware tree held by the caller.
 * @param node   Node offset of the device whose parent bus lacks dma-ranges.
 * @param ranges Receives the fallback bus windows on success. Unchanged on failure.
 * @return OS_EOK when a board rule matched, or OS_ENOTSUPPORTED otherwise.
 */
__EXTERN oserr_t
FdtBoardDmaRanges(
    _In_  const struct FdtTree* tree,
    _In_  uint32_t              node,
    _Out_ FdtDmaBusWindows_t*   ranges);

#endif //!__FIRMWARE_BOARD_DMA_H__
