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

#include <fdt/board.h>
#include <firmware/board-dma.h>
#include <firmware/dma.h>
#include <ddk/utils.h>

oserr_t
FdtBoardDmaRanges(
    _In_  const struct FdtTree* tree,
    _In_  uint32_t              node,
    _Out_ FdtDmaBusWindows_t*   ranges)
{
    const struct FdtResources* device = FdtTreeNode(tree, node);
    const struct FdtResources* bus;
    const struct FdtResources* root;
    FdtDmaBoardPath_t          path = { 0 };
    oserr_t                    status;

    // Board rules match a controller by its translated CPU register range, so
    // the device node must exist and have a valid first register entry.
    if (device == NULL || device->RegisterStatus != OS_EOK) {
        return OS_ENOTSUPPORTED;
    }

    // Require an exact root -> bus -> device layout where the bus has no
    // dma-ranges property of its own.
    bus = FdtTreeParent(tree, node);
    root = bus == NULL ? NULL : FdtTreeParent(tree, bus->NodeOffset);
    if (bus == NULL || root == NULL || bus->DmaRanges != NULL ||
        FdtTreeParent(tree, root->NodeOffset) != NULL) {
        return OS_ENOTSUPPORTED;
    }

    path.RootCompatible = FdtProperty(&root->View, "compatible", &path.RootCompatibleLength);
    path.BusCompatible = FdtProperty(&bus->View, "compatible", &path.BusCompatibleLength);
    path.BusAddressCells = bus->AddressCells;
    path.BusSizeCells = bus->SizeCells;
    path.DeviceCompatible = FdtProperty(&device->View, "compatible", &path.DeviceCompatibleLength);
    path.RegisterBase = device->PhysicalBase;
    path.RegisterLength = device->PhysicalLength;
    
    status = FdtDmaBoardRoute(&path, ranges);
    if (status != OS_EOK) {
        return status;
    }
    TRACE("FdtBoardDmaRanges: node %u uses a board DMA route", node);
    return OS_EOK;
}
