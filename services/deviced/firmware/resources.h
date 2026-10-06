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

#ifndef DEVICED_FIRMWARE_RESOURCES_H
#define DEVICED_FIRMWARE_RESOURCES_H
#include "reader.h"

/** Standard decoded resource bindings plus the original borrowed node view. */
struct FdtResources {
    struct FdtNode View;
    uint32_t ParentAddressCells;
    uint32_t ParentSizeCells;
    oserr_t RegisterStatus;
    const char* Name;
    uint32_t NodeOffset;
    const uint8_t* Compatible;
    uint32_t CompatibleLength;
    // Cell sizes that apply to this node's children
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

/** @brief Decode common resource properties of completed nodes, including ancestors. */
oserr_t
FdtWalkResources(
    _In_ const void* blob,
    _In_ size_t length,
    _In_ FdtResourceFn visitor,
    _InOut_ void* context);
/** @brief Resolve an enabled unique provider and decode its standard resources. */
oserr_t
FdtFindResources(
    _In_ const void* blob,
    _In_ size_t length,
    _In_ uint32_t phandle,
    _Out_ struct FdtResources* provider);
/** @brief Decode a raw register tuple. Translation is a separate operation. */
oserr_t
FdtRawRegister(
    _In_ const struct FdtResources* node,
    _In_ unsigned int index,
    _Out_ uint64_t* base,
    _Out_ uint64_t* length);
/** @brief Translate an entire extent through ancestor ranges or DMA ranges. */
int
FdtTranslateAddress(
    _In_ const struct FdtResources* nodes,
    _In_ int depth,
    _In_ uint64_t length,
    _In_ uint64_t* address,
    _In_ int dma);
/** @brief Decode a bounded cell value, string list, named index, or range relation. */
uint64_t
FdtReadCells(
    _In_ const uint8_t* value,
    _In_ uint32_t cells);
int
FdtStringListContains(
    _In_ const uint8_t* list,
    _In_ uint32_t length,
    _In_ const char* needle);
oserr_t
FdtNameIndex(
    _In_ const uint8_t* names,
    _In_ uint32_t length,
    _In_ const char* name,
    _Out_ uint32_t* index,
    _Out_ uint32_t* count);
int
FdtContainsRange(
    _In_ uint64_t base,
    _In_ uint64_t length,
    _In_ uint64_t child,
    _In_ uint64_t childLength);
/** @brief Resolve the next provider and its bounded argument span. Zero inherited
 * provider selects an explicit phandle list; otherwise specifiers omit phandles.
 * Output and offset change only on success; the arguments borrow the blob. */
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
#endif
