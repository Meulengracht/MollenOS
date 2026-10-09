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

/**
 * @brief Join big-endian 32-bit values into the integer they represent.
 *
 * Device-tree addresses can be wider than one 32-bit cell. Reading one cell
 * at a time and shifting the earlier value left preserves the device-tree
 * order, where the most significant cell comes first.
 */
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

/**
 * @brief Search a bounded list without reading beyond its final byte.
 *
 * Each string ends at a zero byte. memchr is given only the bytes that remain
 * in the property, so a broken final string is rejected instead of allowing
 * the search to continue into unrelated firmware data.
 */
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

static int
__FdtCellCountSupported(
    _In_ uint32_t count)
{
    return count != 0 && count <= 2;
}

/**
 * @brief Translate an address through one bus mapping.
 *
 * One mapping entry connects a range used by a bus's children to a range in
 * its parent bus. Keeping this step separate lets the caller focus on walking
 * up the bus chain, while this helper checks and applies exactly one mapping.
 */
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
    uint32_t       offset;
    uint64_t       child;
    uint64_t       parent;
    uint64_t       size;
    uint64_t       parentAddress;

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

    for (offset = 0; offset < rangesLength; offset += cells * 4) {
        child = FdtReadCells(ranges + offset, bus->AddressCells);
        parent = FdtReadCells(
            ranges + offset + bus->AddressCells * 4,
            parentBus->AddressCells
        );
        size = FdtReadCells(
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

/**
 * @brief Apply each parent bus mapping from the starting bus toward the root.
 *
 * A device address may be translated more than once, once for every bus in
 * its path. This function coordinates those single-bus steps and then checks
 * that the final address can hold the complete requested range.
 */
int
FdtTranslateAddress(
    _In_    const struct FdtResources* nodes,
    _In_    int                        depth,
    _In_    uint64_t                   length,
    _InOut_ uint64_t*                  address,
    _In_    int                        dma)
{
    if (nodes[depth].Malformed) {
        return 0;
    }

    for (; depth > 0; depth--) {
        int translated = __FdtTranslateBusAddress(
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

/**
 * @brief Validate a name list and find the unique position of a requested name.
 *
 * The full list must be checked because callers use the returned position to
 * select a matching resource value. Accepting an incomplete list or a
 * duplicate requested name could select the wrong resource while appearing
 * successful.
 */
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

/**
 * @brief Check containment without calculating either range's end address.
 *
 * End-address addition can overflow near the top of the address space. Once
 * the child's start is known to be at or above the base, subtraction gives its
 * offset; comparing lengths against the remaining space is safe.
 */
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
 * These properties fit in one 32-bit value or indicate that the node provides
 * a resource. Handling them together keeps value decoding separate from the
 * byte lists that are stored by reference into the firmware tree.
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
    } else {
        return 0;
    }
    return 1;
}

/**
 * @brief Keep the byte lists that describe a node's resources.
 *
 * These properties may contain one or many bytes, so the record keeps their
 * location and length instead of interpreting or copying them here. Later
 * helpers can read each list according to its own format.
 */
static int
__ResourceDataProperty(
    _InOut_ struct FdtResources* node,
    _In_ const char* name,
    _In_ const uint8_t* value,
    _In_ uint32_t length)
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

/**
 * @brief Validate and dispatch one resource property.
 *
 * Fixed-size values are decoded only when their lengths are correct. Invalid
 * fixed-size properties mark the node malformed; the tree walk can still
 * visit it, while resource consumers can avoid trusting its data.
 */
static oserr_t
__ResourceProperty(
    _InOut_ void*       context,
    _In_    const char* name,
    _In_    const void* property,
    _In_    uint32_t    length)
{
    struct FdtResources* node = context;
    const uint8_t*       value = property;

    if ((!strcmp(name, "#address-cells") || !strcmp(name, "#size-cells") ||
         !strcmp(name, "#interrupt-cells") || !strcmp(name, "#reset-cells") ||
         !strcmp(name, "#clock-cells")) && length != 4) {
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

/**
 * @brief Decode one register entry using the format chosen by its parent bus.
 *
 * Each entry has an address followed by a size, but the parent bus determines
 * how many 32-bit values make up each part. Checking the cell counts and the
 * complete entry before reading prevents an incomplete property from being
 * mistaken for a valid hardware range.
 */
oserr_t
FdtRawRegister(
    _In_  const struct FdtResources* node,
    _In_  unsigned int               index,
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

struct __ResourceWalk {
    FdtResourceFn Visitor;
    void*         Context;
};

/**
 * @brief Decode one node into its resource record.
 *
 * The node's parent has already been prepared in the records array. Its cell
 * counts and interrupt-controller information are needed to interpret this
 * node, so they are copied before its properties and register address are
 * processed.
 */
static void
__ResourceBuildNode(
    _In_ const struct FdtNode* views,
    _In_ struct FdtResources*  nodes,
    _In_ int                   index)
{
    struct FdtResources* node = &nodes[index];
    struct FdtParser     parser = { .Property = __ResourceProperty };

    memset(node, 0, sizeof(*node));
    node->View = views[index];
    node->Name = views[index].Name;
    node->NodeOffset = views[index].NodeOffset;
    node->Phandle = views[index].Phandle;
    node->Malformed = views[index].Malformed;
    node->Disabled = views[index].Disabled;
    node->AncestorDisabled = views[index].AncestorDisabled;
    node->AncestorMalformed = index &&
            (nodes[index - 1].Malformed || nodes[index - 1].AncestorMalformed);
    node->AddressCells = 2;
    node->SizeCells = 1;
    node->Compatible = FdtProperty(&views[index], "compatible", &node->CompatibleLength);
    if (index) {
        node->ParentAddressCells = nodes[index - 1].AddressCells;
        node->ParentSizeCells = nodes[index - 1].SizeCells;
        node->InterruptParent = nodes[index - 1].IsInterruptController ?
                nodes[index - 1].Phandle : nodes[index - 1].InterruptParent;
    }

    parser.UserData = node;
    
    FdtVisitProperties(
        views[index].Properties,
        views[index].PropertiesLength,
        views[index].Strings,
        views[index].StringsLength,
        &parser
    );

    node->RegisterStatus = FdtRawRegister(
        node,
        0,
        &node->PhysicalBase,
        &node->PhysicalLength
    );
    if (node->RegisterStatus == OS_EOK) {
        int translated = FdtTranslateAddress(
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
}

/**
 * @brief Prepare the records from the tree root through the current node.
 *
 * Processing in root-to-leaf order makes each parent's resource settings
 * available before they are needed by its child. Once the path is ready, the
 * caller's visitor receives the complete array for this node.
 */
static void
__ResourceVisit(
    _In_    const struct FdtNode* views,
    _In_    int                   depth,
    _InOut_ void*                 context)
{
    struct __ResourceWalk* walk = context;
    struct FdtResources    nodes[FDT_MAX_DEPTH];

    for (int i = 0; i <= depth; i++) {
        __ResourceBuildNode(views, nodes, i);
    }
    walk->Visitor(nodes, depth, walk->Context);
}

/**
 * @brief Walk the tree and provide resource details for each node.
 *
 * This wrapper keeps the caller's visitor and context together for the tree
 * reader. The reader handles the tree's structure; __ResourceVisit interprets
 * each node's resource properties and supplies its ancestors as context.
 */
oserr_t
FdtWalkResources(
    _In_    const void*   blob,
    _In_    size_t        length,
    _In_    FdtResourceFn visitor,
    _InOut_ void*         context)
{
    struct __ResourceWalk walk = { visitor, context };
    return FdtWalkNodes(blob, length, __ResourceVisit, &walk);
}

struct __ResourceQuery {
    uint32_t            Phandle;
    struct FdtResources Result;
};

/**
 * @brief Copy the resource record for the node whose ID is being searched.
 *
 * The general resource walk visits every node. Keeping the match operation in
 * its callback lets FdtFindResources reuse the same parsing rules as callers
 * that walk the whole tree.
 */
static void
__ResourceFind(
    _In_    const struct FdtResources* nodes,
    _In_    int                        depth,
    _InOut_ void*                      context)
{
    struct __ResourceQuery* query = context;
    if (nodes[depth].Phandle == query->Phandle) {
        query->Result = nodes[depth];
    }
}

/**
 * @brief Look up one identified node and return its parsed resource details.
 *
 * The initial lookup verifies that the requested ID exists and can be used.
 * A resource walk then builds the same ancestor-aware record used by normal
 * traversal. This keeps single-node lookups consistent with full-tree visits.
 */
oserr_t
FdtFindResources(
    _In_  const void*          blob,
    _In_  size_t               length,
    _In_  uint32_t             phandle,
    _Out_ struct FdtResources* provider)
{
    struct FdtNode         identity;
    struct __ResourceQuery query = { .Phandle = phandle };
    oserr_t                status;

    status = FdtFindNode(blob, length, phandle, &identity);
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

/**
 * @brief Parse one provider reference and its provider-defined arguments.
 *
 * The argument count belongs to the referenced provider, not to the property
 * containing the reference. Looking up the provider before consuming its
 * arguments is therefore necessary to find the next entry reliably. All
 * outputs are assigned at the end so a malformed or incomplete entry does not
 * move the caller's cursor or leave it with a partly filled result.
 */
oserr_t
FdtNextReference(
    _In_    const void*          blob,
    _In_    size_t               blobLength,
    _In_    const uint8_t*       cells,
    _In_    uint32_t             length,
    _In_    const char*          cellsName,
    _In_    uint32_t             inheritedProvider,
    _InOut_ uint32_t*            offset,
    _Out_   struct FdtResources* provider,
    _Out_   const uint8_t**      arguments)
{
    struct FdtResources result;
    uint32_t            cursor = *offset;
    uint32_t            phandle = inheritedProvider;
    uint32_t            count;
    oserr_t             status;

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
