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

#include <firmware/resources.h>
#include <string.h>

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
    uint32_t offset       = 0;

    while (offset < length) {
        const uint8_t* entry = list + offset;
        const uint8_t* end   = memchr(entry, 0, length - offset);
        size_t         entryLength;

        if (end == NULL) {
            return 0;
        }

        entryLength = (size_t)(end - entry);
        if (entryLength == needleLength && memcmp(entry, needle, needleLength) == 0) {
            return 1;
        }
        offset += (uint32_t)entryLength + 1;
    }
    return 0;
}
int
FdtTranslateAddress(
        _In_ const struct FdtResources* nodes,
        _In_ int                    depth,
        _In_ uint64_t               length,
    _InOut_ uint64_t*               address,
        _In_ int                    dma)
{
    const struct FdtResources* bus;
    uint32_t               cells;
    uint32_t               offset;
    uint64_t               child;
    uint64_t               parent;
    uint64_t               size;
    int                    found;
    const uint8_t*         ranges;
    uint32_t               rangesLength;

    if (nodes[depth].Malformed) {
        return 0;
    }
    for (; depth > 0; depth--) {
        bus = &nodes[depth];
        if (bus->Malformed || nodes[depth - 1].Malformed) {
            return 0;
        }
        ranges = dma ? bus->DmaRanges : bus->Ranges;
        rangesLength = dma ? bus->DmaRangesLength : bus->RangesLength;
        if (dma && ranges == NULL) {
            continue;
        }
        if (ranges == NULL) {
            return 0;
        }
        if (rangesLength == 0) {
            continue;
        }
        if (bus->AddressCells == 0 || bus->AddressCells > 2 ||
            nodes[depth - 1].AddressCells == 0 || nodes[depth - 1].AddressCells > 2 ||
            bus->SizeCells == 0 || bus->SizeCells > 2) {
            return 0;
        }
        cells = bus->AddressCells + nodes[depth - 1].AddressCells + bus->SizeCells;
        if (rangesLength % (cells * 4) != 0) {
            return 0;
        }
        found = 0;
        for (offset = 0; offset < rangesLength; offset += cells * 4) {
            child = FdtReadCells(ranges + offset, bus->AddressCells);
            parent = FdtReadCells(ranges + offset + bus->AddressCells * 4,
                    nodes[depth - 1].AddressCells);
            size = FdtReadCells(ranges + offset +
                    (bus->AddressCells + nodes[depth - 1].AddressCells) * 4, bus->SizeCells);
            if (*address < child || *address - child > size ||
                length > size - (*address - child)) {
                continue;
            }
            if (*address - child > UINT64_MAX - parent ||
                length == 0 || length - 1 > UINT64_MAX - (parent + (*address - child))) {
                return 0;
            }
            *address = parent + (*address - child);
            found = 1;
            break;
        }
        if (!found) {
            return 0;
        }
    }
    return length != 0 && length - 1 <= UINT64_MAX - *address;
}
oserr_t
FdtNameIndex(
        _In_ const uint8_t* names,
        _In_ uint32_t length,
        _In_ const char* name,
        _Out_ uint32_t* indexOut,
        _Out_ uint32_t* countOut)
{
    uint32_t offset = 0;
    uint32_t count = 0;
    uint32_t selected = UINT32_MAX;
    const uint8_t* end;

    if (length && names == NULL) {
        return OS_EINVALPARAMS;
    }
    while (offset < length) {
        end = memchr(names + offset, 0, length - offset);
        if (end == NULL || end == names + offset) {
            return OS_EINVALPARAMS;
        }
        if (!strcmp((const char*)names + offset, name)) {
            if (selected != UINT32_MAX) {
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
static oserr_t
__ResourceProperty(
    _InOut_ void* context,
    _In_ const char* name,
    _In_ const void* property,
    _In_ uint32_t length)
{
    struct FdtResources* node = context;
    const uint8_t* value = property;

    if ((!strcmp(name, "#address-cells") || !strcmp(name, "#size-cells") ||
         !strcmp(name, "#interrupt-cells") || !strcmp(name, "#reset-cells") ||
         !strcmp(name, "#clock-cells") || !strcmp(name, "#msi-cells") ||
         !strcmp(name, "interrupt-parent")) && length != 4) {
        node->Malformed = 1;
        return OS_EOK;
    }
    if ((!strcmp(name, "interrupt-controller") || !strcmp(name, "msi-controller")) && length) {
        node->Malformed = 1;
        return OS_EOK;
    }
    if (!strcmp(name, "#address-cells") && length == 4) {
        node->AddressCells = FdtReadBe32(value);
        node->HasAddressCells = 1;
    } else if (!strcmp(name, "#size-cells") && length == 4) {
        node->SizeCells = FdtReadBe32(value);
    } else if (!strcmp(name, "#interrupt-cells") && length == 4) {
        node->InterruptCells = FdtReadBe32(value);
    } else if (!strcmp(name, "interrupt-controller")) {
        node->IsInterruptController = 1;
    } else if (!strcmp(name, "reg")) {
        node->Reg       = value;
        node->RegLength = length;
    } else if (!strcmp(name, "ranges")) {
        node->Ranges       = value;
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
    } else if (!strcmp(name, "#reset-cells") && length == 4) {
        node->HasResetCells = 1;
        node->ResetCells = FdtReadBe32(value);
    } else if (!strcmp(name, "#clock-cells") && length == 4) {
        node->HasClockCells = 1;
        node->ClockCells = FdtReadBe32(value);
    } else if (!strcmp(name, "device_type")) {
        node->IsMemory = length == 7 && !memcmp(value, "memory", 7);
    } else if (!strcmp(name, "msi-controller")) {
        node->IsMsiController = 1;
    } else if (!strcmp(name, "#msi-cells")) {
        node->MsiCells = FdtReadBe32(value);
    } else if (!strcmp(name, "msi-parent")) {
        node->MsiParent = value;
        node->MsiParentLength = length;
    } else if (!strcmp(name, "msi-ranges")) {
        node->MsiRanges = value;
        node->MsiRangesLength = length;
    } else if (!strcmp(name, "interrupt-parent")) {
        node->InterruptParent = FdtReadBe32(value);
    } else if (!strcmp(name, "interrupts")) {
        node->Interrupts = value;
        node->InterruptsLength = length;
    } else if (!strcmp(name, "interrupts-extended")) {
        node->InterruptsExtended = value;
        node->InterruptsExtendedLength = length;
    } else if (!strcmp(name, "interrupt-names")) {
        node->InterruptNames = value;
        node->InterruptNamesLength = length;
    }
    return OS_EOK;
}

oserr_t
FdtRawRegister(
    _In_ const struct FdtResources* node,
    _In_ unsigned int index,
    _Out_ uint64_t* base,
    _Out_ uint64_t* length)
{
    uint32_t ac = node->ParentAddressCells;
    uint32_t sc = node->ParentSizeCells;
    uint32_t stride = (ac + sc) * 4;

    if (!ac || ac > 2 || !sc || sc > 2 || !node->RegLength ||
        node->RegLength % stride || index >= node->RegLength / stride) {
        return OS_EINVALPARAMS;
    }
    *base = FdtReadCells(node->Reg + index * stride, ac);
    *length = FdtReadCells(node->Reg + index * stride + ac * 4, sc);
    return OS_EOK;
}

/** Holds the callback and caller data used when reading resource properties. */
struct __ResourceWalk {
    FdtResourceFn Visitor;
    void* Context;
};

static void
__ResourceVisit(
    _In_ const struct FdtNode* views,
    _In_ int depth,
    _InOut_ void* context)
{
    struct __ResourceWalk* walk = context;
    struct FdtResources nodes[FDT_MAX_DEPTH];
    struct FdtResources* node;
    struct FdtParser parser = { .Property = __ResourceProperty };
    int i;

    for (i = 0; i <= depth; i++) {
        node = &nodes[i];
        memset(node, 0, sizeof(*node));
        node->View = views[i];
        node->Name = views[i].Name;
        node->NodeOffset = views[i].NodeOffset;
        node->Phandle = views[i].Phandle;
        node->Malformed = views[i].Malformed;
        node->Disabled = views[i].Disabled;
        node->AncestorDisabled = views[i].AncestorDisabled;
        node->AncestorMalformed = i && (nodes[i - 1].Malformed || nodes[i - 1].AncestorMalformed);
        node->AddressCells = 2;
        node->SizeCells = 1;
        node->Compatible = FdtProperty(&views[i], "compatible", &node->CompatibleLength);
        if (i) {
            node->ParentAddressCells = nodes[i - 1].AddressCells;
            node->ParentSizeCells = nodes[i - 1].SizeCells;
            node->InterruptParent = nodes[i - 1].IsInterruptController ?
                nodes[i - 1].Phandle : nodes[i - 1].InterruptParent;
        }
        parser.UserData = node;
        FdtVisitProperties(views[i].Properties, views[i].PropertiesLength,
            views[i].Strings, views[i].StringsLength, &parser);
        node->RegisterStatus = FdtRawRegister(node, 0, &node->PhysicalBase, &node->PhysicalLength);
        if (node->RegisterStatus == OS_EOK &&
            !FdtTranslateAddress(nodes, i - 1, node->PhysicalLength, &node->PhysicalBase, 0)) {
            node->RegisterStatus = OS_EINVALPARAMS;
        }
    }
    walk->Visitor(nodes, depth, walk->Context);
}

oserr_t
FdtWalkResources(
    _In_ const void* blob,
    _In_ size_t length,
    _In_ FdtResourceFn visitor,
    _InOut_ void* context)
{
    struct __ResourceWalk walk = { visitor, context };

    return FdtWalkNodes(blob, length, __ResourceVisit, &walk);
}

/** Stores the resources for a node whose firmware ID was already checked for duplicates. */
struct __ResourceQuery {
    uint32_t Phandle;
    struct FdtResources Result;
};

static void
__ResourceFind(
    _In_ const struct FdtResources* nodes,
    _In_ int depth,
    _InOut_ void* context)
{
    struct __ResourceQuery* query = context;

    if (nodes[depth].Phandle == query->Phandle) {
        query->Result = nodes[depth];
    }
}

oserr_t
FdtFindResources(
    _In_ const void* blob,
    _In_ size_t length,
    _In_ uint32_t phandle,
    _Out_ struct FdtResources* provider)
{
    struct FdtNode identity;
    struct __ResourceQuery query = { .Phandle = phandle };
    oserr_t status = FdtFindNode(blob, length, phandle, &identity);

    if (status != OS_EOK) {
        return status;
    }
    status = FdtWalkResources(blob, length, __ResourceFind, &query);
    if (status != OS_EOK) {
        return status;
    }
    if (query.Result.Malformed || query.Result.AncestorMalformed) {
        return OS_EINVALPARAMS;
    }
    *provider = query.Result;
    return OS_EOK;
}

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
    _Out_ const uint8_t** arguments)
{
    struct FdtResources result;
    uint32_t cursor = *offset;
    uint32_t phandle = inheritedProvider;
    uint32_t count;
    oserr_t status;

    if (cells == NULL || (length & 3) || cursor >= length || (cursor & 3)) {
        return OS_EINVALPARAMS;
    }
    if (!inheritedProvider) {
        phandle = FdtReadBe32(cells + cursor);
        cursor += 4;
    }
    status = FdtFindResources(blob, blobLength, phandle, &result);
    if (status != OS_EOK) {
        return status;
    }
    status = FdtScalar(&result.View, cellsName, &count);
    if (status != OS_EOK || count > 16 || count * 4 > length - cursor ||
        (inheritedProvider && !count)) {
        return OS_EINVALPARAMS;
    }
    *provider = result;
    *arguments = cells + cursor;
    *offset = cursor + count * 4;
    return OS_EOK;
}
