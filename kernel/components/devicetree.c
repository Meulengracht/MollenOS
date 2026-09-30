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
 * Built from docs/specifications/devicetree-specification-v0.4.pdf
 */

#include <component/devicetree.h>

#define __STATIC_FDT_MAX_DEPTH 8

struct __ParserContext {
    oserr_t (*BeginNode)(void* userData, const char* name, uint32_t nameLength);
    oserr_t (*Property)(void* userData, const char* name, const void* value, uint32_t valueLength);
    oserr_t (*EndNode)(void* userData);
    void*    UserData;
};

static uint32_t
__ReadBe32(
    _In_ const void* value)
{
    const uint8_t* p = value;
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static oserr_t
__SkipPaddedValue(
    _In_    const uint8_t* block,
    _In_    uint32_t       size,
    _InOut_ uint32_t*      cursor,
    _In_    uint32_t       length)
{
    // Subtract before adding: a corrupt length must not wrap the cursor into
    // an earlier, apparently valid portion of the blob.
    if (length > size - *cursor) {
        return OS_EINVALPARAMS;
    }
    *cursor += length;
    uint32_t padding = (4 - (*cursor & 3)) & 3;
    if (padding > size - *cursor) {
        return OS_EINVALPARAMS;
    }
    while (padding--) {
        if (block[(*cursor)++] != 0) {
            return OS_EINVALPARAMS;
        }
    }
    return OS_EOK;
}

/**
 * @brief The devicetree structure is represented as a linear tree: the representation of each node begins with an FDT_BEGIN_NODE
 * token and ends with an FDT_END_NODE token. The node’s properties and subnodes (if any) are represented before the
 * FDT_END_NODE, so that the FDT_BEGIN_NODE and FDT_END_NODE tokens for those subnodes are nested within
 * those of the parent.
 * The structure block as a whole consists of the root node’s representation (which contains the representations for all other
 * nodes), followed by an FDT_END token to mark the end of the structure block as a whole.
 * More precisely, each node’s representation consists of the following components:
 * • (optionally) any number of FDT_NOP tokens
 * • FDT_BEGIN_NODE token
 * – The node’s name as a null-terminated string
 * – [zeroed padding bytes to align to a 4-byte boundary]
 * • For each property of the node:
 * – (optionally) any number of FDT_NOP tokens
 * – FDT_PROP token
 * ∗ property information as given in Section 5.4.1
 * ∗ [zeroed padding bytes to align to a 4-byte boundary]
 * • Representations of all child nodes in this format
 * • (optionally) any number of FDT_NOP tokens
 * • FDT_END_NODE token
 * Note that this process requires that all property definitions for a particular node precede any subnode definitions for that
 * node. Although the structure would not be ambiguous if properties and subnodes were intermingled, the code needed to
 * process a flat tree is simplified by this requirement.
 */

/**
 * @brief Memory node examples
 * 
 * memory@0 {
 *   device_type = "memory";
 *   reg = <0x000000000 0x00000000 0x00000000 0x80000000
 *   0x000000001 0x00000000 0x00000001 0x00000000>;
 * };
 * 
 * memory@0 {
 *   device_type = "memory";
 *   reg = <0x000000000 0x00000000 0x00000000 0x80000000>;
 * };
 * 
 * memory@100000000 {
 *   device_type = "memory";
 *   reg = <0x000000001 0x00000000 0x00000001 0x00000000>;
 * };
 * 
 * The reg property is used to define the address and size of the two memory ranges. The 2 GB I/O region is skipped. Note
 * that the #address-cells and #size-cells properties of the root node specify a value of 2, which means that two 32-bit
 * cells are required to define the address and length for the reg property of the memory node.
 */

/**
 * @brief Reserved memory node examples
 * Reserved memory is specified as a node under the /reserved-memory node. The operating system shall exclude reserved
 * memory from normal usage. One can create child nodes describing particular reserved (excluded from normal use) memory
 * regions. Such memory regions are usually designed for the special usage by various device drivers.
 * Parameters for each memory region can be encoded into the device tree with the following nodes:
 * Properties:
 *   #address-cells: Specifies the number of cells used to represent the address of the reserved memory region.
 *   #size-cells: Specifies the number of cells used to represent the size of the reserved memory region.
 *   ranges: Specifies the mapping of the reserved memory region within the parent address space.
 * 
 * Each child of the reserved-memory node specifies one or more regions of reserved memory. Each child node may either
 * use a reg property to specify a specific range of reserved memory, or a size property with optional constraints to request
 * a dynamically allocated block of memory.
 * Following the generic-names recommended practice, node names should reflect the purpose of the node (ie. “framebuffer”
 * or “dma-pool”). Unit address (@<address>) should be appended to the name if the node is a static allocation.
 * A reserved memory node requires either a reg property for static allocations, or a size property for dynamics allocations.
 * Dynamic allocations may use alignment and alloc-ranges properties to constrain where the memory is allocated from.
 * If both reg and size are present, then the region is treated as a static allocation with the reg property taking precedence
 * and size is ignored.
 */

static const char*
__GetStringFromBlock(
    _In_ const char* stringBlock,
    _In_ uint32_t    stringBlockSize,
    _In_ uint32_t    stringOffset)
{
    if (stringOffset >= stringBlockSize) {
        return NULL;
    }
    return stringBlock + stringOffset;
}

static oserr_t
__ParseStructureBlock(
    _In_ const void*             structureBlock,
    _In_ uint32_t                structureBlockSize,
    _In_ const char*             stringBlock,
    _In_ uint32_t                stringBlockSize,
    _In_ struct __ParserContext* context)
{
    const uint8_t* block = structureBlock;
    uint32_t       cursor = 0;
    uint32_t       depth = 0;
    int            rootSeen = 0;
    int            childrenStarted[__STATIC_FDT_MAX_DEPTH] = { 0 };
    oserr_t        status;

    // Validate each event before exposing its borrowed pointers. Callbacks may
    // accumulate state, but consumers must publish nothing until FDT_END and
    // all callbacks succeed: a valid prefix does not establish a valid tree.
    while (structureBlockSize - cursor >= 4) {
        uint32_t token = __ReadBe32(block + cursor);
        cursor += 4;
        switch (token) {
        case FDT_BEGIN_NODE: {
            const uint8_t* end = memchr(block + cursor, 0, structureBlockSize - cursor);
            if ((!depth && rootSeen)) {
                return OS_EINVALPARAMS;
            }
            uint32_t length = (uint32_t)(end - (block + cursor));
            const char* name = (const char*)block + cursor;
            if ((!depth && length) || (depth && !length) || memchr(name, '/', length)) {
                return OS_EINVALPARAMS;
            }
            if (depth == __STATIC_FDT_MAX_DEPTH) {
                return OS_EOVERFLOW;
            }
            status = __SkipPaddedValue(block, structureBlockSize, &cursor, length + 1);
            if (status != OS_EOK) {
                return status;
            }
            if (depth) {
                childrenStarted[depth - 1] = 1;
            }
            childrenStarted[depth++] = 0;
            rootSeen = 1;
            status = context->BeginNode(context->UserData, name, length);
            break;
        }
        case FDT_PROP: {
            if (!depth || childrenStarted[depth - 1] || structureBlockSize - cursor < 8) {
                return OS_EINVALPARAMS;
            }
            uint32_t length = __ReadBe32(block + cursor);
            uint32_t nameOffset = __ReadBe32(block + cursor + 4);
            const char* name = __GetStringFromBlock(stringBlock, stringBlockSize, nameOffset);
            if (!name || !name[0]) {
                return OS_EINVALPARAMS;
            }
            cursor += 8;
            const void* value = block + cursor;
            status = __SkipPaddedValue(block, structureBlockSize, &cursor, length);
            if (status != OS_EOK) {
                return status;
            }
            status = context->Property(context->UserData, name, value, length);
            break;
        }
        case FDT_END_NODE:
            if (!depth) {
                return OS_EINVALPARAMS;
            }
            status = context->EndNode(context->UserData);
            depth--;
            break;
        case FDT_NOP:
            continue;
        case FDT_END:
            // size_dt_struct includes this final token. Reject trailing data,
            // multiple roots, and trees whose last node was never closed.
            return rootSeen && !depth && cursor == structureBlockSize ?
                OS_EOK : OS_EINVALPARAMS;
        default:
            return OS_EINVALPARAMS;
        }
        
        if (status != OS_EOK) {
            return status;
        }
    }
    return OS_EINVALPARAMS;
}

enum __NodeType {
    NodeTypeOther,
    NodeTypeRoot,
    NodeTypeMemory,
    NodeTypeReservedMemory,
    NodeTypeReservation
};

struct __NodeProperty {
    const void* Value;
    uint32_t    Length;
    int         Present;
};

struct __NodeFrame {
    const char*     Name;
    uint32_t        NameLength;
    enum __NodeType Type;
    uint32_t        PropertiesRead;

    uint32_t AddressCells;
    uint32_t SizeCells;
    int      Enabled;
    int      IsMemory;
    int      NoMap;
    int      Reusable;
    
    struct __NodeProperty Reg;
    struct __NodeProperty Ranges;
    struct __NodeProperty Size;
    struct __NodeProperty Alignment;
    struct __NodeProperty AllocRanges;
};

static void
__SetDefaultProperties(
    _In_ struct __NodeFrame* parent,
    _In_ struct __NodeFrame* frame,
    _In_ const char*         name,
    _In_ uint32_t            nameLength)
{
    frame->Name = name;
    frame->NameLength = nameLength;
    frame->AddressCells = 2;
    frame->SizeCells = 1;
    frame->Enabled = parent ? parent->Enabled : 1;
}

struct __MemoryMapBuilder {
    struct __NodeFrame Frames[__STATIC_FDT_MAX_DEPTH];
    int                FrameIndex;

    void*     MemoryMapPointer;
    uint32_t  MemoryMapMaxSize;
    uint32_t* MemoryMapEntryCountOut;
};

static oserr_t
__ParseMemoryMapBeginNode(
    _In_ void*       userData,
    _In_ const char* name,
    _In_ uint32_t    nameLength) 
{
    struct __MemoryMapBuilder* context = userData;
    struct __NodeFrame*        frame;
    struct __NodeFrame*        parent;

    // Check if the frame index is within valid bounds.
    if (context->FrameIndex < -1 || context->FrameIndex >= __STATIC_FDT_MAX_DEPTH - 1) {
        return OS_EOVERFLOW;
    }

    // Get frames
    parent = context->FrameIndex >= 0 ? &context->Frames[context->FrameIndex] : NULL;
    frame = &context->Frames[++context->FrameIndex];
    
    __SetDefaultProperties(parent, frame, name, nameLength);
    
    if (parent == NULL) {
        frame->Type = NodeTypeRoot;
    } else if (parent->Type == NodeTypeReservedMemory) {
        frame->Type = NodeTypeReservation;
    } else {
        if (!strncmp(name, "memory")) {
            frame->Type = NodeTypeMemory;
        } else if (!strcmp(name, "reserved-memory")) {
            frame->Type = NodeTypeReservedMemory;
        }
    }
    return OS_EOK;
}

static oserr_t
__ParseMemoryMapEndNode(
    _In_ void* userData)
{
    struct __MemoryMapBuilder* context = userData;
    // TODO: validate completed memory/reservation nodes and emit normalized
    // ranges here. Parsing properties alone must not publish available RAM.
    context->FrameIndex--;
    return OS_EOK;
}

// Indices also track duplicates of properties whose interpretation affects
// memory ownership. Ambiguous values must not be resolved by input order.
static const char* const g_memoryProperties[] = {
    "#address-cells", "#size-cells", "status", "device_type", "reg",
    "ranges", "size", "alignment", "alloc-ranges", "no-map", "reusable"
};

static unsigned int
__GetMemoryPropertyIndex(
    _In_ struct __NodeFrame* frame,
    _In_ const char*         name)
{
    unsigned int property;
    for (property = 0; property < SIZEOF_ARRAY(g_memoryProperties); property++) {
        if (frame->PropertiesRead & ((uint32_t)1 << property)) {
            continue;
        }

        if (!strcmp(name, g_memoryProperties[property])) {
            break;
        }
    }
    return property;
}

static oserr_t
__ParseMemoryMapProperty(
    _In_ void*       userData,
    _In_ const char* name,
    _In_ const void* value,
    _In_ uint32_t    valueLength)
{
    struct __MemoryMapBuilder* context = userData;
    struct __NodeFrame*        parent;
    struct __NodeFrame*        frame;
    unsigned int               property;
    
    parent = context->FrameIndex > 0 ? &context->Frames[context->FrameIndex - 1] : NULL;
    frame = &context->Frames[context->FrameIndex];

    property = __GetMemoryPropertyIndex(frame, name);
    if (property == SIZEOF_ARRAY(g_memoryProperties)) {
        return OS_EOK;
    }
    frame->PropertiesRead |= ((uint32_t)1 << property);

    switch (property) {
        case 0: // "#address-cells"
            if (valueLength != 4) {
                return OS_EINVALPARAMS;
            }
            frame->AddressCells = __ReadBe32(value);
            break;
        case 1: // "#size-cells"
            if (valueLength != 4) {
                return OS_EINVALPARAMS;
            }
            frame->SizeCells = __ReadBe32(value);
            break;
        case 2: // "status"
            if (parent == NULL) {
                frame->Enabled = (!strcmp(value, "okay") || !strcmp(value, "ok"));
            } else {
                frame->Enabled = parent->Enabled && (!strcmp(value, "okay") || !strcmp(value, "ok"));
            }
            break;
        case 3: // "device_type"
            frame->IsMemory = !strcmp(value, "memory");
            break;
        case 4: // "reg"
            frame->Reg.Value = value;
            frame->Reg.Length = valueLength;
            frame->Reg.Present = 1;
            break;
        case 5: // "ranges"
            frame->Ranges.Value = value;
            frame->Ranges.Length = valueLength;
            frame->Ranges.Present = 1;
            break;
        case 6: // "size"
            frame->Size.Value = value;
            frame->Size.Length = valueLength;
            frame->Size.Present = 1;
            break;
        case 7: // "alignment"
            frame->Alignment.Value = value;
            frame->Alignment.Length = valueLength;
            frame->Alignment.Present = 1;
            break;
        case 8: // "alloc-ranges"
            frame->AllocRanges.Value = value;
            frame->AllocRanges.Length = valueLength;
            frame->AllocRanges.Present = 1;
            break;
        case 9: // "no-map"
            if (valueLength) {
                return OS_EINVALPARAMS;
            }
            frame->NoMap = 1;
            break;
        case 10: // "reusable"
            if (valueLength) {
                return OS_EINVALPARAMS;
            }
            frame->Reusable = 1;
            break;
        default:
            // Unknown property
            break;
    }
    
    if (frame->NoMap && frame->Reusable) {
        return OS_EINVALPARAMS;
    }

    return OS_EOK;
}

static oserr_t
__ParseMemoryReservationBlock(
    _In_ const void* reservationBlock)
{
    // Read fixed 64-bit address/size pairs until the (0, 0) terminator.
    return OS_EOK;
}

static oserr_t
__ValidateFDTHeader(
    _In_ struct FDTHeader* header)
{
    if (header->Magic != 0xD00DFEED || header->TotalSize < 40 || header->TotalSize > size) {
        return OS_EINVALPARAMS;
    }
    
    if (header->Version < 17 || header->LastCompVersion > 17 ||
        header->LastCompVersion > header->Version) {
        return OS_ENOTSUPPORTED;
    }
    
    // This component uses the standard v17 block order. The interval before
    // the structure block bounds reservation reads, including their terminator.
    if (header->OffMemRsvmap < 40 || (header->OffMemRsvmap & 7) ||
        header->OffMemRsvmap > header->OffDtStruct || (header->OffDtStruct & 3) ||
        header->OffDtStruct > header->OffDtStrings ||
        header->OffDtStrings > header->TotalSize ||
        header->SizeDtStruct > header->OffDtStrings - header->OffDtStruct ||
        header->SizeDtStrings > header->TotalSize - header->OffDtStrings) {
        return OS_EINVALPARAMS;
    }
    return OS_EOK;
}

static oserr_t
__ParseFDTHeader(
    _In_  const void*       deviceTree,
    _In_  uint32_t          size,
    _Out_ struct FDTHeader* header)
{
    const uint8_t* p = deviceTree;
    
    if (deviceTree == NULL || size < 40) {
        return OS_EINVALPARAMS;
    }

    header->Magic = __ReadBe32(p);
    header->TotalSize = __ReadBe32(p + 4);
    header->OffDtStruct = __ReadBe32(p + 8);
    header->OffDtStrings = __ReadBe32(p + 12);
    header->OffMemRsvmap = __ReadBe32(p + 16);
    header->Version = __ReadBe32(p + 20);
    header->LastCompVersion = __ReadBe32(p + 24);
    header->BootCpuidPhys = __ReadBe32(p + 28);
    header->SizeDtStruct = __ReadBe32(p + 32);
    header->SizeDtStrings = __ReadBe32(p + 36);
    return __ValidateFDTHeader(header);
}

oserr_t
DeviceTreeBuildMemoryMap(
    _In_  const void* deviceTree,
    _In_  uint32_t    deviceTreeSize,
    _In_  void*       memoryMap,
    _In_  uint32_t    memoryMapMaxSize,
    _Out_ uint32_t*   memoryMapEntryCountOut)
{
    const uint8_t*            p = deviceTree;
    struct FDTHeader          header;
    oserr_t                   oserr;
    struct __MemoryMapBuilder memoryMapBuilder = {
        .FrameIndex = -1,
        .MemoryMapPointer = memoryMap,
        .MemoryMapMaxSize = memoryMapMaxSize,
        .MemoryMapEntryCountOut = memoryMapEntryCountOut,
    };
    struct __ParserContext context = {
        .BeginNode = __ParseMemoryMapBeginNode,
        .EndNode = __ParseMemoryMapEndNode,
        .Property = __ParseMemoryMapProperty,
        .UserData = &memoryMapBuilder,
    };

    oserr = __ParseFDTHeader(deviceTree, deviceTreeSize, &header);
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = __ParseStructureBlock(
        p + header.OffDtStruct,
        header.SizeDtStruct,
        (const char*)p + header.OffDtStrings,
        header.SizeDtStrings,
        &context
    );
    if (oserr != OS_EOK) {
        return oserr;
    }
    
    return __ParseMemoryReservationBlock(
        p + header.OffMemRsvmap,
        header.OffDtStruct - header.OffMemRsvmap
    );
}
