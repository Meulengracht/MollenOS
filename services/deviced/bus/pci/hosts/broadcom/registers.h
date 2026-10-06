/**
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
#ifndef __DEVICED_BCM_PCI_REGISTERS_H__
#define __DEVICED_BCM_PCI_REGISTERS_H__

/**
 * Register definitions for the BCM2711 and BCM2712 PCIe controller variants.
 *
 * Offsets are bytes from the mapped controller, except the explicitly marked
 * bridge-reset registers and PHY register numbers. Masks identify bits already
 * in their register position. Values are also positioned unless named SHIFT,
 * ORDER, or BIAS; those names describe how to construct an encoded value.
 *
 * In plain terms, outbound windows let the CPU reach a device, and inbound
 * windows let a device reach system memory or other system registers. The
 * hardware calls inbound windows "BARs", but they are separate from the BARs
 * that describe a connected device's own register space.
 *
 * Layout and setup references:
 * https://github.com/raspberrypi/linux/blob/rpi-6.18.y/drivers/pci/controller/pcie-brcmstb.c
 * https://github.com/raspberrypi/linux/blob/rpi-6.18.y/drivers/reset/reset-brcmstb.c
 */

// Both variants expose this controller region. Downstream configuration access
// selects a device at CONFIG_INDEX, then accesses its bytes at CONFIG_DATA.
#define BCM_PCIE_REGISTER_LENGTH                 0x9310
#define BCM_PCIE_CONFIG_INDEX                    0x9000
#define BCM_PCIE_CONFIG_DATA                     0x8000

// A failed register read is conventionally all ones. A cleared enable field
// disables its feature; named nonzero encodings below select specific modes.
#define BCM_PCIE_READ_FAILED                     0xFFFFFFFFU
#define BCM_PCIE_DISABLED                        0U
#define BCM_PCIE_ADDRESS_HIGH_SHIFT              32
#define BCM_PCIE_REGISTER_HIGH_OFFSET            4

// DeviceTree PCI address-space codes, after the parser extracts the field.
// Both memory types are accepted; I/O-port ranges need different programming.
#define BCM_PCIE_DT_MEMORY32                     2U
#define BCM_PCIE_DT_MEMORY64                     3U

// Service sleeps use milliseconds. These conservative waits let reset signals
// and the electrical interface settle before dependent registers are touched.
#define BCM_PCIE_RESET_SETTLE_MS                 1
#define BCM_PCIE_PHY_SETTLE_MS                   1
#define BCM_PCIE_RESET_CONFIG_WAIT_MS            100
#define BCM2712_REGISTER_POLL_MS                 1
#define BCM2712_REGISTER_POLL_ATTEMPTS           10

// The low three bytes describe the bus on this side of the bridge, the bus
// directly behind it, and the highest bus reachable through it. Preserve byte 3.
#define BCM_PCIE_BUS_NUMBERS                     0x0018
#define BCM_PCIE_BUS_NUMBERS_MASK                0x00FFFFFFU
#define BCM_PCIE_SECONDARY_BUS_SHIFT             8
#define BCM_PCIE_SUBORDINATE_BUS_SHIFT           16

// Link control bits 1:0 enable automatic link power saving. Zero keeps the link
// active. Control 2 bits 3:0 select the speed to try when connecting a device.
#define BCM_PCIE_LINK_CONTROL                    0x00BC
#define BCM_PCIE_LINK_CONTROL_ASPM_MASK          0x0003U
#define BCM_PCIE_LINK_CONTROL2                   0x00DC
#define BCM_PCIE_LINK_CONTROL2_SPEED_MASK        0x000FU
#define BCM_PCIE_LINK_SPEED_GEN2                 2U
#define BCM_PCIE_LINK_SPEED_GEN3                 3U

// Bits 3:2 control byte order for inbound BAR2. Zero keeps the little-endian
// byte order used by PCI devices, so the controller does not swap their data.
#define BCM_PCIE_VENDOR_CONTROL                  0x0188
#define BCM_PCIE_VENDOR_BAR2_ENDIAN_MASK         0x0000000CU
#define BCM_PCIE_VENDOR_BAR2_LITTLE_ENDIAN       0U

