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

//#define __TRACE
#include <ddk/utils.h>
#include <stdlib.h>
#include <string.h>

#include "tree-private.h"

uint64_t
FdtReadCells(
    _In_ const uint8_t* p,
    _In_ uint32_t       cells)
{
    uint64_t value = 0;
    for (uint32_t i = 0; i < cells; i++) {
        value = (value << 32) | FdtReadBe32(p + (i * 4));
    }
    return value;
}

int
FdtStringListContains(
    _In_ const uint8_t* list,
    _In_ uint32_t       length,
    _In_ const char*    needle)
{
    size_t   needleLength = strlen(needle);
    uint32_t offset = 0;

    while (offset < length) {
        size_t         entryLength;
        const uint8_t* entry = list + offset;
        const uint8_t* end = memchr(entry, 0, length - offset);
        if (end == NULL) {
            return 0;
        }

        entryLength = (size_t)(end - entry);
        if (entryLength == needleLength) {
            if (memcmp(entry, needle, needleLength) == 0) {
                return 1;
            }
        }
        offset += (uint32_t)entryLength + 1;
    }
    return 0;
}

static int
__FdtCellCountSupported(
    _In_ uint32_t count)
{
    return count != 0 && count <= 2;
}

static int
__FdtTranslateBusAddress(
    _In_    const struct FdtResources* bus,
    _In_    const struct FdtResources* parentBus,
    _In_    uint64_t                   length,
    _InOut_ uint64_t*                  address,
    _In_    int                        dma)
{
    const uint8_t* ranges;
    uint32_t       rangesLength;
    uint32_t       cells;

    if (bus->Malformed || parentBus->Malformed) {
        return 0;
    }

    ranges = dma ? bus->DmaRanges : bus->Ranges;
    rangesLength = dma ? bus->DmaRangesLength : bus->RangesLength;
    if (dma && ranges == NULL) {
        return 1;
    }

    if (ranges == NULL) {
        return 0;
    }
    
    if (rangesLength == 0) {
        return 1;
    }

    if (!__FdtCellCountSupported(bus->AddressCells) ||
        !__FdtCellCountSupported(parentBus->AddressCells) ||
        !__FdtCellCountSupported(bus->SizeCells)) {
        return 0;
    }

    cells = bus->AddressCells + parentBus->AddressCells + bus->SizeCells;
    if (rangesLength % (cells * 4) != 0) {
        return 0;
    }

    for (uint32_t offset = 0; offset < rangesLength; offset += cells * 4) {
        uint64_t parentAddress;
        uint64_t child = FdtReadCells(ranges + offset, bus->AddressCells);
        uint64_t parent = FdtReadCells(
            ranges + offset + bus->AddressCells * 4,
            parentBus->AddressCells
        );
        uint64_t size = FdtReadCells(
            ranges + offset +
                (bus->AddressCells + parentBus->AddressCells) * 4,
            bus->SizeCells
        );

        if (!FdtContainsRange(child, size, *address, length)) {
            continue;
        }

        if (*address - child > UINT64_MAX - parent) {
            return 0;
        }

        parentAddress = parent + (*address - child);
        if (length == 0 || length - 1 > UINT64_MAX - parentAddress) {
            return 0;
        }

        *address = parentAddress;
        return 1;
    }
    return 0;
}

int
FdtTranslateAddress(
    _In_    const struct FdtResources* nodes,
    _In_    int                        depth,
    _In_    uint64_t                   length,
    _InOut_ uint64_t*                  address,
    _In_    int                        dma)
{
    int translated;

    if (nodes[depth].Malformed) {
        return 0;
    }

    for (; depth > 0; depth--) {
        translated = __FdtTranslateBusAddress(
            &nodes[depth],
            &nodes[depth - 1],
            length,
            address,
            dma
        );
        if (!translated) {
            return 0;
        }
    }
    return length != 0 && length - 1 <= UINT64_MAX - *address;
}

oserr_t
FdtNameIndex(
    _In_  const uint8_t* names,
    _In_  uint32_t       length,
    _In_  const char*    name,
    _Out_ uint32_t*      indexOut,
    _Out_ uint32_t*      countOut)
{
    uint32_t       offset = 0;
    uint32_t       count = 0;
    uint32_t       selected = UINT32_MAX;
    const uint8_t* end;

    if (length && names == NULL) {
        ERROR("FdtNameIndex: missing names data for %s (%u bytes)", name, length);
        return OS_EINVALPARAMS;
    }

    while (offset < length) {
        end = memchr(names + offset, 0, length - offset);
        if (end == NULL || end == names + offset) {
            ERROR("FdtNameIndex: malformed names entry at byte %u for %s", offset, name);
            return OS_EINVALPARAMS;
        }

        if (!strcmp((const char*)names + offset, name)) {
            if (selected != UINT32_MAX) {
                ERROR("FdtNameIndex: duplicate resource name %s", name);
                return OS_EINVALPARAMS;
            }
            selected = count;
        }
        offset = (uint32_t)(end - names) + 1;
        count++;
    }

    *indexOut = selected;
    *countOut = count;
    return selected == UINT32_MAX ? OS_ENOENT : OS_EOK;
}

