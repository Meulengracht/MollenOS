/**
 * MollenOS
 *
 * Copyright, Philip Meulengracht
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

#ifndef __VALI_AARCH64_RPI_MIP_H__
#define __VALI_AARCH64_RPI_MIP_H__

#include <devicetree.h>
#include "msi-firmware.h"

// Minimum register range needed to cover the BCM2712 MIP block.
#define ARM64_MIP_REGISTER_LENGTH 0xc0U
// Each MIP accepts 64 message numbers, split into two banks of 32.
#define ARM64_MIP_VECTOR_COUNT    64U

// Byte offset of CPU trigger configuration for messages 0-31; one selects an edge.
#define MIP_CONFIG_LOW       0x20U
// Byte offset of CPU trigger configuration for messages 32-63; one selects an edge.
#define MIP_CONFIG_HIGH      0x30U
// Byte offset of the CPU delivery mask for message numbers 0-31; one blocks delivery.
#define MIP_HOST_MASK_LOW    0x40U
// Byte offset of the CPU delivery mask for message numbers 32-63.
#define MIP_HOST_MASK_HIGH   0x50U
// Byte offset of the video processor delivery mask for message numbers 0-31.
#define MIP_VPU_MASK_LOW     0x60U
// Byte offset of the video processor delivery mask for message numbers 32-63.
#define MIP_VPU_MASK_HIGH    0x70U
// Set every bank bit when masking delivery or selecting edge-triggered inputs.
#define MIP_ALL_BITS         UINT32_MAX
// Clear every bank bit to allow delivery to the CPU.
#define MIP_NO_BITS          0U

// One msi-ranges entry holds a GIC handle, three interrupt cells, and a count.
#define MIP_DT_RANGE_CELLS   (1U + RPI_MSI_DT_GIC_CELLS + 1U)
// The count follows the GIC handle and its interrupt description.
#define MIP_DT_COUNT_OFFSET ((1U + RPI_MSI_DT_GIC_CELLS) * RPI_MSI_DT_CELL_BYTES)

struct Arm64MipRoute {
    const DeviceTreeNode_t* Host;
    const DeviceTreeNode_t* Provider;
    // The CPU physical address used to configure the receiver.
    uint64_t                RegisterBase;
    // The PCI bus address devices write to request an interrupt.
    uint64_t                MessageAddress;
    // The first GIC ID assigned to this host
    uint32_t                FirstInterrupt;
    // The first message number it writes
    uint32_t                MessageOffset;
    // The number of message numbers assigned to this host.
    uint32_t                Count;
};

/**
 * @brief Decode a BCM2712 host's receiver and check where its message writes go.
 *
 * @param tree Retained boot firmware tree.
 * @param host PCI host node.
 * @param gic ARM interrupt controller node used by this kernel.
 * @param interruptCount Number of usable IDs reported by the GIC hardware.
 * @param routeOut Receives the route on success; unchanged on failure.
 * @return OS_EOK, OS_ENOTSUPPORTED for absent/unsupported routing, or a firmware error.
 */
oserr_t
Arm64MipReadRoute(
    _In_  const DeviceTree_t*     tree,
    _In_  const DeviceTreeNode_t* host,
    _In_  const DeviceTreeNode_t* gic,
    _In_  uint32_t                interruptCount,
    _Out_ struct Arm64MipRoute*   routeOut);

/**
 * @brief Initialize MIP blocks and publish kernel-owned controllers for enabled PCI hosts.
 * Call once after GIC initialization, before userspace starts. Published routes
 * and device mappings remain valid until shutdown. Initialization failure is
 * fatal to boot; it does not retract controllers already published.
 *
 * @param tree Boot tree retained until shutdown.
 * @param gicId Firmware node handle of the active ARM interrupt controller.
 * @param interruptCount Number of usable GIC interrupt IDs.
 * @return OS_EOK or an initialization error.
 */
oserr_t
Arm64MipInitialize(
    _In_ const DeviceTree_t* tree,
    _In_ uint32_t            gicId,
    _In_ uint32_t            interruptCount);

#endif //!__VALI_AARCH64_RPI_MIP_H__
