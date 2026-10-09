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
 * Registers for the BCM2711 and BCM2712 PCI Express (PCIe) controllers.
 * A host controller connects the CPU to PCI devices. Its CPU-side connection
 * is called the root port; a connected device is called an endpoint.
 *
 * Offsets count bytes from the start of the mapped controller registers, except
 * where comments identify separate reset registers or electrical-interface
 * (PHY) register numbers. Masks select bits in their final register positions.
 * Most values are already shifted into position too. SHIFT names a bit count,
 * ORDER is an exponent (a size of 2^ORDER bytes), and BIAS is an adjustment
 * used to turn a size into the code expected by hardware.
 *
 * A window is an address range mapped between the CPU and PCI devices.
 * Outbound means CPU-to-device access; inbound means device-to-system access.
 * The hardware calls inbound windows "BARs" (Base Address Registers). These
 * belong to the host, separately from the BARs describing a connected device's
 * own registers or memory.
 *
 * Layout and setup references:
 * https://github.com/raspberrypi/linux/blob/rpi-6.18.y/drivers/pci/controller/pcie-brcmstb.c
 * https://github.com/raspberrypi/linux/blob/rpi-6.18.y/drivers/reset/reset-brcmstb.c
 */

// Both chips provide this register area. To access a connected device's
// configuration, select it at CONFIG_INDEX, then read or write at CONFIG_DATA.
#define BCM_PCIE_REGISTER_LENGTH                 0x9310
#define BCM_PCIE_CONFIG_INDEX                    0x9000
#define BCM_PCIE_CONFIG_DATA                     0x8000

// A failed register read returns all bits set. Clearing an enable field
// disables its feature; the nonzero values below select specific settings.
#define BCM_PCIE_READ_FAILED                     0xFFFFFFFFU
#define BCM_PCIE_DISABLED                        0U
#define BCM_PCIE_ADDRESS_HIGH_SHIFT              32
#define BCM_PCIE_REGISTER_HIGH_OFFSET            4

// PCI address types read from the firmware device tree. Both memory types
// are supported; PCI I/O ports use a separate address space and need different setup.
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

// Bits 3:2 control byte order for data entering the system through BAR2.
// Zero keeps PCI's little-endian order (least significant byte first), so
// the controller does not swap the data's bytes.
#define BCM_PCIE_VENDOR_CONTROL                  0x0188
#define BCM_PCIE_VENDOR_BAR2_ENDIAN_MASK         0x0000000CU
#define BCM_PCIE_VENDOR_BAR2_LITTLE_ENDIAN       0U

// This controller-specific register sets the device class reported to PCI
// discovery. Class 06:04:00 means a bridge connecting two PCI buses.
#define BCM_PCIE_CLASS_CODE                      0x043C
#define BCM_PCIE_CLASS_CODE_MASK                 0x00FFFFFFU
#define BCM_PCIE_CLASS_CODE_BRIDGE               0x00060400U

// These bits report supported settings, not the connection's current state.
// Bits 3:0 give the maximum speed; bits 8:4 give the number of parallel data
// paths (lanes). Bits 11:10 report support for the L0s/L1 sleep modes.
// Clearing them tells software not to select those modes.
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

// Bits 7:3 control which deeper L1 sleep modes the host reports as supported.
// Set the field to 2 to hide these modes; 2 is a field value, not a mask of bits.
#define BCM2712_ROOT_CAPABILITY                  0x04F8
#define BCM2712_ROOT_CAP_L1SS_MASK               0x000000F8U
#define BCM2712_ROOT_CAP_L1SS_SHIFT              3
#define BCM2712_ROOT_CAP_L1SS_DISABLED           (2U << BCM2712_ROOT_CAP_L1SS_SHIFT)

// Controls device access to system memory, responses to unsupported
// configuration reads, and how data is split across read replies.
// RCB (read completion boundary) sets where one read reply can end and another begin.
#define BCM_PCIE_MISC_CONTROL                    0x4008
// Bit 7 splits replies at 64-byte boundaries; bit 10 also accounts for packet size.
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

// BCM2711's first memory-controller size field must match the range devices
// use to reach RAM. It holds the same size code as BAR2, shifted into bits 31:27.
#define BCM2711_MISC_SCB0_SIZE_MASK              0xF8000000U
#define BCM2711_MISC_SCB0_SIZE_SHIFT             27

