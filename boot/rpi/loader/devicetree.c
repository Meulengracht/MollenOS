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

// The DTB memory parser constructs VBootEntries from the memory
// nodes in the device tree blob (DTB). We do this here because DTB
// platforms need a way to fill in memory information for a shared
// machine initialization.
#include <vboot/vboot.h>
#include <string.h>

// Include devicetree framework header, this provides
// everything we need
#include "private.h"
#include "loader.h"

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

enum __NodeType {
    NodeTypeOther,
    NodeTypeRoot,
    NodeTypeChosen,
    NodeTypeCpus,
    NodeTypeCpu,
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
    // Initialize the node frame to default values.
    memset(frame, 0, sizeof(struct __NodeFrame));
    frame->Name = name;
    frame->NameLength = nameLength;
    frame->AddressCells = 2;
    frame->SizeCells = 1;
    frame->Enabled = parent ? parent->Enabled : 1;
}

struct __MemoryMapBuilder {
    struct __NodeFrame     Frames[__STATIC_FDT_MAX_DEPTH];
    int                    FrameIndex;
    const char*            CompatibleBoard;
    struct RpiBootContext* Platform;

    // Node frames are reused after EndNode. These facts must survive that reuse
    // so final validation does not depend on property or sibling ordering.
    int                   CompatibleSeen;
    int                   CompatibleMatched;
    int                   ChosenSeen;
    unsigned int          InitrdSeen;
    uint64_t              InitrdStart;
    uint64_t              InitrdEnd;
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
    } else if (parent->Type == NodeTypeCpus &&
        (!strcmp(name, "cpu") || !strncmp(name, "cpu@", 4))) {
        frame->Type = NodeTypeCpu;
    } else if (parent->Type == NodeTypeRoot) {
        // Nodes that are supported under the Root node
        if (!strcmp(name, "memory") || !strncmp(name, "memory@", 7)) {
            frame->Type = NodeTypeMemory;
        } else if (!strcmp(name, "reserved-memory")) {
            frame->Type = NodeTypeReservedMemory;
        } else if (!strcmp(name, "chosen")) {
            // A second chosen node would make endpoint ownership ambiguous.
            if (context->ChosenSeen) {
                return OS_EINVALPARAMS;
            }
            context->ChosenSeen = 1;
            frame->Type = NodeTypeChosen;
        } else if (!strcmp(name, "cpus")) {
            frame->Type = NodeTypeCpus;
        }
    }
    return OS_EOK;
}

static uint64_t
__ReadMemoryCells(
    _In_ const uint8_t* value,
    _In_ uint32_t       cells)
{
    // The caller limits widths to one or two cells. Byte-safe reads also work
    // for two-cell values whose address is only four-byte aligned in the DTB.
    uint64_t result = __ReadBe32(value);
    if (cells == 2) {
        result = (result << 32) | __ReadBe32(value + 4);
    }
    return result;
}

