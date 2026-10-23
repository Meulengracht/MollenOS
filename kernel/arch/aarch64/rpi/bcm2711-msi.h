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

#ifndef __VALI_AARCH64_RPI_BCM2711_MSI_H__
#define __VALI_AARCH64_RPI_BCM2711_MSI_H__

#include <devicetree.h>
#include "msi-firmware.h"

// One pending bit per message; all 32 messages share one GIC parent interrupt.
#define BCM2711_MSI_COUNT           32U
// Minimum host register range covering the receiver and the reset state we inspect.
#define BCM2711_MSI_REGISTER_LENGTH 0x9310U

// Byte offset of the message destination's low address word and enable bit.
#define BCM_MSI_BAR_LOW       0x4044U
// Byte offset of the message destination's high address word.
#define BCM_MSI_BAR_HIGH      0x4048U
// Byte offset of the message data comparison mask and expected value.
#define BCM_MSI_DATA          0x404cU
// Byte offset of pending messages, one bit per message number.
#define BCM_MSI_STATUS        0x4500U
// Byte offset where writing a one clears that message's pending bit.
#define BCM_MSI_CLEAR         0x4508U
// Byte offset of current delivery masks; one means masked.
#define BCM_MSI_MASK_STATUS   0x450cU
// Byte offset where writing a one masks that message without changing other bits.
#define BCM_MSI_MASK_SET      0x4510U
// Byte offset where writing a one unmasks that message without changing other bits.
#define BCM_MSI_MASK_CLEAR    0x4514U
// Compare data bits 15:5 against 0x6540; bits 4:0 select one of the 32 messages.
#define BCM_MSI_DATA_CONFIG   0xffe06540U
// Fixed message data bits supplied to drivers, combined with their message number.
#define BCM_MSI_DATA_BASE     0x6540U
// Preferred aligned destination: the last 32-bit word below 4 GiB.
#define BCM_MSI_ADDRESS_LOW   0xfffffffcULL
// Alternative destination: the last 32-bit word below 64 GiB if RAM covers the first.
#define BCM_MSI_ADDRESS_HIGH  0xffffffffcULL
// Bit zero enables address matching; zero disables the receiver during setup.
#define BCM_MSI_BAR_ENABLE    1U
// Select all messages for mask/clear commands or a complete mask readback.
#define BCM_MSI_ALL_BITS      UINT32_MAX

// Byte offset of inbound window 1's low word; a nonzero size enables the window.
#define BCM_BAR1_LOW          0x402cU
// Byte offset of inbound window 2's low address word and size code.
#define BCM_BAR2_LOW          0x4034U
// Byte offset of inbound window 2's high address word.
#define BCM_BAR2_HIGH         0x4038U
// Byte offset of inbound window 3's low word; reject it if enabled.
#define BCM_BAR3_LOW          0x403cU
// Bits 4:0 hold the inbound window size code, not address bits.
#define BCM_BAR_SIZE_MASK     0x1fU
// This decoder accepts codes 1-21; zero disables the window, higher codes differ.
#define BCM_BAR_SIZE_CODE_MAX 21U
// Accepted size codes describe 2^(code + 15) bytes.
#define BCM_BAR_SIZE_SHIFT    15U
// Split a 64-bit PCI address into the hardware's two 32-bit register words.
#define BCM_PCIE_ADDRESS_BITS 32U

// Byte offset of PCIe host/link status, used to wait for deviced's setup.
#define BCM_PCIE_STATUS       0x4068U
// Bits 7, 5, and 4 require root-complex mode, active data link, and physical link up.
#define BCM_PCIE_READY        0xb0U
// Byte offset of the controller revision register.
#define BCM_PCIE_REVISION     0x406cU
// The low 16 bits hold the revision; all ones is not a valid readable revision.
#define BCM_PCIE_REVISION_MASK 0xffffU
// Revision 3.03 is the oldest layout supported by this receiver implementation.
#define BCM_PCIE_REVISION_MIN  0x303U
// Byte offset of the Pi 4 software reset controls.
#define BCM_PCIE_RESET        0x9210U
// Either the endpoint reset (bit 0) or bridge reset (bit 1) prevents receiver setup.
#define BCM_PCIE_RESET_MASK   3U

struct Arm64Bcm2711MsiRoute {
    const DeviceTreeNode_t* Host;
    // CPU physical address
    uint64_t                RegisterBase;
    // The ARM interrupt controller (GIC) ID shared by all
    // messages from this receiver
    uint32_t                ParentInterrupt;
};

/**
 * @brief Check a BCM2711 host's internal message-signaled interrupt (MSI) receiver.
 *
 * @param tree Parsed firmware tree.
 * @param host Candidate PCI host.
 * @param gic Active ARM interrupt controller node.
 * @param interruptCount Number of usable IDs reported by that controller.
 * @param routeOut Route written only on success.
 * @return OS_EOK, OS_ENOTSUPPORTED for another topology, or malformed data error.
 */
oserr_t
Arm64Bcm2711MsiReadRoute(
    _In_  const DeviceTree_t*          tree,
    _In_  const DeviceTreeNode_t*      host,
    _In_  const DeviceTreeNode_t*      gic,
    _In_  uint32_t                    interruptCount,
    _Out_ struct Arm64Bcm2711MsiRoute* routeOut);

/**
 * @brief Publish the Pi 4 controller and reserve its disabled parent handler.
 * The first allocation initializes the receiver after host startup. Published
 * contexts and mappings live until shutdown. Runtime host reset is unsupported.
 *
 * @param tree Permanent kernel device tree.
 * @param gicId Firmware node handle of the active ARM interrupt controller.
 * @param interruptCount Number of usable GIC IDs.
 * @return OS_EOK or a boot initialization error.
 */
oserr_t
Arm64Bcm2711MsiInitialize(
    _In_ const DeviceTree_t* tree,
    _In_ uint32_t           gicId,
    _In_ uint32_t           interruptCount);

#endif //!__VALI_AARCH64_RPI_BCM2711_MSI_H__
