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
 * Board-specific Device Tree DMA fallback dispatch
 */

#include <fdt/board.h>
#include "bcm2712.h"

oserr_t
FdtDmaBoardRoute(
    _In_  const FdtDmaBoardPath_t* path,
    _Out_ FdtDmaBusWindows_t*      windows)
{
    if (path == NULL || windows == NULL) {
        return OS_EINVALPARAMS;
    }

    // Try each board-specific rule until one matches. Currently only Pi 5 SDIO1
    // needs a fallback.
    return FdtDmaBcm2712Sdio(path, windows);
}