static oserr_t
__InsertMemoryRange(
    _In_ struct __MemoryMapBuilder* context,
    _In_ uint64_t                   base,
    _In_ uint64_t                   length,
    _In_ int                        reserved)
{
    struct VBootMemoryEntry* entries = context->Platform->MemoryMap;
    
    uint32_t count = context->Platform->MemoryMapCount;
    uint32_t capacity = sizeof(context->Platform->MemoryMap) / sizeof(struct VBootMemoryEntry);
    uint32_t index = 0;
    uint64_t cursor = base;
    uint64_t end = base + length;

    // Maintain a sorted, disjoint map as each node closes. Reservations can
    // precede RAM in the tree, so later available ranges fill only gaps and
    // can never overwrite a reservation already recorded here. Reservations
    // outside described RAM remain reserved; they never manufacture free RAM.
    while (cursor < end) {
        uint64_t boundary;
        uint32_t left;
        uint32_t right;
        uint32_t extra;
        struct VBootMemoryEntry original;
        struct VBootMemoryEntry entry = {
            .Type = reserved ? VBootMemoryType_Reserved : VBootMemoryType_Available,
            .PhysicalBase = cursor,
            .VirtualBase = 0,
            .Length = 0,
            .Attributes = 0
        };

        while (index < count && entries[index].PhysicalBase + entries[index].Length <= cursor) {
            index++;
        }
        if (index == count || cursor < entries[index].PhysicalBase) {
            boundary = index < count && entries[index].PhysicalBase < end ?
                entries[index].PhysicalBase : end;
            // Extending an adjacent entry needs no temporary slot. In
            // particular, contiguous RAM banks must fit a one-entry buffer.
            if (index && entries[index - 1].Type == entry.Type &&
                entries[index - 1].PhysicalBase + entries[index - 1].Length == cursor) {
                entries[index - 1].Length += boundary - cursor;
                cursor = boundary;
                continue;
            }
            if (index < count && entries[index].Type == entry.Type &&
                boundary == entries[index].PhysicalBase) {
                entries[index].Length += boundary - cursor;
                entries[index].PhysicalBase = cursor;
                cursor = boundary;
                continue;
            }
            if (count == capacity) {
                return OS_EBUFFER;
            }
            for (uint32_t i = count; i > index; i--) {
                entries[i] = entries[i - 1];
            }
            entry.Length = boundary - cursor;
            entries[index++] = entry;
            count++;
            cursor = boundary;
            continue;
        }

        original = entries[index];
        boundary = original.PhysicalBase + original.Length;
        if (boundary > end) {
            boundary = end;
        }
        if (!reserved || original.Type == VBootMemoryType_Reserved) {
            cursor = boundary;
            continue;
        }

        // A reservation inside RAM replaces the overlap, retaining both RAM
        // tails when needed. Check capacity before shifting, so no write can
        // escape the caller's buffer. A later error invalidates the whole map.
        left = original.PhysicalBase < cursor;
        right = boundary < original.PhysicalBase + original.Length;
        extra = left + right;
        if (extra > capacity - count) {
            return OS_EBUFFER;
        }
        for (uint32_t i = count; i > index + 1; i--) {
            entries[i - 1 + extra] = entries[i - 1];
        }
        if (left) {
            entries[index] = original;
            entries[index++].Length = cursor - original.PhysicalBase;
        }
        entry.Length = boundary - cursor;
        entries[index++] = entry;
        if (right) {
            entries[index] = original;
            entries[index].PhysicalBase = boundary;
            entries[index].Length = original.PhysicalBase + original.Length - boundary;
        }
        count += extra;
        cursor = boundary;
    }

    // Coalesce equivalent neighbours to keep repeated/overlapping reg tuples
    // from consuming the fixed boot buffer. Attributes are deliberately zero:
    // this parser describes ownership, not a virtual mapping or cache policy.
    uint32_t output = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (output && entries[output - 1].Type == entries[i].Type &&
            entries[output - 1].PhysicalBase + entries[output - 1].Length == entries[i].PhysicalBase) {
            entries[output - 1].Length += entries[i].Length;
        } else {
            entries[output++] = entries[i];
        }
    }
    context->Platform->MemoryMapCount = output;
    return OS_EOK;
}

static oserr_t
__ValidateReservedMemoryNode(
    _In_ const struct __NodeFrame* frame,
    _In_ const struct __NodeFrame* root)
{
    // Empty ranges explicitly establishes identity translation. Missing or
    // nonempty ranges cannot be treated as physical addresses by this loader.
    if (!root || root->Type != NodeTypeRoot ||
        (frame->PropertiesRead & 3) != 3 || !frame->Ranges.Present) {
        return OS_EINVALPARAMS;
    }
    
    if (frame->AddressCells != root->AddressCells || frame->SizeCells != root->SizeCells) {
        return OS_EINVALPARAMS;
    }
    
    if (frame->Ranges.Length) {
        return OS_ENOTSUPPORTED;
    }
    return OS_EOK;
}