int
FdtContainsRange(
    _In_ uint64_t base,
    _In_ uint64_t length,
    _In_ uint64_t child,
    _In_ uint64_t childLength)
{
    return childLength && child >= base && child - base < length &&
            childLength <= length - (child - base);
}

/**
 * @brief Decode the numeric and flag properties for a node.
 *
 * The caller must validate numeric properties as four bytes and controller
 * flags as empty properties before calling. device_type is checked here
 * against the complete zero-terminated strings "memory" and "pci".
 */
static int
__ResourceScalarProperty(
    _InOut_ struct FdtResources* node,
    _In_    const char*          name,
    _In_    const uint8_t*       value,
    _In_    uint32_t             length)
{
    if (!strcmp(name, "#address-cells")) {
        node->AddressCells = FdtReadBe32(value);
        node->HasAddressCells = 1;
    } else if (!strcmp(name, "#size-cells")) {
        node->SizeCells = FdtReadBe32(value);
    } else if (!strcmp(name, "#interrupt-cells")) {
        node->InterruptCells = FdtReadBe32(value);
    } else if (!strcmp(name, "#reset-cells")) {
        node->HasResetCells = 1;
        node->ResetCells = FdtReadBe32(value);
    } else if (!strcmp(name, "#clock-cells")) {
        node->HasClockCells = 1;
        node->ClockCells = FdtReadBe32(value);
    } else if (!strcmp(name, "#msi-cells")) {
        node->MsiCells = FdtReadBe32(value);
    } else if (!strcmp(name, "interrupt-parent")) {
        node->InterruptParent = FdtReadBe32(value);
    } else if (!strcmp(name, "interrupt-controller")) {
        node->IsInterruptController = 1;
    } else if (!strcmp(name, "msi-controller")) {
        node->IsMsiController = 1;
    } else if (!strcmp(name, "device_type")) {
        node->IsMemory = length == 7 && !memcmp(value, "memory", 7);
        node->IsPci = length == 4 && !memcmp(value, "pci", 4);
    } else {
        return 0;
    }
    return 1;
}

static int
__ResourceDataProperty(
    _InOut_ struct FdtResources* node,
    _In_    const char*          name,
    _In_    const uint8_t*       value,
    _In_    uint32_t             length)
{
    if (!strcmp(name, "reg")) {
        node->Reg = value;
        node->RegLength = length;
    } else if (!strcmp(name, "ranges")) {
        node->Ranges = value;
        node->RangesLength = length;
    } else if (!strcmp(name, "dma-ranges")) {
        node->DmaRanges = value;
        node->DmaRangesLength = length;
    } else if (!strcmp(name, "resets")) {
        node->Resets = value;
        node->ResetsLength = length;
    } else if (!strcmp(name, "reset-names")) {
        node->ResetNames = value;
        node->ResetNamesLength = length;
    } else if (!strcmp(name, "clocks")) {
        node->Clocks = value;
        node->ClocksLength = length;
    } else if (!strcmp(name, "clock-names")) {
        node->ClockNames = value;
        node->ClockNamesLength = length;
    } else if (!strcmp(name, "msi-parent")) {
        node->MsiParent = value;
        node->MsiParentLength = length;
    } else if (!strcmp(name, "msi-ranges")) {
        node->MsiRanges = value;
        node->MsiRangesLength = length;
    } else if (!strcmp(name, "interrupts")) {
        node->Interrupts = value;
        node->InterruptsLength = length;
    } else if (!strcmp(name, "interrupts-extended")) {
        node->InterruptsExtended = value;
        node->InterruptsExtendedLength = length;
    } else if (!strcmp(name, "interrupt-names")) {
        node->InterruptNames = value;
        node->InterruptNamesLength = length;
    } else {
        return 0;
    }
    return 1;
}

static oserr_t
__ResourceProperty(
    _InOut_ void*       context,
    _In_    const char* name,
    _In_    const void* property,
    _In_    uint32_t    length)
{
    struct FdtResources* node = context;
    const uint8_t*       value = property;

    if ((!strcmp(name, "#address-cells") || !strcmp(name, "#size-cells")) && length != 4) {
        node->Malformed = 1;
        return OS_EOK;
    }

    if ((!strcmp(name, "#interrupt-cells") || !strcmp(name, "#reset-cells")) && length != 4) {
        node->Malformed = 1;
        return OS_EOK;
    }

    if (!strcmp(name, "#clock-cells") && length != 4) {
        node->Malformed = 1;
        return OS_EOK;
    }

    if ((!strcmp(name, "#msi-cells") || !strcmp(name, "interrupt-parent")) && length != 4) {
        node->Malformed = 1;
        return OS_EOK;
    }

    if ((!strcmp(name, "interrupt-controller") || !strcmp(name, "msi-controller")) && length) {
        node->Malformed = 1;
        return OS_EOK;
    }

    if (__ResourceScalarProperty(node, name, value, length)) {
        return OS_EOK;
    }

    __ResourceDataProperty(node, name, value, length);
    return OS_EOK;
}

