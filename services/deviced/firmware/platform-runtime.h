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

struct FdtPlatformDependency {
    enum FdtPlatformDependencyKind Kind;
    uint32_t                       Provider;
    uint32_t                       ArgumentCount;
    uint32_t                       Arguments[FDT_PLATFORM_MAX_ARGUMENTS];
    int                            Fixed;
};

struct FdtPlatformDependencies {
    uint32_t                     Count;
    struct FdtPlatformDependency Entries[FDT_PLATFORM_MAX_DEPENDENCIES];
};

/**
 * @brief Decode and validate activation dependencies from the shared inventory.
 *
 * The caller must hold a tree reference during this call. Decoding does not write
 * registers, change clocks, or take references to runtime devices. Assigned-clock
 * programming is unsupported. Missing or disabled dependency nodes cause failure.
 * An empty list means firmware describes no external prerequisites.
 *
 * @param tree Parsed tree with a reference held by the caller.
 * @param node Structure offset of the native device.
 * @param dependencies Receives a copied plan on success. Unchanged on failure.
 * @return OS_EOK, an invalid-reference error, or OS_ENOTSUPPORTED for a binding
 *         or capacity this interface cannot represent.
 */
oserr_t
FdtPlatformDependencies(
    _In_  const struct FdtTree*           tree,
    _In_  uint32_t                        node,
    _Out_ struct FdtPlatformDependencies* dependencies);

/**
 * @brief Compute the RAM ranges reachable by DMA through each parent bus.
 *
 * The caller must hold a tree reference during this call. Translation starts
 * with enabled memory ranges from the device tree. Every non-root bus must have
 * dma-ranges. An empty property maps addresses unchanged. An absent property is
 * unsupported, as are PCI and IOMMU paths. The result describes address ranges
 * only. The caller must separately allocate buffers, enable DMA and arrange any
 * required cache synchronization.
 *
 * @param tree Parsed tree held by the caller.
 * @param node Structure offset of the DMA master.
 * @param map Receives reachable RAM ranges. Unchanged on failure.
 * @return OS_EOK, OS_ENOENT for no reachable RAM, or validation/support error.
 */
oserr_t
FdtPlatformDma(
    _In_  const struct FdtTree* tree,
    _In_  uint32_t              node,
    _Out_ DmDmaMap_t*           map);

#endif
