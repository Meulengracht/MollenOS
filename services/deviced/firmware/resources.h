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

/** Describes a node's registers, address ranges, clocks, resets, and interrupts.
 * View and the property pointers refer to the original firmware data, which
 * must stay mapped and unchanged while this structure is used.
 * A cell is a 32-bit number in device-tree data. AddressCells and SizeCells
 * count how many cells encode each address and size for this node's children.
 * PhysicalBase and PhysicalLength describe the first register range only when
 * RegisterStatus is OS_EOK. Malformed marks invalid resource properties. */
struct FdtResources {
    struct FdtNode View;
    uint32_t ParentAddressCells;
    uint32_t ParentSizeCells;
    oserr_t RegisterStatus;
    const char* Name;
    uint32_t NodeOffset;
    const uint8_t* Compatible;
    uint32_t CompatibleLength;
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
 * @brief Read each node's resource properties and pass them to a callback.
 *
 * @param blob Firmware device-tree data, kept mapped and unchanged during use.
 * @param length Available size of blob in bytes.
 * @param visitor Callback receiving an array from the root to the current node
 *                and that node's index (depth). Copy records needed after the
 *                callback returns; their property pointers still refer to blob.
 * @param context Caller data passed to visitor.
 * @return OS_EOK if the tree was read, or a tree format error. The callback
 *         must also check each record's Malformed and RegisterStatus fields.
 */
oserr_t
FdtWalkResources(
    _In_ const void* blob,
    _In_ size_t length,
    _In_ FdtResourceFn visitor,
    _InOut_ void* context);
/**
 * @brief Find an enabled node by its firmware ID and read its resources.
 *
 * @param blob Firmware device-tree data, kept mapped and unchanged during use.
 * @param length Available size of blob in bytes.
 * @param phandle Numeric node ID to find; duplicate IDs are rejected.
 * @param provider Receives the node's resources on success. Check RegisterStatus
 *                 before using its physical register address.
 * @return OS_EOK on success, OS_ENOENT if missing or disabled, or an error for
 *         invalid input or malformed resource properties.
 */
oserr_t
FdtFindResources(
    _In_ const void* blob,
    _In_ size_t length,
    _In_ uint32_t phandle,
    _Out_ struct FdtResources* provider);
/**
 * @brief Read one address and size pair from a node's "reg" property.
 *
 * @param node Node whose register ranges should be read.
 * @param index Zero-based index of the range.
 * @param base Receives the starting address as written in "reg". It has not
 *             yet been converted from the parent bus address to a CPU address.
 * @param length Receives the range size in bytes.
 * @return OS_EOK on success, or OS_EINVALPARAMS for a missing entry, unsupported
 *         cell counts, or an incomplete address and size pair. The returned
 *         address and size still need range and overflow checks before use.
 */
oserr_t
FdtRawRegister(
    _In_ const struct FdtResources* node,
    _In_ unsigned int index,
    _Out_ uint64_t* base,
    _Out_ uint64_t* length);
/**
 * @brief Convert a bus address to a CPU physical address through its parent buses.
 *
 * The whole requested range must fit within a mapping at each bus level.
 * An empty mapping leaves the address unchanged. A missing "dma-ranges"
 * property also leaves it unchanged; a missing "ranges" property is an error.
 *
 * @param nodes Array of bus descriptions from the root to the starting bus.
 * @param depth Index of the starting bus in nodes.
 * @param length Size of the requested range in bytes.
 * @param address Starting address, updated as each bus mapping is applied.
 *                On failure it may contain a partly converted address.
 * @param dma Nonzero to use "dma-ranges" for direct memory access by devices;
 *            zero to use "ranges" for CPU access to device resources.
 * @return 1 on success, or 0 if a mapping is missing, invalid, or does not fit.
 */
int
FdtTranslateAddress(
    _In_ const struct FdtResources* nodes,
    _In_ int depth,
    _In_ uint64_t length,
    _In_ uint64_t* address,
    _In_ int dma);
/**
 * @brief Combine device-tree cells into a number in the CPU's byte order.
 *
 * @param value Bytes to read; the caller must ensure cells * 4 bytes are available.
 * @param cells Number of 32-bit cells. Use at most two for a 64-bit result.
 * @return The combined value, or zero if cells is zero.
 */
uint64_t
FdtReadCells(
    _In_ const uint8_t* value,
    _In_ uint32_t cells);
/**
 * @brief Check for an exact string in a list of zero-terminated strings.
 *
 * @param list String list bytes.
 * @param length Available size of list in bytes.
 * @param needle String to find.
 * @return 1 for a match, or 0 if absent or an unterminated string is found first.
 */
int
FdtStringListContains(
    _In_ const uint8_t* list,
    _In_ uint32_t length,
    _In_ const char* needle);
/**
 * @brief Find a name's position after checking the entire string list.
 *
 * @param names List of zero-terminated names.
 * @param length Available size of names in bytes.
 * @param name Name to find; more than one occurrence is an error.
 * @param index Receives its zero-based position, or UINT32_MAX if not found.
 * @param count Receives the total number of names if the list is valid.
 * @return OS_EOK for a match, OS_ENOENT if absent, or OS_EINVALPARAMS for an
 *         invalid list or duplicate matches. Outputs stay unchanged on error
 *         except for OS_ENOENT, which still sets index and count.
 */
oserr_t
FdtNameIndex(
    _In_ const uint8_t* names,
    _In_ uint32_t length,
    _In_ const char* name,
    _Out_ uint32_t* index,
    _Out_ uint32_t* count);
/**
 * @brief Check whether one address range fits entirely inside another.
 *
 * @param base Starting address of the containing range.
 * @param length Size of the containing range in bytes.
 * @param child Starting address of the range to check.
 * @param childLength Size of the range to check in bytes.
 * @return 1 if it fits and childLength is nonzero, otherwise 0.
 */
int
FdtContainsRange(
    _In_ uint64_t base,
    _In_ uint64_t length,
    _In_ uint64_t child,
    _In_ uint64_t childLength);
/**
 * @brief Read the next reference to a resource, such as a clock or interrupt.
 *
 * The referenced node (the provider) declares how many 32-bit argument cells
 * follow its ID. This function checks that those cells fit within the list.
 * Outputs and offset change only on success.
 *
 * @param blob Firmware device-tree data, kept mapped and unchanged during use.
 * @param blobLength Available size of blob in bytes.
 * @param cells Reference list to read.
 * @param length Size of the reference list in bytes.
 * @param cellsName Provider property giving the argument count, e.g. "#clock-cells".
 * @param inheritedProvider Zero if each entry begins with a provider ID (phandle).
 *                          Otherwise, use this ID and read only arguments.
 * @param offset Byte position to read; advances past the entry on success.
 * @param provider Receives the referenced node's resources.
 * @param arguments Receives a pointer to the entry's argument bytes in cells.
 *                  The original list must remain available while these are used.
 * @return OS_EOK on success, or an error for an invalid reference or argument count.
 */
oserr_t
FdtNextReference(
    _In_ const void* blob,
    _In_ size_t blobLength,
    _In_ const uint8_t* cells,
    _In_ uint32_t length,
    _In_ const char* cellsName,
    _In_ uint32_t inheritedProvider,
    _InOut_ uint32_t* offset,
    _Out_ struct FdtResources* provider,
    _Out_ const uint8_t** arguments);

#endif //!__FIRMWARE_RESOURCES_H__
