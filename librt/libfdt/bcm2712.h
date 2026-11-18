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

#ifndef __FDT_BCM2712_H__
#define __FDT_BCM2712_H__

#include <fdt/board.h>

/**
 * @brief Check whether a path describes the Raspberry Pi 5 SDIO1 controller
 *        and return its 32-bit direct DMA route.
 *
 * Raspberry Pi 5 firmware omits dma-ranges on the bus above SDIO1, even though
 * SDIO1 can access physical addresses below 4 GiB directly. Callers must have
 * already verified the three-level layout (root -> bus -> device) and checked
 * that the bus has no dma-ranges property. Call through FdtDmaBoardRoute.
 * Output is unchanged if the path does not match.
 *
 * @param path    Root, bus, and device details for the candidate path.
 * @param windows Receives a 4 GiB pass-through window on match.
 * @return OS_EOK for Raspberry Pi 5 SDIO1, OS_ENOTSUPPORTED for any other
 *         controller or board, or OS_EINVALPARAMS for NULL pointers.
 */
__EXTERN oserr_t
FdtDmaBcm2712Sdio(
    _In_  const FdtDmaBoardPath_t* path,
    _Out_ FdtDmaBusWindows_t*      windows);

#endif //!__FDT_BCM2712_H__