oserr_t
FdtRawRegister(
    _In_  const struct FdtResources* node,
    _In_  unsigned int              index,
    _Out_ uint64_t*                  base,
    _Out_ uint64_t*                  length)
{
    uint32_t ac = node->ParentAddressCells;
    uint32_t sc = node->ParentSizeCells;
    uint32_t stride = (ac + sc) * 4;

    if (!__FdtCellCountSupported(ac) || !__FdtCellCountSupported(sc)) {
        return OS_EINVALPARAMS;
    }

    if (!node->RegLength || node->RegLength % stride ||
        index >= node->RegLength / stride) {
        return OS_EINVALPARAMS;
    }

    *base = FdtReadCells(node->Reg + index * stride, ac);
    *length = FdtReadCells(node->Reg + index * stride + ac * 4, sc);
    return OS_EOK;
}

void
FdtDecodeResources(
    _In_    const struct FdtNode* view,
    _InOut_ struct FdtResources*  nodes,
    _In_    int                  index)
{
    struct FdtResources* node = &nodes[index];
    int                  translated;

    memset(node, 0, sizeof(*node));
    node->View = *view;
    node->Name = view->Name;
    node->NodeOffset = view->NodeOffset;
    node->Phandle = view->Phandle;
    node->Malformed = view->Malformed;
    node->Disabled = view->Disabled;
    node->ParentDisabled = view->ParentDisabled;
    node->ParentMalformed = index &&
            (nodes[index - 1].Malformed || nodes[index - 1].ParentMalformed);
    node->AddressCells = 2;
    node->SizeCells = 1;
    node->Compatible = FdtProperty(view, "compatible", &node->CompatibleLength);
    node->IsSimpleBus = FdtCompatible(view, "simple-bus");
    
    if (index) {
        node->ParentAddressCells = nodes[index - 1].AddressCells;
        node->ParentSizeCells = nodes[index - 1].SizeCells;
        node->InterruptParent = nodes[index - 1].IsInterruptController ?
                nodes[index - 1].Phandle : nodes[index - 1].InterruptParent;
    }

    for (size_t property = 0; property < view->PropertyCount; property++) {
        __ResourceProperty(
            node,
            view->Properties[property].Name,
            view->Properties[property].Value,
            view->Properties[property].Length
        );
    }

    node->RegisterStatus = FdtRawRegister(
        node,
        0,
        &node->PhysicalBase,
        &node->PhysicalLength
    );
    if (node->RegisterStatus != OS_EOK) {
        // Many nodes have no CPU register range. The caller decides whether
        // that matters for this device and can report it with context.
        return;
    }

    if (!index) {
        node->RegisterStatus = OS_EINVALPARAMS;
        return;
    }

    translated = FdtTranslateAddress(
        nodes,
        index - 1,
        node->PhysicalLength,
        &node->PhysicalBase,
        0
    );
    if (!translated) {
        node->RegisterStatus = OS_EINVALPARAMS;
    }
}

oserr_t
FdtNextReference(
    _In_    const struct FdtTree* tree,
    _In_    const uint8_t*        cells,
    _In_    uint32_t              length,
    _In_    const char*           cellsName,
    _In_    uint32_t              inheritedProvider,
    _InOut_ uint32_t*             offset,
    _Out_   struct FdtResources*  provider,
    _Out_   const uint8_t**       arguments)
{
    struct FdtResources result;
    uint32_t            cursor = *offset;
    uint32_t            phandle = inheritedProvider;
    uint32_t            count;
    oserr_t             status;

    if (cells == NULL || (length & 3)) {
        ERROR("FdtNextReference: invalid %s reference at byte %u of %u",
            cellsName, cursor, length);
        return OS_EINVALPARAMS;
    }

    if (cursor >= length || (cursor & 3)) {
        ERROR("FdtNextReference: invalid %s reference at byte %u of %u",
            cellsName, cursor, length);
        return OS_EINVALPARAMS;
    }

    if (!inheritedProvider) {
        phandle = FdtReadBe32(cells + cursor);
        cursor += 4;
    }

    status = FdtFindResources(tree, phandle, &result);
    if (status != OS_EOK) {
        WARNING("FdtNextReference: %s provider %u unavailable (%u)", cellsName, phandle, status);
        return status;
    }

    status = FdtScalar(&result.View, cellsName, &count);
    if (status != OS_EOK) {
        ERROR("FdtNextReference: provider %u lacks usable %s (%u)", phandle, cellsName, status);
        return OS_EINVALPARAMS;
    }

    if (count > 16 || count * 4 > length - cursor ||
        (inheritedProvider && !count)) {
        ERROR("FdtNextReference: provider %u has invalid %s argument count %u",
            phandle, cellsName, count);
        return OS_EINVALPARAMS;
    }

    *provider = result;
    *arguments = cells + cursor;
    *offset = cursor + count * 4;
    return OS_EOK;
}
