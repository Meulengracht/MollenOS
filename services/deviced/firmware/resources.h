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

#ifndef __FIRMWARE_RESOURCES_H__
#define __FIRMWARE_RESOURCES_H__

#include <firmware/reader.h>

/**
 * @brief Read hardware resource information from a device-tree description.
 *
 * A device tree is a nested description of hardware. Each node represents a
 * device or a bus and stores named properties as byte sequences. Some
 * properties describe values directly; others refer to a different node by a
 * numeric ID and include extra values for that node.
 *
 * This interface turns the resource-related properties into easier-to-use
 * records. A cell is one 32-bit value in the device tree. For example, an
 * address may use one or two cells, and a size may also use one or two cells.
 * The number of cells used for a device's registers is set by its parent bus.
 * A bus may also describe how addresses used by its children map to addresses
 * used by its own parent; FdtTranslateAddress applies those mappings one level
 * at a time.
 *
 * The View and property pointers in these records point into the original
 * firmware data. Keep that data mapped and unchanged for as long as any such
 * record or pointer is in use. PhysicalBase and PhysicalLength describe only
 * the first register range, and are usable only when RegisterStatus is
 * OS_EOK. Malformed indicates that a resource property could not be trusted.
 */
struct FdtResources {
    struct FdtNode View;
    uint32_t       ParentAddressCells;
    uint32_t       ParentSizeCells;
    oserr_t        RegisterStatus;
    const char*    Name;
    uint32_t       NodeOffset;
    const uint8_t* Compatible;
    uint32_t       CompatibleLength;

    // Number of 32-bit cells per address and size for this node's children
    uint32_t       AddressCells;
    int            HasAddressCells;
    uint32_t       SizeCells;
    uint32_t       Phandle;
    uint32_t       InterruptCells;
    int            IsInterruptController;
    int            Disabled;
    int            AncestorDisabled;
    int            AncestorMalformed;
    const uint8_t* Ranges;
    uint32_t       RangesLength;
    const uint8_t* Reg;
    uint32_t       RegLength;
    const uint8_t* DmaRanges;
    uint32_t       DmaRangesLength;
    const uint8_t* Resets;
    uint32_t       ResetsLength;
    const uint8_t* ResetNames;
    uint32_t       ResetNamesLength;
    const uint8_t* Clocks;
    uint32_t       ClocksLength;
    const uint8_t* ClockNames;
    uint32_t       ClockNamesLength;
    uint32_t       ResetCells;
    uint32_t       ClockCells;
    int            HasResetCells;
    int            HasClockCells;
    uint64_t       PhysicalBase;
    uint64_t       PhysicalLength;
    int            Malformed;
    int            IsMemory;
    int            IsMsiController;
    uint32_t       MsiCells;
    const uint8_t* MsiParent;
    uint32_t       MsiParentLength;
    const uint8_t* MsiRanges;
    uint32_t       MsiRangesLength;
    uint32_t       InterruptParent;
    const uint8_t* Interrupts;
    uint32_t       InterruptsLength;
    const uint8_t* InterruptsExtended;
    uint32_t       InterruptsExtendedLength;
    const uint8_t* InterruptNames;
    uint32_t       InterruptNamesLength;
};

typedef void (*FdtResourceFn)(const struct FdtResources*, int, void*);

/**
 * @brief Visit every device-tree node with its decoded resource properties.
 *
 * For each node, this function builds an array containing that node and its
 * ancestors, starting at the tree root. It reads resource properties such as
 * register ranges, bus address rules, clocks, resets, and interrupts, then
 * passes the array to the visitor. Keeping the ancestors in the array lets
 * callers interpret a node using the cell counts and address rules declared
 * by the buses above it.
 *
 * A node can still be visited when one of its resource properties is invalid.
 * The visitor must check Malformed and RegisterStatus before using the
 * affected data. The callback runs while the tree is being read, so copy any
 * records that must outlive the callback. Their pointers still refer to the
 * original firmware data.
 *
 * @param blob Firmware device-tree data, kept mapped and unchanged during use.
 * @param length Available size of blob in bytes.
 * @param visitor Function called for each node. It receives the ancestor array
 *                and the index of the current node in that array.
 * @param context Caller data passed to visitor.
 * @return OS_EOK if the tree was read, or an error if the tree itself could
 *         not be read. Resource-property problems are reported in each record
 *         and do not necessarily stop the walk.
 */
__EXTERN oserr_t
FdtWalkResources(
    _In_    const void*   blob,
    _In_    size_t        length,
    _In_    FdtResourceFn visitor,
    _InOut_ void*         context);