// This private register supplies the class seen in root-port configuration
// space. 06:04:00 identifies a PCI-to-PCI bridge rather than an endpoint device.
#define BCM_PCIE_CLASS_CODE                      0x043C
#define BCM_PCIE_CLASS_CODE_MASK                 0x00FFFFFFU
#define BCM_PCIE_CLASS_CODE_BRIDGE               0x00060400U

// Capability bits describe what software may request, not the current link.
// Bits 3:0 give maximum speed, bits 8:4 give lane count, and bits 11:10 advertise
// the L0s/L1 power-saving states. Clearing these discourages entering those states.
#define BCM_PCIE_LINK_CAPABILITY                 0x04DC
#define BCM_PCIE_LINK_CAP_SPEED_MASK             0x0000000FU
#define BCM_PCIE_LINK_CAP_WIDTH_MASK             0x000001F0U
#define BCM_PCIE_LINK_CAP_WIDTH_SHIFT            4
#define BCM_PCIE_LINK_CAP_ASPM_L0S                0x00000400U
#define BCM_PCIE_LINK_CAP_ASPM_L1                 0x00000800U
#define BCM_PCIE_LINK_CAP_ASPM_MASK              (BCM_PCIE_LINK_CAP_ASPM_L0S | BCM_PCIE_LINK_CAP_ASPM_L1)
#define BCM_PCIE_LINK_LANES_X1                   1U
#define BCM_PCIE_LINK_LANES_X2                   2U
#define BCM_PCIE_LINK_LANES_X4                   4U

// Bits 7:3 select how the root advertises deeper L1 power-saving modes.
// The hardware encoding 2 hides these modes; it is not an enable-bit mask.
#define BCM2712_ROOT_CAPABILITY                  0x04F8
#define BCM2712_ROOT_CAP_L1SS_MASK               0x000000F8U
#define BCM2712_ROOT_CAP_L1SS_SHIFT              3
#define BCM2712_ROOT_CAP_L1SS_DISABLED           (2U << BCM2712_ROOT_CAP_L1SS_SHIFT)

// Controller memory behavior: allow device access to the system memory bus,
// handle unsupported configuration reads, and choose read-reply boundary modes.
// RCB means read completion boundary: a boundary used when splitting read replies.
#define BCM_PCIE_MISC_CONTROL                    0x4008
// Bit 7 selects 64-byte reply boundaries; bit 10 enables packet-size-aware boundaries.
#define BCM_PCIE_MISC_RCB_64_BYTES               0x00000080U
#define BCM_PCIE_MISC_RCB_PACKET_SIZE            0x00000400U
// Bit 12 permits system-memory access; bit 13 enables unsupported config-read handling.
#define BCM_PCIE_MISC_SYSTEM_ACCESS              0x00001000U
#define BCM_PCIE_MISC_CONFIG_READ_UR             0x00002000U
// Bits 21:20 select the largest transfer on the system-memory side.
#define BCM_PCIE_MISC_BURST_MASK                 0x00300000U
#define BCM2711_MISC_BURST_128_BYTES             0x00000000U
#define BCM2712_MISC_BURST_512_BYTES             0x00200000U
#define BCM_PCIE_MISC_MEMORY_ENABLES             (BCM_PCIE_MISC_RCB_64_BYTES | \
                                                 BCM_PCIE_MISC_RCB_PACKET_SIZE | \
                                                 BCM_PCIE_MISC_SYSTEM_ACCESS | \
                                                 BCM_PCIE_MISC_CONFIG_READ_UR)

// BCM2711's first memory-controller size field must match its inbound RAM
// window. It holds the same size code as BAR2, shifted into bits 31:27.
#define BCM2711_MISC_SCB0_SIZE_MASK              0xF8000000U
#define BCM2711_MISC_SCB0_SIZE_SHIFT             27

