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
 * PCI Device Tree address helpers
 * Decodes the address-space kind and flags from the first 32-bit word of a
 * three-word PCI address in the Device Tree.
 */

#ifndef __FDT_PCI_H__
#define __FDT_PCI_H__

// A Device Tree PCI address is made of three 32-bit numbers: phys.hi, phys.mid,
// and phys.lo. The middle and low words hold the 64-bit numeric address, while
// phys.hi carries flags and the address-space kind:
//   bit 31:     relocatable flag (n)
//   bit 30:     prefetchable memory flag (p)
//   bits 25:24: address-space kind (ss)
// DMA windows only permit these bits and reject bus/device/function or other
// flags that have no meaning for a DMA window.
#define FDT_DMA_PCI_VALID_FLAGS 0xc3000000U

// Address-space kinds stored in bits 25:24 of phys.hi.
typedef enum FdtPciSpace {
    // PCI configuration space (device setup registers).
    FdtPciSpaceConfiguration = 0,
    // PCI I/O port space.
    FdtPciSpaceIo = 1,
    // PCI memory space limited to 32-bit addresses (below 4 GiB).
    FdtPciSpaceMemory32 = 2,
    // PCI memory space supporting full 64-bit addresses.
    FdtPciSpaceMemory64 = 3
} FdtPciSpace_t;

// Extracts the two-bit address-space kind from phys.hi.
#define FDT_PCI_SPACE_FROM_ATTRIBUTES(attributes) \
    ((FdtPciSpace_t)(((attributes) >> 24) & 3))

#endif //!__FDT_PCI_H__
