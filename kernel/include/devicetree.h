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

#ifndef __DEVICETREE_H__
#define __DEVICETREE_H__

#include <os/osdefs.h>

/**
 * @brief Flattened Device Tree (FDT) parser.
 * All fields are stored in big-endian format, so before values are decoded
 * we must convert them to the host's endianness before use.
 */

struct FDTHeader {
    // Contains 0xD00DFEED (big-endian)
    uint32_t Magic;
    // This field shall contain the total size in bytes of the devicetree data structure
    uint32_t TotalSize;
    // Offset to the structure block
    uint32_t OffDtStruct;
    // Offset to the strings block
    uint32_t OffDtStrings;
    // Offset to the memory reservation block
    uint32_t OffMemRsvmap;
    // Version of the device tree format, we support version 17
    uint32_t Version;
    // Last compatible version of the device tree format
    uint32_t LastCompVersion;
    // Physical ID of the boot CPU
    uint32_t BootCpuidPhys;
    // Size of the strings block
    uint32_t SizeDtStrings;
    // Size of the structure block
    uint32_t SizeDtStruct;
};

/**
 * @brief The memory reservation block provides the client program with a list of areas in physical memory which are reserved; that
 * is, which shall not be used for general memory allocations. It is used to protect vital data structures from being overwritten
 * by the client program. For example, on some systems with an IOMMU, the TCE (translation control entry) tables initialized
 * by a DTSpec boot program would need to be protected in this manner. Likewise, any boot program code or data used
 * during the client program’s runtime would need to be reserved (e.g., RTAS on Open Firmware platforms). DTSpec does
 * not require the boot program to provide any such runtime components, but it does not prohibit implementations from doing
 * so as an extension.
 * 
 * As with the /reserved-memory node (Section 3.5.4), when booting via [UEFI] entries in the Memory Reservation Block
 * must also be listed in the system memory map obtained via the GetMemoryMap() to protect against allocations by UEFI
 * applications. The memory reservation block entries should be listed with type EfiReservedMemoryType.
 */

struct FDTReserveEntry {
    uint64_t Address;
    uint64_t Size;
};

/**
 * @brief The structure block is composed of a sequence of pieces, each beginning with a token, that is, a big-endian 32-bit integer.
 * Some tokens are followed by extra data, the format of which is determined by the token value. All tokens shall be aligned
 * on a 32-bit boundary, which may require padding bytes (with a value of 0x0) to be inserted after the previous token’s data.
 * 
 * The devicetree structure is represented as a linear tree: the representation of each node begins with an FDT_BEGIN_NODE
 * token and ends with an FDT_END_NODE token. The node’s properties and subnodes (if any) are represented before the
 * FDT_END_NODE, so that the FDT_BEGIN_NODE and FDT_END_NODE tokens for those subnodes are nested within
 * those of the parent.
 * The structure block as a whole consists of the root node’s representation (which contains the representations for all other
 * nodes), followed by an FDT_END token to mark the end of the structure block as a whole.
 */

enum FDTToken {
    FDT_BEGIN_NODE = 0x1,
    FDT_END_NODE = 0x2,
    FDT_PROP = 0x3,
    FDT_NOP = 0x4,
    FDT_END = 0x9,
};

struct FDTProperty {
    uint32_t Length;
    // Relative offset into the string block for the property name
    uint32_t NameOffset;
    // Followed by Length bytes of property value
    uint8_t  Value[];
};

/**
 * @brief Parses the entire device tree and performs necessary initialization.
 * This creates and registers components in the system. The available system componenets
 * we register are the ones under kernel/include/component.
 *  - Cpu's
 *  - Memory controllers
 *  - Memory ranges
 *  - Interrupt controllers
 *  - Other platform-specific components
 * 
 * This relies on memory subsystem being initialized prior to its invocation, as dynamic
 * allocation occurs during this.
 * TODO: Exposing DTB to our system services for device discovery / driver loading.
 * 
 * @param deviceTree The address of the device tree.
 * @param deviceTreeSize The size of the device tree in bytes.
 * @return An error code indicating the success or failure of the operation.
 */
__EXTERN oserr_t
DeviceTreeParseFull(
    _In_  const void* deviceTree,
    _In_  uint32_t    deviceTreeSize);

#endif //!__DEVICETREE_H__