// Inbound window low words contain the PCI base and a size code in bits 4:0.
// A zero size code disables the window. The next word holds address bits 63:32.
#define BCM_PCIE_INBOUND_BAR1                    0x402C
#define BCM_PCIE_INBOUND_BAR2                    0x4034
#define BCM_PCIE_INBOUND_BAR3                    0x403C
#define BCM_PCIE_INBOUND_SIZE_MASK               0x0000001FU
#define BCM_PCIE_INBOUND_DISABLED                0U
#define BCM_PCIE_INBOUND_MIN_ORDER               12 // 2^12 bytes = 4 KiB, supported by BCM2712.
#define BCM_PCIE_INBOUND_LARGE_MIN_ORDER         16 // 2^16 bytes = 64 KiB, also BCM2711's minimum.
#define BCM_PCIE_INBOUND_MAX_ORDER               36 // 2^36 bytes = 64 GiB.
#define BCM_PCIE_INBOUND_SMALL_SIZE_BASE         0x1CU // Orders 12..15 encode as 0x1c..0x1f.
#define BCM_PCIE_INBOUND_LARGE_SIZE_BIAS         15 // Orders 16..36 encode as order minus 15.
#define BCM_PCIE_INBOUND_MIN_SIZE                (1ULL << BCM_PCIE_INBOUND_MIN_ORDER)
#define BCM2711_INBOUND_MIN_SIZE                 (1ULL << BCM_PCIE_INBOUND_LARGE_MIN_ORDER)
#define BCM_PCIE_INBOUND_MAX_SIZE                (1ULL << BCM_PCIE_INBOUND_MAX_ORDER)

// The reference driver excludes bases strictly between 2 GiB and 4 GiB when
// checking Pi 4's memory layout. Low PCI addresses must leave room for device
// register windows, and early Pi 4 revisions also have RAM-access restrictions.
// Preserve that placement rule separately from power-of-two size alignment.
#define BCM2711_INBOUND_RESTRICTED_BASE_START    0x80000000ULL
#define BCM2711_INBOUND_RESTRICTED_BASE_END      0x100000000ULL

// The legacy message-interrupt target is separate from the memory windows.
// Writing zero disables it. BCM2712's MIP interrupt block uses an ordinary
// inbound window instead; installing interrupt handlers remains separate work.
#define BCM_PCIE_MSI_BAR                         0x4044
#define BCM_PCIE_MSI_DISABLED                    0U

// Bit 7 distinguishes a root port (the CPU side) from an endpoint device.
// Reading this bit does not prove link-up; the shared code checks that later.
#define BCM_PCIE_LINK_STATUS                     0x4068
#define BCM_PCIE_STATUS_ROOT_PORT                0x00000080U

// Debug/clock controls share bit meanings but move between chip variants.
// IDDQ powers down the high-speed electrical interface. CLKREQ is a device's
// request for its reference clock; L1SS controls deeper link sleep support.
#define BCM2711_HARD_DEBUG                       0x4204
#define BCM2712_HARD_DEBUG                       0x4304
#define BCM_PCIE_DEBUG_CLKREQ_ENABLE             0x00000002U
#define BCM_PCIE_DEBUG_REFCLK_OVERRIDE_ENABLE    0x00010000U
#define BCM_PCIE_DEBUG_REFCLK_OVERRIDE_OUT       0x00100000U
#define BCM_PCIE_DEBUG_L1SS_ENABLE               0x00200000U
#define BCM_PCIE_DEBUG_SERDES_POWERDOWN          0x08000000U
#define BCM2711_DEBUG_CLOCK_POWER_MASK          (BCM_PCIE_DEBUG_CLKREQ_ENABLE | \
                                                 BCM_PCIE_DEBUG_L1SS_ENABLE)
#define BCM2712_DEBUG_CLOCK_MASK                (BCM2711_DEBUG_CLOCK_POWER_MASK | \
                                                 BCM_PCIE_DEBUG_REFCLK_OVERRIDE_ENABLE | \
                                                 BCM_PCIE_DEBUG_REFCLK_OVERRIDE_OUT)
#define BCM2712_DEBUG_CLOCK_ALWAYS_ON           (BCM_PCIE_DEBUG_REFCLK_OVERRIDE_ENABLE | \
                                                 BCM_PCIE_DEBUG_REFCLK_OVERRIDE_OUT)