/**
 * @brief Find a node by its numeric device-tree ID and read its resources.
 *
 * Nodes that provide things such as clocks or interrupts are often named by
 * numeric IDs in other nodes' properties. This function locates the node for
 * one of those IDs and returns its decoded resource information. The ID must
 * identify exactly one node, and the node and all of its ancestors must be
 * enabled and structurally usable. The returned register address still needs
 * to be checked through RegisterStatus before it is used.
 *
 * @param blob Firmware device-tree data, kept mapped and unchanged during use.
 * @param length Available size of blob in bytes.
 * @param phandle Numeric ID of the node to find. Duplicate IDs are rejected.
 * @param provider Receives the node's resources on success. Check RegisterStatus
 *                 before using its physical register address.
 * @return OS_EOK on success, OS_ENOENT if missing or disabled, or an error for
 *         invalid input or malformed resource properties.
 */
__EXTERN oserr_t
FdtFindResources(
    _In_  const void*          blob,
    _In_  size_t               length,
    _In_  uint32_t             phandle,
    _Out_ struct FdtResources* provider);

/**
 * @brief Read one register range as it is written in the device tree.
 *
 * A node's "reg" property is a sequence of address-and-size pairs. The
 * parent bus decides how many 32-bit cells make up each part, so this function
 * uses the cell counts saved on the node to find the requested pair. It does
 * not apply any bus address mappings; use FdtTranslateAddress afterward when
 * a CPU physical address is needed.
 *
 * @param node Node whose register ranges should be read.
 * @param index Zero-based index of the range.
 * @param base Receives the starting address exactly as written in "reg".
 *             It has not yet been translated through the parent buses.
 * @param length Receives the range size in bytes.
 * @return OS_EOK on success, or OS_EINVALPARAMS if the entry is missing, the
 *         parent uses unsupported cell counts, or the property ends partway
 *         through a pair. The caller remains responsible for checking that
 *         the range is valid for its intended use.
 */
__EXTERN oserr_t
FdtRawRegister(
    _In_  const struct FdtResources* node,
    _In_  unsigned int               index,
    _Out_ uint64_t*                  base,
    _Out_ uint64_t*                  length);

/**
 * @brief Translate an address through each parent bus up to the tree root.
 *
 * A device's address is first written in the address space of its parent bus,
 * which may not be the same address space used by the CPU. Each bus can
 * provide a "ranges" property that maps part of its children's address space
 * into the address space of its own parent. This function follows those maps
 * from the starting bus toward the root so the caller gets the final address.
 *
 * The entire requested range must fit inside one map at every level; checking
 * only its first byte could allow a device to access memory beyond the mapped
 * area. A present but empty mapping means addresses already match at that
 * level. For DMA, a missing "dma-ranges" property means no translation is
 * specified and leaves that level unchanged; for normal device addresses, a
 * missing "ranges" property is an error. Failed translation may leave
 * address partly updated, so use a temporary value if the original is needed.
 *
 * @param nodes Bus descriptions from the tree root through the starting bus.
 * @param depth Index of the starting bus in nodes.
 * @param length Number of bytes that must fit in the translated range.
 * @param address Starting address; replaced with the translated address on
 *                success and possibly partly changed on failure.
 * @param dma Nonzero to use "dma-ranges", which describes addresses used by
 *            devices when they directly read or write memory. Zero uses
 *            "ranges", which describes addresses used for device resources.
 * @return 1 on success, or 0 if a mapping is missing, invalid, or does not fit.
 */
__EXTERN int
FdtTranslateAddress(
    _In_ const struct FdtResources* nodes,
    _In_ int                        depth,
    _In_ uint64_t                   length,
    _In_ uint64_t*                  address,
    _In_ int                        dma);

/**
 * @brief Read one or more device-tree cells as a single integer.
 *
 * Device-tree values are stored as 32-bit numbers in big-endian byte order.
 * Addresses and sizes may use multiple cells, with the most significant cell
 * first. This helper joins those cells and converts each one to the CPU's
 * byte order, so callers can do ordinary integer comparisons and arithmetic.
 *
 * @param value Bytes to read. The caller must ensure at least cells * 4 bytes
 *              are available.
 * @param cells Number of 32-bit values to combine. Use no more than two when
 *              the result must fit in 64 bits.
 * @return The combined integer, or zero when cells is zero.
 */
__EXTERN uint64_t
FdtReadCells(
    _In_ const uint8_t* value,
    _In_ uint32_t       cells);

