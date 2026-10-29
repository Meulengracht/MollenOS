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
 */

#ifndef __FIRMWARE_PLATFORM_H__
#define __FIRMWARE_PLATFORM_H__

#include <firmware/resources.h>
#include <ddk/devices/platform.h>

/**
 * @brief Describe one enabled native platform node using its cached parent path.
 * Validates the compatible strings, complete register ranges and interrupt
 * references. Register addresses are translated to CPU physical addresses.
 * Supported GIC SPI descriptions include their controller phandle and use GIC
 * INTIDs as source numbers.
 * Simple buses carry identity only, since their ranges describe their children.
 *
 * @param tree Shared tree used to look up interrupt controllers by phandle.
 * @param nodes Cached records from the root through the current node, supplied
 *              by firmware traversal. Must contain at least depth + 1 records.
 * @param depth Index of the node in nodes. Must be greater than zero.
 * @param device Receives a self-contained description on success, unchanged on
 *               failure. Registry identity, name ownership and Pending are left
 *               unset for the caller to supply when publishing the device.
 * @return OS_EOK, OS_ENOENT for a disabled/unidentified node or unavailable
 *         interrupt controller, OS_ENOTSUPPORTED for unsupported bindings or
 *         descriptor capacity, or OS_EINVALPARAMS for malformed firmware.
 */
__EXTERN oserr_t
FdtDescribePlatformDevice(
    _In_  const struct FdtTree*      tree,
    _In_  const struct FdtResources* nodes,
    _In_  int                        depth,
    _Out_ PlatformDevice_t*          device);

#endif