static oserr_t
__EmitMemoryNode(
    _In_ struct __MemoryMapBuilder* context,
    _In_ const struct __NodeFrame*  frame,
    _In_ const struct __NodeFrame*  parent)
{
    const uint8_t* bytes = frame->Reg.Value;
    uint32_t stride;
    int reserved = frame->Type == NodeTypeReservation;
    oserr_t status;

    if (!parent) {
        return OS_EINVALPARAMS;
    }
    if (reserved) {
        if (parent->Type != NodeTypeReservedMemory || context->FrameIndex < 2) {
            return OS_EINVALPARAMS;
        }
        status = __ValidateReservedMemoryNode(parent, &context->Frames[context->FrameIndex - 2]);
        if (status != OS_EOK) {
            return status;
        }
        if (frame->NoMap && frame->Reusable) {
            return OS_EINVALPARAMS;
        }
        // Reserved ownership alone does not express no-map: a later mapper
        // could still create speculative mappings. Do not silently lose that
        // promise until the handoff ABI has a corresponding mapping policy.
        if (frame->NoMap) {
            return OS_ENOTSUPPORTED;
        }
        if (!frame->Reg.Present) {
            // Dynamic size/alignment/alloc-ranges requests require an allocator.
            // Static reg wins when both reg and size are present.
            return frame->Size.Present ? OS_ENOTSUPPORTED : OS_EINVALPARAMS;
        }
        // Reusable ranges remain reserved until an explicit reclamation path
        // exists. Their presence never authorizes immediate general allocation.
    } else if (parent->Type != NodeTypeRoot || !frame->IsMemory) {
        return OS_EINVALPARAMS;
    }

    // A node's own counts describe its children, not its reg encoding. Restrict
    // this consumer to physical ranges representable in the 64-bit VBoot ABI.
    if (parent->AddressCells < 1 || parent->AddressCells > 2 ||
        parent->SizeCells < 1 || parent->SizeCells > 2) {
        return OS_ENOTSUPPORTED;
    }
    stride = (parent->AddressCells + parent->SizeCells) * 4;
    if (!frame->Reg.Present || !bytes || !frame->Reg.Length || frame->Reg.Length % stride) {
        return OS_EINVALPARAMS;
    }

    // Validate every tuple before emitting any of this node's entries. Empty
    // ranges carry no ownership; reject them rather than generating zero-sized
    // map entries. Reject exclusive ends that overflow the map's address type.
    for (uint32_t offset = 0; offset < frame->Reg.Length; offset += stride) {
        uint64_t base = __ReadMemoryCells(bytes + offset, parent->AddressCells);
        uint64_t length = __ReadMemoryCells(bytes + offset + parent->AddressCells * 4, parent->SizeCells);
        if (!length || length > UINT64_MAX - base) {
            return OS_EINVALPARAMS;
        }
    }
    for (uint32_t offset = 0; offset < frame->Reg.Length; offset += stride) {
        uint64_t base = __ReadMemoryCells(bytes + offset, parent->AddressCells);
        uint64_t length = __ReadMemoryCells(bytes + offset + parent->AddressCells * 4, parent->SizeCells);
        status = __InsertMemoryRange(context, base, length, reserved);
        if (status != OS_EOK) {
            return status;
        }
    }
    return OS_EOK;
}

static oserr_t
__ParseMemoryMapEndNode(
    _In_ void* userData)
{
    struct __MemoryMapBuilder* context = userData;
    struct __NodeFrame*        frame;
    struct __NodeFrame*        parent;
    oserr_t                    status = OS_EOK;

    if (!context || !context->Platform) {
        return OS_EINVALPARAMS;
    }

    // Validate frame index
    if (context->FrameIndex < 0 || context->FrameIndex >= __STATIC_FDT_MAX_DEPTH) {
        return OS_EINVALPARAMS;
    }
    
    frame = &context->Frames[context->FrameIndex];
    parent = context->FrameIndex ? &context->Frames[context->FrameIndex - 1] : NULL;

    if (frame->Enabled) {
        switch (frame->Type) {
        case NodeTypeMemory:
        case NodeTypeReservation:
            status = __EmitMemoryNode(context, frame, parent);
            break;
        case NodeTypeReservedMemory:
            status = __ValidateReservedMemoryNode(frame, parent);
            break;
        default:
            break;
        }
    }
    
    if (status != OS_EOK) {
        // Earlier nodes may already have written entries. Zeroing the count
        // prevents a failed completion from exposing that partial map as RAM.
        context->Platform->MemoryMapCount = 0;
        return status;
    }

    // Release borrowed properties when the frame ends. BeginNode also resets
    // the slot, so siblings cannot inherit a missing reg or status property.
    *frame = (struct __NodeFrame){0};
    context->FrameIndex--;
    return OS_EOK;
}

// Indices also track duplicates of properties whose interpretation affects
// memory ownership. Ambiguous values must not be resolved by input order.
static const char* const g_supportedProperties[] = {
    "#address-cells", "#size-cells", "status", "device_type", "reg",
    "ranges", "size", "alignment", "alloc-ranges", "no-map", "reusable",
    
    // Board information relevant to memory map construction
    "compatible", "linux,initrd-start", "linux,initrd-end", "cpu-release-addr"
};

static unsigned int
__GetPropertyIndex(
    _In_ const char* name)
{
    unsigned int property;
    for (property = 0; property < SIZEOF_ARRAY(g_supportedProperties); property++) {
        if (!strcmp(name, g_supportedProperties[property])) {
            break;
        }
    }
    return property;
}