/**
 * @brief Check whether a zero-terminated string list contains a name.
 *
 * Properties such as "compatible" and "clock-names" store several strings
 * back-to-back, each ending with a zero byte. This function walks those
 * strings one at a time and compares complete entries, so a short name does
 * not accidentally match the beginning of a longer one. If an entry has no
 * terminating zero within the supplied length, the list is treated as invalid
 * and no match is reported.
 *
 * @param list Bytes containing the string list.
 * @param length Number of bytes available in list.
 * @param needle Name to find, compared exactly.
 * @return 1 if a complete matching entry is found, or 0 if it is absent or the
 *         list contains an unterminated entry before a match.
 */
__EXTERN int
FdtStringListContains(
    _In_ const uint8_t* list,
    _In_ uint32_t       length,
    _In_ const char*    needle);

/**
 * @brief Find a name's position and validate the complete name list.
 *
 * Some properties pair a list of names with another list of values by using
 * the same position in each list. This function returns that position and
 * counts all names. It reads the whole list, rather than stopping at the
 * requested name, so malformed trailing data and repeated copies of that name
 * are not silently accepted. Empty entries are rejected because they cannot
 * name a resource.
 *
 * @param names Bytes containing the zero-terminated names.
 * @param length Number of bytes available in names.
 * @param name Name whose zero-based position is requested. A repeated match
 *             makes the result ambiguous and is reported as an error.
 * @param index Receives the matching position, or UINT32_MAX if there is no
 *              match. It is written only after the whole list is valid.
 * @param count Receives the total number of names when the list is valid.
 * @return OS_EOK when the name occurs once, OS_ENOENT when it is absent, or
 *         OS_EINVALPARAMS when the list is malformed or the name is repeated.
 *         Outputs are unchanged for OS_EINVALPARAMS; for OS_ENOENT, index and
 *         count are set to describe the valid list.
 */
__EXTERN oserr_t
FdtNameIndex(
    _In_  const uint8_t* names,
    _In_  uint32_t       length,
    _In_  const char*    name,
    _Out_ uint32_t*      index,
    _Out_ uint32_t*      count);

/**
 * @brief Check whether a nonempty address range fits inside another range.
 *
 * Resource properties often give a starting address and a byte length. This
 * helper checks that every byte of one such range belongs to a containing
 * range. It uses subtraction after confirming the starting address is not
 * below the containing range, avoiding an end-address addition that could
 * overflow the integer type.
 *
 * @param base First address in the containing range.
 * @param length Size of the containing range in bytes.
 * @param child First address in the range being checked.
 * @param childLength Size of the range being checked in bytes.
 * @return 1 if the child range is nonempty and fully contained, otherwise 0.
 */
__EXTERN int
FdtContainsRange(
    _In_ uint64_t base,
    _In_ uint64_t length,
    _In_ uint64_t child,
    _In_ uint64_t childLength);

/**
 * @brief Read one reference to another node from a resource property.
 *
 * A property such as "clocks" can contain a sequence of references. Each
 * reference names a node that supplies a resource, such as a clock or reset;
 * that node is called the provider. A reference normally starts with the
 * provider's numeric ID. The provider declares how many 32-bit argument
 * values follow that ID, so this function reads the ID, finds the provider,
 * checks its declared argument count, and returns the argument bytes.
 *
 * Some properties name their provider once for the whole list instead of
 * repeating the ID for every entry. In that case inheritedProvider supplies
 * the ID and each entry contains only arguments. The function advances offset
 * only after the entire reference is valid, allowing a caller to stop safely
 * on an error without losing its place. The returned pointers refer to the
 * original firmware data.
 *
 * @param blob Firmware device-tree data, kept mapped and unchanged during use.
 * @param blobLength Available size of blob in bytes.
 * @param cells Bytes containing the references and their arguments.
 * @param length Number of bytes available in cells.
 * @param cellsName Name of the provider property that states the argument
 *                  count, such as "#clock-cells".
 * @param inheritedProvider Zero when every entry starts with its provider ID.
 *                          Otherwise, this ID is used for each entry and the
 *                          entry starts directly with its arguments.
 * @param offset Byte position of the next entry to read. It advances past the
 *               entry only when that entry is valid.
 * @param provider Receives the resource information for the named provider.
 * @param arguments Receives a pointer to this entry's argument bytes. The
 *                  original firmware data must remain available while the
 *                  pointer is used.
 * @return OS_EOK on success, or an error if the provider cannot be found, its
 *         argument count is invalid, or the entry is incomplete.
 */
__EXTERN oserr_t
FdtNextReference(
    _In_    const void*          blob,
    _In_    size_t               blobLength,
    _In_    const uint8_t*       cells,
    _In_    uint32_t             length,
    _In_    const char*          cellsName,
    _In_    uint32_t             inheritedProvider,
    _InOut_ uint32_t*            offset,
    _Out_   struct FdtResources* provider,
    _Out_   const uint8_t**      arguments);

#endif //!__FIRMWARE_RESOURCES_H__
