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

#ifndef __FIRMWARE_PLATFORM_RUNTIME_H__
#define __FIRMWARE_PLATFORM_RUNTIME_H__

#include <firmware/tree.h>
#include <firmware/dma.h>

#define FDT_PLATFORM_MAX_DEPENDENCIES 32
#define FDT_PLATFORM_MAX_ARGUMENTS    4

enum FdtPlatformDependencyKind {
    FdtPlatformPower,
    FdtPlatformSupply,
    FdtPlatformClock,
    FdtPlatformReset,
    FdtPlatformPins,
    FdtPlatformPhy
};

/**
 * @brief Device setup requirement decoded from firmware.
 * NodeOffset selects the controller driver. Arguments contains the decoded
 * binding cells, or the pin-state node for FdtPlatformPins. Fixed is set for
 * validated resources that need no runtime callback.
 */
struct FdtPlatformDependency {
    enum FdtPlatformDependencyKind Kind;
    uint32_t                       NodeOffset;
    uint32_t                       ArgumentCount;
    uint32_t                       Arguments[FDT_PLATFORM_MAX_ARGUMENTS];
    int                            Fixed;
};

/**
 * @brief Ordered device setup requirements, copied without changing hardware.
 * Process Entries from first to last during setup. Release completed requests
 * in reverse order so parent resources stay available until children are done.
 */
struct FdtPlatformDependencies {
    uint32_t                     Count;
    struct FdtPlatformDependency Entries[FDT_PLATFORM_MAX_DEPENDENCIES];
};

/**
 * @brief Decode and validate device setup dependencies from the shared inventory.
 *
 * The caller must hold a tree reference during this call. Decoding leaves
 * registers and clocks unchanged and takes no references to runtime devices.
 * Assigned-clock programming is unsupported. Every dependency node must exist
 * and be enabled. An empty list means firmware specifies no external dependencies.
 *
 * @param tree Parsed tree with a reference held by the caller.
 * @param node Structure offset of the native device.
 * @param dependencies Receives a copy of the decoded requirements. Unchanged on failure.
 * @return OS_EOK, an invalid-reference error, or OS_ENOTSUPPORTED for a binding
 *         or capacity this interface cannot represent.
 */
oserr_t
FdtPlatformDependencies(
    _In_  const struct FdtTree*           tree,
    _In_  uint32_t                        node,
    _Out_ struct FdtPlatformDependencies* dependencies);

/**
 * @brief Decode only the device's dependencies when its bus is already prepared.
 *
 * @param tree Shared tree. The caller must hold a reference during this call.
 * @param node Child of a bus with separate runtime setup, such as RP1.
 * @param dependencies Receives a copy of the decoded requirements. Unchanged on error.
 * @return OS_EOK or a firmware validation or support error.
 */
oserr_t
FdtPlatformLocalDependencies(
    _In_  const struct FdtTree*           tree,
    _In_  uint32_t                        node,
    _Out_ struct FdtPlatformDependencies* dependencies);

/**
 * @brief Find all RAM ranges a platform device can reach over direct DMA.
 *
 * The caller must hold a tree reference during this call. Starts from enabled
 * physical RAM nodes in the Device Tree, then applies every parent bus's
 * dma-ranges from the root down toward the device. Every bus below the root
 * needs a dma-ranges property (an empty property passes addresses through
 * unchanged). One board exception exists: on Raspberry Pi 5, the SDIO1 bus
 * omits dma-ranges and gets a 4 GiB pass-through fallback. Other missing
 * routes, PCI buses, and IOMMU-translated devices are unsupported.
 *
 * Returns address intervals only. Buffer allocation, enabling device DMA,
 * and CPU cache flushes are handled separately by the caller.
 *
 * @param tree Parsed firmware tree with a reference held by the caller.
 * @param node Structure offset of the DMA-capable device.
 * @param map  Receives reachable RAM ranges on success. Unchanged on failure.
 * @return OS_EOK on success, OS_ENOENT if disabled or no RAM is reachable,
 *         or an error if the path is malformed or unsupported.
 */
oserr_t
FdtPlatformDma(
    _In_  const struct FdtTree* tree,
    _In_  uint32_t              node,
    _Out_ FdtDmaMap_t*          map);

#endif //!__FIRMWARE_PLATFORM_RUNTIME_H__