static oserr_t
__ParseProperty(
    _In_ void*       userData,
    _In_ const char* name,
    _In_ const void* value,
    _In_ uint32_t    valueLength)
{
    struct __MemoryMapBuilder* context = userData;
    struct __NodeFrame*        parent;
    struct __NodeFrame*        frame;
    unsigned int               property;
    const uint8_t*             bytes = value;
    
    parent = context->FrameIndex > 0 ? &context->Frames[context->FrameIndex - 1] : NULL;
    frame = &context->Frames[context->FrameIndex];

    property = __GetPropertyIndex(name);
    if (property == SIZEOF_ARRAY(g_supportedProperties)) {
        return OS_EOK;
    }
    
    // Only cell counts/status affect arbitrary nodes. Device-specific bindings
    // can reuse other names; interpreting them as memory metadata is unsafe.
    if ((property == 3 && frame->Type != NodeTypeMemory) ||
        (property == 4 && frame->Type != NodeTypeMemory && frame->Type != NodeTypeReservation) ||
        (property == 5 && frame->Type != NodeTypeReservedMemory) ||
        (property >= 6 && property <= 10 && frame->Type != NodeTypeReservation) ||
        (property == 11 && frame->Type != NodeTypeRoot) ||
        ((property == 12 || property == 13) && frame->Type != NodeTypeChosen) ||
        (property == 14 && frame->Type != NodeTypeCpu)) {
        return OS_EOK;
    }
    
    if (frame->PropertiesRead & ((uint32_t)1 << property)) {
        return OS_EINVALPARAMS;
    }
    
    frame->PropertiesRead |= ((uint32_t)1 << property);
    if ((property == 2 || property == 3) &&
        (!valueLength || memchr(value, 0, valueLength) != (const uint8_t*)value + valueLength - 1)) {
        return OS_EINVALPARAMS;
    }

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
        case 11: { // "compatible"
            uint32_t offset = 0;
            oserr_t  oserr;
            if (!valueLength) {
                return OS_EINVALPARAMS;
            }
            context->CompatibleSeen = 1;

            // Set the status to OS_ENOENT initially
            oserr = OS_ENOENT;

            // compatible is a bounded string list. A substring match could select
            // the wrong platform's MMIO/firmware policy, so compare whole entries.
            while (offset < valueLength) {
                const char* item = (const char*)bytes + offset;
                const char* end = memchr(item, 0, valueLength - offset);
                if (!end || end == item) {
                    return OS_EINVALPARAMS;
                }
                if (!strcmp(item, context->CompatibleBoard)) {
                    oserr = OS_EOK;
                }
                offset += (uint32_t)(end - item) + 1;
            }
            context->CompatibleMatched = oserr == OS_EOK;
            return oserr; // Return the status of the compatible check instead of just breaking
        }
        case 12: // "linux,initrd-start"
        case 13: { // "linux,initrd-end"
            unsigned int bit = property == 12 ? 1 : 2;
            uint64_t address;
            if ((context->InitrdSeen & bit) || (valueLength != 4 && valueLength != 8)) {
                return OS_EINVALPARAMS;
            }
            context->InitrdSeen |= bit;
            address = __ReadBe32(bytes);
            if (valueLength == 8) {
                address = (address << 32) | __ReadBe32(bytes + 4);
            }
            if (bit == 1) {
                context->InitrdStart = address;
            } else {
                context->InitrdEnd = address;
            }
            break;
        }
        case 14: { // "cpu-release-addr"
            uint64_t address;
            if (valueLength != 8) {
                return OS_EINVALPARAMS;
            }
            address = ((uint64_t)__ReadBe32(bytes) << 32) | __ReadBe32(bytes + 4);
            // Reserve even an unused CPU's mailbox. A later CPU-start implementation
            // must retain the firmware spin loop rather than reallocating its data.
            if ((address & 7) || address > UINT64_MAX - 8) {
                return OS_EINVALPARAMS;
            }
            return __InsertMemoryRange(context, address, 8, 1);
        }
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
    _In_ const void* reservationBlock,
    _In_ uint32_t size,
    _In_ struct __MemoryMapBuilder* context)
{
    const uint8_t* bytes = reservationBlock;

    // Reservations have no separate length in the header. The validated gap
    // before the structure block bounds the terminator search; zero RAM beyond
    // that gap must never masquerade as a valid terminator.
    for (uint32_t offset = 0; size - offset >= 16; offset += 16) {
        uint64_t base = __ReadMemoryCells(bytes + offset, 2);
        uint64_t length = __ReadMemoryCells(bytes + offset + 8, 2);
        oserr_t status;
        if (!base && !length) {
            return OS_EOK;
        }
        if (!length || length > UINT64_MAX - base) {
            return OS_EINVALPARAMS;
        }
        status = __InsertMemoryRange(context, base, length, 1);
        if (status != OS_EOK) {
            return status;
        }
    }
    return OS_EINVALPARAMS;
}