// BCM2711 has active-high reset bits in the controller itself. PERST holds the
// connected device in reset; bridge reset resets the host-side controller logic.
#define BCM2711_SW_INIT                          0x9210
#define BCM2711_PERST_ASSERT                     0x00000001U
#define BCM2711_BRIDGE_RESET_ASSERT              0x00000002U
#define BCM2711_RESET_ASSERT_BOTH               (BCM2711_PERST_ASSERT | BCM2711_BRIDGE_RESET_ASSERT)

// BCM2712 moves endpoint reset here and reverses its polarity: bit 2 set
// releases reset, bit 2 clear holds the device in reset. Other bits are preserved.
#define BCM2712_PCIE_CONTROL                     0x4064
#define BCM2712_PERST_RELEASE                    0x00000004U

// These offsets belong to the separate firmware-described reset provider.
// Each bank has 32 reset lines and occupies 24 bytes. SET/CLEAR accept a single
// 1 bit to assert/release that line; STATUS reports which lines remain asserted.
#define BCM2712_RESET_LINES_PER_BANK             32
#define BCM2712_RESET_BANK_STRIDE                0x18
#define BCM2712_RESET_SET                        0x00
#define BCM2712_RESET_CLEAR                      0x04
#define BCM2712_RESET_STATUS                     0x08
#define BCM2712_RESET_ASSERTED                   1
#define BCM2712_RESET_RELEASED                   0
#define BCM_PCIE_RESCAL_MIN_LENGTH               12 // Covers START, CONTROL and STATUS.

// BCM2712 has ten inbound windows. Slots 1..3 and 4..10 occupy separate blocks.
// Each remap low word gives physical address bits 31:12 and bit 0 enables access;
// the following word gives bits 63:32. Physical addresses are limited to 40 bits.
#define BCM2712_INBOUND_COUNT                    10
#define BCM2712_INBOUND_FIRST_BLOCK_COUNT        3
#define BCM2712_INBOUND_REGISTER_STRIDE          8
#define BCM2712_INBOUND_BAR4                     0x40D4
#define BCM2712_INBOUND_REMAP1                   0x40AC
#define BCM2712_INBOUND_REMAP4                   0x410C
#define BCM2712_INBOUND_REMAP_ENABLE             0x00000001U
#define BCM2712_INBOUND_PHYSICAL_ALIGN_MASK      0x00000FFFULL
#define BCM2712_SYSTEM_ADDRESS_LIMIT            (1ULL << 40)

// MDIO is the small register interface used to configure the electrical
// interface (PHY). The address word chooses port and register; command zero is
// a write. The data word's bit 31 starts a write and clears when it completes.
#define BCM2712_MDIO_ADDRESS                     0x1100
#define BCM2712_MDIO_WRITE                       0x1104
#define BCM2712_MDIO_PORT0                       0x00000000U
#define BCM2712_MDIO_COMMAND_WRITE               0x00000000U
#define BCM2712_MDIO_BUSY                        0x80000000U

// PHY register numbers, not controller byte offsets. Select block 0x1600, then
// apply the complete reference-driver sequence for a 54 MHz board oscillator.
// The PLL is the circuit that generates the faster PCIe clock from that oscillator.
// The reference publishes these seven values as fixed settings, without the
// individual field meanings. Keep them whole: do not guess divider/filter bits
// or derive replacements from the CPU frequency. Suffixes identify registers.
#define BCM2712_PHY_BLOCK_SELECT                 0x1F
#define BCM2712_PHY_PLL_BLOCK                    0x1600
#define BCM2712_PHY_PLL_REG16                    0x16
#define BCM2712_PHY_PLL_REG17                    0x17
#define BCM2712_PHY_PLL_REG18                    0x18
#define BCM2712_PHY_PLL_REG19                    0x19
#define BCM2712_PHY_PLL_REG1B                    0x1B
#define BCM2712_PHY_PLL_REG1C                    0x1C
#define BCM2712_PHY_PLL_REG1E                    0x1E
#define BCM2712_PHY_PLL_54MHZ_REG16              0x50B9
#define BCM2712_PHY_PLL_54MHZ_REG17              0xBDA1
#define BCM2712_PHY_PLL_54MHZ_REG18              0x0094
#define BCM2712_PHY_PLL_54MHZ_REG19              0x97B4
#define BCM2712_PHY_PLL_54MHZ_REG1B              0x5030
#define BCM2712_PHY_PLL_54MHZ_REG1C              0x5030
#define BCM2712_PHY_PLL_54MHZ_REG1E              0x0007

