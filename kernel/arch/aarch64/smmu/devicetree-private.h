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
 * ARM SMMU Device Tree Discovery
 * Shared state passed between devicetree-unit.c and devicetree-route.c while
 * walking a Device Tree.
 */

#ifndef __ARM_SMMU_DEVICETREE_PRIVATE_H__
#define __ARM_SMMU_DEVICETREE_PRIVATE_H__

#include "discovery-private.h"

/**
 * @brief Scratch record for one enabled SMMU node found in the Device Tree.
 *
 * Units that use features our kernel cannot drive stay in this table with a
 * non-OK Status so device routes targeting them fail cleanly as unsupported.
 * Index holds the unit's slot inside ArmSMMUDiscovery.Units, or UINT32_MAX
 * when unsupported. Cells is #iommu-cells (how many 32-bit numbers follow an
 * SMMU phandle reference), and Mask holds the default stream-match-mask for
 * a 1-cell MMU-500 controller.
 */
struct ArmSMMUTreeUnit {
    const DeviceTreeNode_t* Node;
    uint32_t                Index;
    uint32_t                Cells;
    uint32_t                Mask;
    uint64_t                RegisterBase;
    uint64_t                RegisterLength;
    oserr_t                 Status;
};

/**
 * @brief Working state shared between Pass 1 (units) and Pass 2 (routes).
 */
struct ArmSMMUTreeParser {
    const DeviceTree_t*     Tree;                       // Parsed Device Tree being scanned.
    ArmSMMUDiscovery_t*     Discovery;                  // Output discovery table under construction.
    uint32_t                UnitCount;                  // Number of enabled SMMU nodes recorded in Units.
    struct ArmSMMUTreeUnit  Units[ARM_SMMU_MAX_UNITS];  // Scratch list of all SMMU nodes (supported or not).
};

/**
 * @brief Collects every enabled SMMU node into parser->Units (Pass 1).
 *
 * Supported units are copied into parser->Discovery->Units. Units needing
 * unsupported features stay in parser->Units with an error Status so device
 * routes pointing at them fail cleanly. Also validates string properties across
 * every node and caps the node count to bound work on malformed trees.
 * Must run before ArmSMMUDeviceTreeReadRoute so phandle references resolve no
 * matter where SMMU nodes appear in the Device Tree.
 *
 * @param parser Parser with Tree and Discovery set and no units collected yet.
 * @return OS_EOK on success, OS_ENOTSUPPORTED if a table limit is exceeded, or
 *         OS_EINVALPARAMS if a node has malformed properties.
 */
__EXTERN oserr_t
ArmSMMUDeviceTreeCollectUnits(
    _InOut_ struct ArmSMMUTreeParser* parser);

/**
 * @brief Reads the DMA routing and bus limits of one device node (Pass 2).
 *
 * Adds the node's source entry and ID mapping ranges to parser->Discovery if
 * it is enabled and lists iommus or iommu-map. Other nodes are skipped. Requires
 * ArmSMMUDeviceTreeCollectUnits to have run first.
 *
 * @param parser Parser populated by ArmSMMUDeviceTreeCollectUnits.
 * @param node   Device Tree node to inspect.
 * @return OS_EOK on success, or OS_ENOTSUPPORTED / OS_EINVALPARAMS if the
 *         node's DMA properties are malformed or unsupported.
 */
__EXTERN oserr_t
ArmSMMUDeviceTreeReadRoute(
    _InOut_ struct ArmSMMUTreeParser* parser,
    _In_    const DeviceTreeNode_t*   node);

#endif //!__ARM_SMMU_DEVICETREE_PRIVATE_H__