oserr_t
DeviceTreeReserveMemory(
    _In_ struct RpiBootContext* context,
    _In_ uint64_t physicalBase,
    _In_ uint64_t length)
{
    struct __MemoryMapBuilder builder = {.Platform = context};
    oserr_t                   status;
    
    if (!context) {
        return OS_EINVALPARAMS;
    }
    
    if (context->MemoryMapCount > RPI_MEMORY_MAP_CAPACITY ||
        !length || length > UINT64_MAX - physicalBase) {
        context->MemoryMapCount = 0;
        return OS_EINVALPARAMS;
    }
    
    status = __InsertMemoryRange(&builder, physicalBase, length, 1);
    if (status != OS_EOK) {
        context->MemoryMapCount = 0;
    }
    return status;
}

oserr_t
DeviceTreeParseEarlyPlatform(
    _In_ const void*            deviceTree,
    _In_ uint32_t               deviceTreeSize,
    _In_ struct RpiBootContext* context)
{
    const uint8_t*            p = deviceTree;
    struct FDTHeader          header;
    oserr_t                   oserr;
    uintptr_t                 tree;
    uintptr_t                 output;
    
    struct __MemoryMapBuilder memoryMapBuilder = {
        .FrameIndex = -1,
        .Platform = context,
    };
    
    struct __ParserContext parser = {
        .BeginNode = __ParseMemoryMapBeginNode,
        .EndNode = __ParseMemoryMapEndNode,
        .Property = __ParseProperty,
        .UserData = &memoryMapBuilder,
    };

    if (!context) {
        return OS_EINVALPARAMS;
    }
    
    // Zero out some members first
    context->MemoryMapCount = 0;
    context->ExternalPayloadBase = 0;
    context->ExternalPayloadLength = 0;
    
    // Verify some of the input
    if (!deviceTree || (context->Board != 4 && context->Board != 5)) {
        return OS_EINVALPARAMS;
    }
    
    memoryMapBuilder.CompatibleBoard = context->Board == 4 ? "brcm,bcm2711" : "brcm,bcm2712";

    // In-place output could overwrite properties that subsequent callbacks
    // still borrow. The entire context must have independent storage for the
    // walk, including the count and external-payload descriptors we publish.
    tree = (uintptr_t)deviceTree;
    output = (uintptr_t)context;
    if (deviceTreeSize > UINTPTR_MAX - tree || sizeof(*context) > UINTPTR_MAX - output ||
        (output < tree + deviceTreeSize && tree < output + sizeof(*context))) {
        return OS_EINVALPARAMS;
    }
    
    oserr = __ParseFDTHeader(deviceTree, deviceTreeSize, &header);
    if (oserr != OS_EOK) {
        return oserr;
    }

    // One structure traversal collects both RAM ownership and Pi boot facts.
    // The separate reservation block is not another traversal of the tree.
    oserr = __ParseStructureBlock(
        p + header.OffDtStruct,
        header.SizeDtStruct,
        (const char*)p + header.OffDtStrings,
        header.SizeDtStrings,
        &parser
    );
    if (oserr != OS_EOK) {
        goto failed;
    }

    oserr = __ParseMemoryReservationBlock(
        p + header.OffMemRsvmap,
        header.OffDtStruct - header.OffMemRsvmap,
        &memoryMapBuilder
    );
    if (oserr != OS_EOK) {
        goto failed;
    }
    
    if (!memoryMapBuilder.CompatibleSeen || !memoryMapBuilder.CompatibleMatched ||
        (memoryMapBuilder.InitrdSeen && memoryMapBuilder.InitrdSeen != 3)) {
        oserr = OS_EINVALPARAMS;
        goto failed;
    }
    
    if (memoryMapBuilder.InitrdSeen) {
        if (memoryMapBuilder.InitrdEnd <= memoryMapBuilder.InitrdStart) {
            oserr = OS_EINVALPARAMS;
            goto failed;
        }
        // These are raw firmware endpoints, not an initialized VBoot ramdisk.
        // Platform preparation checks overlaps and reserves the interval before
        // it publishes the final map or authorizes subsequent PE allocations.
        context->ExternalPayloadBase = memoryMapBuilder.InitrdStart;
        context->ExternalPayloadLength = memoryMapBuilder.InitrdEnd - memoryMapBuilder.InitrdStart;
    }
    return OS_EOK;

failed:
    context->MemoryMapCount = 0;
    return oserr;
}
