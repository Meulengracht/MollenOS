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
 * Broadcom BCM2712 board DMA fallback rules
 */

#include "bcm2712.h"
#include <fdt/reader.h>

oserr_t
FdtDmaBcm2712Sdio(
    _In_  const FdtDmaBoardPath_t* path,
    _Out_ FdtDmaBusWindows_t*      windows)
{
    // Several SDHCI controllers on BCM2712 share the "brcm,bcm2712-sdhci"
    // compatible string. Match SDIO1 specifically by its CPU MMIO register
    // range (0x1000fff000, 0x260 bytes). Its DMA engine uses 32-bit addresses
    // directly with no address offset, regardless of where its registers sit.
    const uint64_t sdioBase = 0x1000fff000ULL;
    const uint64_t sdioLength = 0x260ULL;

    if (path == NULL || windows == NULL) {
        return OS_EINVALPARAMS;
    }

    if (path->BusAddressCells != 1 || path->BusSizeCells != 1 ||
        !FdtStringListContains(path->BusCompatible, path->BusCompatibleLength, "simple-bus")) {
        return OS_ENOTSUPPORTED;
    }

    if (!FdtStringListContains(path->RootCompatible, path->RootCompatibleLength, "raspberrypi,5-model-b") ||
        !FdtStringListContains(path->RootCompatible, path->RootCompatibleLength, "brcm,bcm2712") ||
        !FdtStringListContains(path->DeviceCompatible, path->DeviceCompatibleLength, "brcm,bcm2712-sdhci") ||
        path->RegisterBase != sdioBase || path->RegisterLength != sdioLength) {
        return OS_ENOTSUPPORTED;
    }

    // Pass-through window capped at 4 GiB (UINT32_MAX inclusive).
    *windows = (FdtDmaBusWindows_t) {
        .State = FdtDmaPropertyIdentity,
        .ChildLimit = UINT32_MAX
    };
    return OS_EOK;
}