// The reference setup enables the PHY's P2 power-down control when overriding
// lane count. P2 is an internal electrical-interface power state, not Gen2 speed.
#define BCM2712_PHY_CONTROL                      0x1804
#define BCM2712_PHY_P2_POWERDOWN_ENABLE          0x00000008U

// Bits 7:0 give the power-management clock period in nanoseconds. At 54 MHz
// this is about 18.52 ns; the reference sequence rounds down to 18 (0x12).
#define BCM2712_PHY_TIMERS                       0x184C
#define BCM2712_PHY_PM_CLOCK_PERIOD_MASK         0x000000FFU
#define BCM2712_PHY_PM_CLOCK_54MHZ_NS            0x00000012U

// UBUS and AXI are the controller's connections to the rest of the chip. Avoid
// turning an absent PCI device into a CPU-side bus error: suppress both general
// and address-decode error replies, and supply the configured failed-read value.
#define BCM2712_UBUS_CONTROL                     0x40A4
#define BCM2712_UBUS_REPLY_ERROR_DISABLE         0x00002000U
#define BCM2712_UBUS_DECODE_ERROR_DISABLE        0x00080000U
#define BCM2712_UBUS_ERROR_DISABLE_MASK         (BCM2712_UBUS_REPLY_ERROR_DISABLE | \
                                                 BCM2712_UBUS_DECODE_ERROR_DISABLE)
#define BCM2712_READ_ERROR                       0x4170
#define BCM2712_READ_ERROR_VALUE                 BCM_PCIE_READ_FAILED

// Timeouts count controller clocks at 750 MHz, not CPU clocks. The general
// bus timeout is about 250 ms; configuration retries expire slightly earlier,
// at about 240 ms. Neither kind of wait should stall device discovery forever.
#define BCM2712_UBUS_TIMEOUT                     0x40A8
#define BCM2712_UBUS_TIMEOUT_250MS               0x0B2D0000U
#define BCM2712_RETRY_TIMEOUT                    0x405C
#define BCM2712_RETRY_TIMEOUT_240MS              0x0ABA0000U

// Memory request priority workarounds. Clear forwarding of queued priorities
// (bit 7); set the table, update-timing, and update-gating corrections (13:11).
// The reference does not describe the circuits behind these workaround bits.
#define BCM2712_AXI_CONTROL                      0x416C
#define BCM2712_AXI_PRIORITY_FORWARD_ENABLE     0x00000080U
#define BCM2712_AXI_PRIORITY_GATING_DISABLE     0x00000800U
#define BCM2712_AXI_PRIORITY_TIMING_FIX          0x00001000U
#define BCM2712_AXI_PRIORITY_TABLE_FIX           0x00002000U
#define BCM2712_AXI_PRIORITY_FIXES              (BCM2712_AXI_PRIORITY_GATING_DISABLE | \
                                                 BCM2712_AXI_PRIORITY_TIMING_FIX | \
                                                 BCM2712_AXI_PRIORITY_TABLE_FIX)
#define BCM2712_AXI_PRIORITY_SETUP_MASK         (BCM2712_AXI_PRIORITY_FORWARD_ENABLE | \
                                                 BCM2712_AXI_PRIORITY_FIXES)
#define BCM2712_AXI_OUTSTANDING_MASK             0x0000003FU // Bits 5:0 limit requests awaiting replies.
#define BCM2712_AXI_OUTSTANDING_FALLBACK         15U // Conservative limit when the timing fix is absent.

// Bit 5 lets vendor-defined device messages change memory request priorities.
// Keep this off during enumeration; the priority-message path has known faults.
#define BCM2712_MISC_CONTROL1                    0x40A0
#define BCM2712_MISC_VDM_PRIORITY_ENABLE         0x00000020U

#endif