// Each inbound mapping uses two 32-bit registers. The first stores the lower
// PCI address bits and a size code in bits 4:0; zero disables the mapping.
// The next register stores the upper address bits, 63:32.
#define BCM_PCIE_INBOUND_BAR1                    0x402C
#define BCM_PCIE_INBOUND_BAR2                    0x4034
#define BCM_PCIE_INBOUND_BAR3                    0x403C
#define BCM_PCIE_INBOUND_SIZE_MASK               0x0000001FU
#define BCM_PCIE_INBOUND_DISABLED                0U
#define BCM_PCIE_INBOUND_MIN_ORDER               12 // 2^12 bytes = 4 KiB, supported by BCM2712.
#define BCM_PCIE_INBOUND_LARGE_MIN_ORDER         16 // 2^16 bytes = 64 KiB, also BCM2711's minimum.
#define BCM_PCIE_INBOUND_MAX_ORDER               36 // 2^36 bytes = 64 GiB.
#define BCM_PCIE_INBOUND_SMALL_SIZE_BASE         0x1CU // Sizes 2^12..2^15 bytes use codes 0x1c..0x1f.
#define BCM_PCIE_INBOUND_LARGE_SIZE_BIAS         15 // Sizes 2^16..2^36 bytes use exponent minus 15.
#define BCM_PCIE_INBOUND_MIN_SIZE                (1ULL << BCM_PCIE_INBOUND_MIN_ORDER)
#define BCM2711_INBOUND_MIN_SIZE                 (1ULL << BCM_PCIE_INBOUND_LARGE_MIN_ORDER)
#define BCM_PCIE_INBOUND_MAX_SIZE                (1ULL << BCM_PCIE_INBOUND_MAX_ORDER)

// The reference driver excludes bases strictly between 2 GiB and 4 GiB when
// checking Pi 4's memory layout. Low PCI addresses must leave room for device
// register windows, and early Pi 4 revisions also have RAM-access restrictions.
// Check that placement separately from whether the base is a multiple of the size.
#define BCM2711_INBOUND_RESTRICTED_BASE_START    0x80000000ULL
#define BCM2711_INBOUND_RESTRICTED_BASE_END      0x100000000ULL

// A device raises a message-signaled interrupt (MSI) by writing to an address.
// This older interrupt destination is separate from the memory mappings; zero
// disables it. BCM2712's MIP interrupt controller instead receives writes through
// an ordinary inbound mapping. Interrupt handlers still need separate setup.
#define BCM_PCIE_MSI_BAR                         0x4044
#define BCM_PCIE_MSI_DISABLED                    0U

// Bit 7 distinguishes a root port (the CPU side) from an endpoint device.
// This bit alone does not show a working connection; the shared code checks that later.
#define BCM_PCIE_LINK_STATUS                     0x4068
#define BCM_PCIE_STATUS_ROOT_PORT                0x00000080U

// Debug and clock bits have the same meanings but different locations on each chip.
// The power-down bit turns off the high-speed electrical interface. CLKREQ lets
// a device request its reference clock; L1SS enables deeper connection sleep modes.
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

// BCM2711 keeps reset controls in the PCI controller itself. Setting PERST to 1
// holds the connected device in reset; setting bridge reset to 1 resets the host.
#define BCM2711_SW_INIT                          0x9210
#define BCM2711_PERST_ASSERT                     0x00000001U
#define BCM2711_BRIDGE_RESET_ASSERT              0x00000002U
#define BCM2711_RESET_ASSERT_BOTH               (BCM2711_PERST_ASSERT | BCM2711_BRIDGE_RESET_ASSERT)

// BCM2712 uses this register for device reset: bit 2 set lets the device run,
// while bit 2 clear holds it in reset. Keep the other bits unchanged.
#define BCM2712_PCIE_CONTROL                     0x4064
#define BCM2712_PERST_RELEASE                    0x00000004U

// These offsets belong to the separate reset controller described by firmware.
// Each group (bank) controls 32 reset signals and occupies 24 bytes. Write a
// single 1 bit to SET to hold that signal in reset, or to CLEAR to release it.
// STATUS reports which reset signals remain active.
#define BCM2712_RESET_LINES_PER_BANK             32
#define BCM2712_RESET_BANK_STRIDE                0x18
#define BCM2712_RESET_SET                        0x00
#define BCM2712_RESET_CLEAR                      0x04
#define BCM2712_RESET_STATUS                     0x08
#define BCM2712_RESET_ASSERTED                   1
#define BCM2712_RESET_RELEASED                   0
#define BCM_PCIE_RESCAL_MIN_LENGTH               12 // Covers START, CONTROL and STATUS.

// BCM2712 has ten device-to-system mappings. Slots 1..3 and 4..10 use separate
// register blocks. The remap registers give each mapping's CPU physical address:
// the first stores address bits 31:12 plus an enable bit at bit 0; the next stores
// bits 63:32. Only physical addresses that fit in 40 bits are supported.
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

// Priority decides which memory request is handled first. Work around hardware
// faults by clearing bit 7, which forwards queued priorities, and setting bits
// 13:11, which fix the priority table and control when its values are updated.
// The reference driver does not explain the circuits changed by these bits.
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

// Bit 5 lets manufacturer-specific device messages change memory request priorities.
// Keep this off while finding devices; that hardware mechanism has known faults.
#define BCM2712_MISC_CONTROL1                    0x40A0
#define BCM2712_MISC_VDM_PRIORITY_ENABLE         0x00000020U

#endif
