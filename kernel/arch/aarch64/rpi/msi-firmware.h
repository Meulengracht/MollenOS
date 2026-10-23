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

#ifndef __VALI_AARCH64_RPI_MSI_FIRMWARE_H__
#define __VALI_AARCH64_RPI_MSI_FIRMWARE_H__

// Device-tree numbers are stored as big-endian 32-bit cells, even on a 64-bit CPU.
#define RPI_MSI_DT_CELL_BYTES          4U
// Each cell contributes this many bits when joining cells into a 64-bit address.
#define RPI_MSI_DT_CELL_BITS           32U
// A parent reference contains only its node handle; these receivers take no arguments.
#define RPI_MSI_DT_PARENT_BYTES        RPI_MSI_DT_CELL_BYTES

// A GIC interrupt description has three cells: kind, number within that kind, flags.
#define RPI_MSI_DT_GIC_CELLS           3U
// Kind zero means a shared device interrupt, rather than a CPU-private interrupt.
#define RPI_MSI_DT_GIC_SHARED          0U
// Rising-edge delivery is required for each BCM2712 MIP output.
#define RPI_MSI_DT_IRQ_EDGE_RISING     1U
// High-level delivery is required for the BCM2711 combined receiver output.
#define RPI_MSI_DT_IRQ_LEVEL_HIGH      4U
// Shared interrupt numbers in the tree start at zero; GIC hardware IDs start at 32.
#define RPI_MSI_DT_GIC_SPI_BASE        32U
// IDs 1020-1023 are special GIC responses, not usable device interrupts.
#define RPI_MSI_DT_GIC_ID_LIMIT        1020U
// Number of shared device interrupts below the first special GIC ID.
#define RPI_MSI_DT_GIC_SPI_COUNT       (RPI_MSI_DT_GIC_ID_LIMIT - RPI_MSI_DT_GIC_SPI_BASE)

// PCI addresses use one space/flags cell followed by two cells of address.
#define RPI_MSI_DT_PCI_ADDRESS_CELLS   3U
// Two cells can hold a full 64-bit bus address, parent address, or window length.
#define RPI_MSI_DT_U64_CELLS           2U
// Bits 25:24 of the PCI flags cell select I/O, 32-bit memory, or 64-bit memory.
#define RPI_MSI_DT_PCI_SPACE_MASK      0x03000000U
// A 32-bit PCI memory window can carry the message write.
#define RPI_MSI_DT_PCI_MEMORY32        0x02000000U
// A 64-bit PCI memory window can also carry the message write.
#define RPI_MSI_DT_PCI_MEMORY64        0x03000000U

// Devices raise an MSI by writing one aligned 32-bit value to the destination.
#define RPI_MSI_MESSAGE_BYTES          4U
// Low address bits that must be zero for that 32-bit write.
#define RPI_MSI_MESSAGE_ALIGNMENT_MASK (RPI_MSI_MESSAGE_BYTES - 1U)

#endif //!__VALI_AARCH64_RPI_MSI_FIRMWARE_H__
