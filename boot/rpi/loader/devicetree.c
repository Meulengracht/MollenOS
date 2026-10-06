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
#include <fdt/reader.h>
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

// Borrowed DTB property bytes and their presence, including empty properties.
struct __NodeProperty {
    const void* Value;
    uint32_t    Length;
    int         Present;
};

// Metadata for one active node; its slot is reused after the node closes.
struct __NodeFrame {
    const char*     Name;
    uint32_t        NameLength;
    enum __NodeType Type;
    uint32_t        PropertiesRead; // One duplicate-detection bit per __PropertyIndex.

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

// Traversal frames and boot facts that must survive individual node lifetimes.
struct __MemoryMapBuilder {
    struct __NodeFrame     Frames[FDT_MAX_DEPTH];
    int                    FrameIndex;
    const char*            CompatibleBoard;
    struct RpiBootContext* Platform;

    // Node frames are reused after EndNode. These facts must survive that reuse
    // so final validation does not depend on property or sibling ordering.
    int                   CompatibleSeen;
    int                   CompatibleMatched;
    int                   ChosenSeen;
    unsigned int          InitrdSeen; // Bit 0: start endpoint; bit 1: end endpoint.
    uint64_t              InitrdStart;
    uint64_t              InitrdEnd;
};

static int
__StringEquals(
    _In_ const char* first,
    _In_ const char* second)
{
    return strcmp(first, second) == 0;
}

static int
__NodeNameMatches(
    _In_ const char* name,
    _In_ const char* baseName,
    _In_ size_t      baseLength)
{
    int comparison;

    if (__StringEquals(name, baseName)) {
        return 1;
    }
    comparison = strncmp(name, baseName, baseLength);
    if (comparison != 0) {
        return 0;
    }
    // A unit address is allowed only after the complete base name and '@'.
    return name[baseLength] == '@';
}

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
    if (context->FrameIndex < -1 || context->FrameIndex >= FDT_MAX_DEPTH - 1) {
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
    } else if (parent->Type == NodeTypeCpus && __NodeNameMatches(name, "cpu", sizeof("cpu") - 1)) {
        frame->Type = NodeTypeCpu;
    } else if (parent->Type == NodeTypeRoot) {
        // Nodes that are supported under the Root node
        if (__NodeNameMatches(name, "memory", sizeof("memory") - 1)) {
            frame->Type = NodeTypeMemory;
        } else if (__StringEquals(name, "reserved-memory")) {
            frame->Type = NodeTypeReservedMemory;
        } else if (__StringEquals(name, "chosen")) {
            // A second chosen node would make endpoint ownership ambiguous.
            if (context->ChosenSeen) {
                return OS_EINVALPARAMS;
            }
            context->ChosenSeen = 1;
            frame->Type = NodeTypeChosen;
        } else if (__StringEquals(name, "cpus")) {
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
    uint64_t result = FdtReadBe32(value);
    if (cells == 2) {
        result = (result << 32) | FdtReadBe32(value + 4);
    }
    return result;
}

static int
__MemoryRangesJoinable(
    _In_ const struct VBootMemoryEntry* first,
    _In_ const struct VBootMemoryEntry* second)
{
    // Adjacent ranges may merge only when both ownership and mapping policy agree.
    return first->Type == second->Type && first->Attributes == second->Attributes &&
        first->PhysicalBase + first->Length == second->PhysicalBase;
}

static uint32_t
__CoalesceMemoryRanges(
    _InOut_ struct VBootMemoryEntry* entries,
    _In_    uint32_t                 count)
{
    uint32_t output = 0;

    // Merge only equivalent neighbours; no-map policy must survive merging.
    for (uint32_t i = 0; i < count; i++) {
        if (output && __MemoryRangesJoinable(&entries[output - 1], &entries[i])) {
            entries[output - 1].Length += entries[i].Length;
        } else {
            entries[output++] = entries[i];
        }
    }
    return output;
}

static oserr_t
__InsertMemoryRange(
    _In_ struct __MemoryMapBuilder* context,
    _In_ uint64_t                   base,
    _In_ uint64_t                   length,
    _In_ int                        reserved,
    _In_ uint64_t                   attributes)
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
            .Attributes = attributes
        };

        while (index < count && entries[index].PhysicalBase + entries[index].Length <= cursor) {
            index++;
        }
        if (index == count || cursor < entries[index].PhysicalBase) {
            boundary = index < count && entries[index].PhysicalBase < end ?
                entries[index].PhysicalBase : end;
            // Extending an adjacent entry needs no temporary slot. In
            // particular, contiguous RAM banks must fit a one-entry buffer.
            if (index && __MemoryRangesJoinable(&entries[index - 1], &entry)) {
                entries[index - 1].Length += boundary - cursor;
                cursor = boundary;
                continue;
            }
            // A matching right neighbour can absorb the gap without another map slot.
            if (index < count && boundary == entries[index].PhysicalBase) {
                if (entries[index].Type == entry.Type && entries[index].Attributes == entry.Attributes) {
                    entries[index].Length += boundary - cursor;
                    entries[index].PhysicalBase = cursor;
                    cursor = boundary;
                    continue;
                }
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
        // Free RAM cannot replace a reservation. Existing reservations can also
        // stay unchanged when they already carry all requested policy bits.
        if (!reserved || (original.Type == VBootMemoryType_Reserved &&
            (original.Attributes & attributes) == attributes)) {
            cursor = boundary;
            continue;
        }

        entry.Attributes |= original.Attributes;

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

    context->Platform->MemoryMapCount = __CoalesceMemoryRanges(entries, count);
    return OS_EOK;
}

static oserr_t
__ValidateReservedMemoryNode(
    _In_ const struct __NodeFrame* frame,
    _In_ const struct __NodeFrame* root)
{
    // Reserved-memory describes physical ranges only as a direct child of the root.
    if (!root || root->Type != NodeTypeRoot) {
        return OS_EINVALPARAMS;
    }

    // The first two property bits require explicit address and size counts.
    if ((frame->PropertiesRead & 3) != 3 || !frame->Ranges.Present) {
        return OS_EINVALPARAMS;
    }
    
    // Matching cell counts let the loader use the root's physical address format.
    if (frame->AddressCells != root->AddressCells || frame->SizeCells != root->SizeCells) {
        return OS_EINVALPARAMS;
    }
    
    // Nonempty ranges would need address translation this early parser does not do.
    if (frame->Ranges.Length) {
        return OS_ENOTSUPPORTED;
    }
    return OS_EOK;
}

static oserr_t
__CollectDynamicReservation(
    struct __MemoryMapBuilder* context,
    const struct __NodeFrame* frame,
    const struct __NodeFrame* parent)
{
    struct RpiDynamicReservation* reservation;
    uint64_t length;
    uint64_t alignment = RPI_PAGE_SIZE;
    uint32_t stride = (parent->AddressCells + parent->SizeCells) * 4;

    // At most two address cells fit a physical base in the 64-bit boot ABI.
    if (parent->AddressCells < 1 || parent->AddressCells > 2) {
        return OS_ENOTSUPPORTED;
    }
    
    // The reservation length must also fit that ABI.
    if (parent->SizeCells < 1 || parent->SizeCells > 2) {
        return OS_ENOTSUPPORTED;
    }
    
    // A dynamic request needs exactly one length encoded with the parent's width.
    if (!frame->Size.Present || frame->Size.Length != parent->SizeCells * 4) {
        return OS_EINVALPARAMS;
    }
    
    length = __ReadMemoryCells(frame->Size.Value, parent->SizeCells);
    // Page rounding must neither produce an empty request nor overflow.
    if (!length || length > UINT64_MAX - RPI_PAGE_MASK) {
        return OS_EINVALPARAMS;
    }
    
    if (frame->Alignment.Present) {
        // Decode only a complete alignment value in the inherited cell format.
        if (frame->Alignment.Length != parent->SizeCells * 4) {
            return OS_EINVALPARAMS;
        }
        alignment = __ReadMemoryCells(frame->Alignment.Value, parent->SizeCells);
        // The allocator's bit-mask rounding requires a nonzero power of two.
        if (!alignment || (alignment & (alignment - 1))) {
            return OS_EINVALPARAMS;
        }
        if (alignment < RPI_PAGE_SIZE) {
            alignment = RPI_PAGE_SIZE;
        }
    }
    
    if (frame->AllocRanges.Present) {
        const unsigned char* range = frame->AllocRanges.Value;

        // Allocation limits must contain whole address/length tuples.
        if (!frame->AllocRanges.Length || frame->AllocRanges.Length % stride) {
            return OS_EINVALPARAMS;
        }
        for (uint32_t offset = 0; offset < frame->AllocRanges.Length; offset += stride) {
            uint64_t base = __ReadMemoryCells(range + offset, parent->AddressCells);
            uint64_t size = __ReadMemoryCells(range + offset + parent->AddressCells * 4,
                parent->SizeCells);

            // A limit interval must have a nonempty, nonwrapping end address.
            if (!size || size > UINT64_MAX - base) {
                return OS_EINVALPARAMS;
            }
        }
    }
    
    // Pending requests live in a fixed array until platform storage is reserved.
    if (context->Platform->ReservationCount == RPI_DYNAMIC_RESERVATION_CAPACITY) {
        return OS_EBUFFER;
    }
    
    reservation = &context->Platform->Reservations[context->Platform->ReservationCount++];
    *reservation = (struct RpiDynamicReservation) {
        .InsertBefore = (const unsigned char*)(((uintptr_t)frame->Name + frame->NameLength + 4) & ~3ULL),
        .AllocRanges = frame->AllocRanges.Value,
        .AllocRangesLength = frame->AllocRanges.Length,
        .AddressCells = parent->AddressCells,
        .SizeCells = parent->SizeCells,
        .Length = length,
        .Alignment = alignment,
        .Attributes = frame->NoMap ? VBOOT_MEMORY_NO_MAP : 0
    };
    return OS_EOK;
}

static oserr_t
__EmitMemoryNode(
    _In_ struct __MemoryMapBuilder* context,
    _In_ const struct __NodeFrame*  frame,
    _In_ const struct __NodeFrame*  parent)
{
    const uint8_t* bytes = frame->Reg.Value;
    uint32_t       stride;
    int reserved = frame->Type == NodeTypeReservation;
    oserr_t        status;

    if (!parent) {
        return OS_EINVALPARAMS;
    }
    
    if (reserved) {
        // Only children of the validated reserved-memory container are reservations.
        if (parent->Type != NodeTypeReservedMemory || context->FrameIndex < 2) {
            return OS_EINVALPARAMS;
        }
        
        status = __ValidateReservedMemoryNode(parent, &context->Frames[context->FrameIndex - 2]);
        if (status != OS_EOK) {
            return status;
        }
        
        // A range cannot forbid mappings while also permitting later reuse.
        if (frame->NoMap && frame->Reusable) {
            return OS_EINVALPARAMS;
        }
        
        if (!frame->Reg.Present) {
            // Static reg wins when both reg and size are present.
            return __CollectDynamicReservation(context, frame, parent);
        }
        // Reusable ranges remain reserved until an explicit reclamation path
        // exists. Their presence never authorizes immediate general allocation.
    } else {
        // Only root-level nodes explicitly marked as memory can contribute free RAM.
        if (parent->Type != NodeTypeRoot || !frame->IsMemory) {
            return OS_EINVALPARAMS;
        }
    }

    // A node's own counts describe its children, not its reg encoding. Restrict
    // this consumer to physical ranges representable in the 64-bit VBoot ABI.
    if (parent->AddressCells < 1 || parent->AddressCells > 2) {
        return OS_ENOTSUPPORTED;
    }
    if (parent->SizeCells < 1 || parent->SizeCells > 2) {
        return OS_ENOTSUPPORTED;
    }
    
    stride = (parent->AddressCells + parent->SizeCells) * 4;
    
    // A static node needs nonempty property bytes before tuple validation.
    if (!frame->Reg.Present || !bytes || !frame->Reg.Length) {
        return OS_EINVALPARAMS;
    }
    
    // Partial address/length tuples cannot describe a complete memory range.
    if (frame->Reg.Length % stride) {
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
        // Exclude complete pages from speculative access. The kernel must never
        // identity-map the page containing even a sub-page no-map reservation.
        if (frame->NoMap) {
            if (base + length > UINT64_MAX - RPI_PAGE_MASK) {
                return OS_EINVALPARAMS;
            }
            length = ((base + length + RPI_PAGE_MASK) & ~RPI_PAGE_MASK) - (base & ~RPI_PAGE_MASK);
            base &= ~RPI_PAGE_MASK;
        }
        
        status = __InsertMemoryRange(context, base, length, reserved,
            frame->NoMap ? VBOOT_MEMORY_NO_MAP : 0);
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
    if (context->FrameIndex < 0 || context->FrameIndex >= FDT_MAX_DEPTH) {
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
enum __PropertyIndex {
    PropertyAddressCells,
    PropertySizeCells,
    PropertyStatus,
    PropertyDeviceType,
    PropertyReg,
    PropertyRanges,
    PropertySize,
    PropertyAlignment,
    PropertyAllocRanges,
    PropertyNoMap,
    PropertyReusable,
    PropertyCompatible,
    PropertyInitrdStart,
    PropertyInitrdEnd,
    PropertyCpuRelease,
    PropertyCount
};

static const char* const g_supportedProperties[] = {
    "#address-cells", "#size-cells", "status", "device_type", "reg",
    "ranges", "size", "alignment", "alloc-ranges", "no-map", "reusable",
    
    // Board information relevant to memory map construction
    "compatible", "linux,initrd-start", "linux,initrd-end", "cpu-release-addr"
};

_Static_assert(SIZEOF_ARRAY(g_supportedProperties) == PropertyCount,
    "property names must match their duplicate-detection indices");

static unsigned int
__GetPropertyIndex(
    _In_ const char* name)
{
    unsigned int property;
    for (property = 0; property < SIZEOF_ARRAY(g_supportedProperties); property++) {
        if (__StringEquals(name, g_supportedProperties[property])) {
            break;
        }
    }
    return property;
}

static oserr_t
__ParseBoardCompatibility(
    _InOut_ struct __MemoryMapBuilder* context,
    _In_    const uint8_t*             bytes,
    _In_    uint32_t                   length)
{
    uint32_t offset = 0;
    int matched = 0;

    if (!length) {
        return OS_EINVALPARAMS;
    }
    context->CompatibleSeen = 1;

    // compatible is a bounded string list. A substring match could select
    // the wrong platform policy, so compare complete, nonempty entries.
    while (offset < length) {
        const char* item = (const char*)bytes + offset;
        const char* end = memchr(item, 0, length - offset);

        if (!end || end == item) {
            return OS_EINVALPARAMS;
        }
        if (__StringEquals(item, context->CompatibleBoard)) {
            matched = 1;
        }
        offset += (uint32_t)(end - item) + 1;
    }
    context->CompatibleMatched = matched;
    return matched ? OS_EOK : OS_ENOENT;
}

static int
__PropertyAppliesToNode(
    _In_ unsigned int    property,
    _In_ enum __NodeType type)
{
    // Device bindings can reuse property names without describing boot memory.
    switch (property) {
        case PropertyDeviceType:
            return type == NodeTypeMemory;
        case PropertyReg:
            return type == NodeTypeMemory || type == NodeTypeReservation;
        case PropertyRanges:
            return type == NodeTypeReservedMemory;
        case PropertySize:
        case PropertyAlignment:
        case PropertyAllocRanges:
        case PropertyNoMap:
        case PropertyReusable:
            return type == NodeTypeReservation;
        case PropertyCompatible:
            return type == NodeTypeRoot;
        case PropertyInitrdStart:
        case PropertyInitrdEnd:
            return type == NodeTypeChosen;
        case PropertyCpuRelease:
            return type == NodeTypeCpu;
        default:
            return 1;
    }
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
    const uint8_t*             terminator;
    
    parent = context->FrameIndex > 0 ? &context->Frames[context->FrameIndex - 1] : NULL;
    frame = &context->Frames[context->FrameIndex];

    property = __GetPropertyIndex(name);
    if (property == SIZEOF_ARRAY(g_supportedProperties)) {
        return OS_EOK;
    }
    
    // Only cell counts/status affect arbitrary nodes. Device-specific bindings
    // can reuse other names; interpreting them as memory metadata is unsafe.
    if (!__PropertyAppliesToNode(property, frame->Type)) {
        return OS_EOK;
    }
    
    // Repeated ownership properties are ambiguous; input order must not pick a winner.
    if (frame->PropertiesRead & ((uint32_t)1 << property)) {
        return OS_EINVALPARAMS;
    }
    
    frame->PropertiesRead |= ((uint32_t)1 << property);
    // These bindings require one complete string, not a string list or raw bytes.
    if (property == PropertyStatus || property == PropertyDeviceType) {
        if (!valueLength) {
            return OS_EINVALPARAMS;
        }
        terminator = memchr(value, 0, valueLength);
        // The first NUL must be the last byte, keeping later comparisons bounded.
        if (terminator != bytes + valueLength - 1) {
            return OS_EINVALPARAMS;
        }
    }

    switch (property) {
        case PropertyAddressCells:
            // Cell-count properties are encoded as exactly one 32-bit cell.
            if (valueLength != 4) {
                return OS_EINVALPARAMS;
            }
            frame->AddressCells = FdtReadBe32(value);
            break;
        case PropertySizeCells:
            // The inherited size format also needs exactly one count cell.
            if (valueLength != 4) {
                return OS_EINVALPARAMS;
            }
            frame->SizeCells = FdtReadBe32(value);
            break;
        case PropertyStatus:
            if (parent == NULL) {
                frame->Enabled = (__StringEquals(value, "okay") || __StringEquals(value, "ok"));
            } else {
                frame->Enabled = parent->Enabled && (__StringEquals(value, "okay") || __StringEquals(value, "ok"));
            }
            break;
        case PropertyDeviceType:
            frame->IsMemory = __StringEquals(value, "memory");
            break;
        case PropertyReg:
            frame->Reg.Value = value;
            frame->Reg.Length = valueLength;
            frame->Reg.Present = 1;
            break;
        case PropertyRanges:
            frame->Ranges.Value = value;
            frame->Ranges.Length = valueLength;
            frame->Ranges.Present = 1;
            break;
        case PropertySize:
            frame->Size.Value = value;
            frame->Size.Length = valueLength;
            frame->Size.Present = 1;
            break;
        case PropertyAlignment:
            frame->Alignment.Value = value;
            frame->Alignment.Length = valueLength;
            frame->Alignment.Present = 1;
            break;
        case PropertyAllocRanges:
            frame->AllocRanges.Value = value;
            frame->AllocRanges.Length = valueLength;
            frame->AllocRanges.Present = 1;
            break;
        case PropertyNoMap:
            // Boolean DT properties signal presence and must not carry data bytes.
            if (valueLength) {
                return OS_EINVALPARAMS;
            }
            frame->NoMap = 1;
            break;
        case PropertyReusable:
            // Reusable is also a presence-only property, not an integer value.
            if (valueLength) {
                return OS_EINVALPARAMS;
            }
            frame->Reusable = 1;
            break;
        case PropertyCompatible:
            return __ParseBoardCompatibility(context, bytes, valueLength);
        case PropertyInitrdStart:
        case PropertyInitrdEnd: {
            unsigned int bit = property == PropertyInitrdStart ? 1 : 2;
            uint64_t     address;
            // Firmware endpoints must be unique and encoded in 32 or 64 bits.
            if ((context->InitrdSeen & bit) || (valueLength != 4 && valueLength != 8)) {
                return OS_EINVALPARAMS;
            }
            
            context->InitrdSeen |= bit;
            address = FdtReadBe32(bytes);
            if (valueLength == 8) {
                address = (address << 32) | FdtReadBe32(bytes + 4);
            }
            if (bit == 1) {
                context->InitrdStart = address;
            } else {
                context->InitrdEnd = address;
            }
            break;
        }
        case PropertyCpuRelease: {
            uint64_t address;
            
            // Spin-table mailboxes carry one complete 64-bit physical address.
            if (valueLength != 8) {
                return OS_EINVALPARAMS;
            }
            
            address = ((uint64_t)FdtReadBe32(bytes) << 32) | FdtReadBe32(bytes + 4);
            // Reserve even an unused CPU's mailbox. A later CPU-start implementation
            // must retain the firmware spin loop rather than reallocating its data.
            if ((address & 7) || address > UINT64_MAX - 8) {
                return OS_EINVALPARAMS;
            }
            return __InsertMemoryRange(context, address, 8, 1, 0);
        }
        default:
            // Unknown property
            break;
    }
    
    // A reservation cannot prohibit mappings and simultaneously permit reuse.
    if (frame->NoMap && frame->Reusable) {
        return OS_EINVALPARAMS;
    }

    return OS_EOK;
}

static oserr_t
__ParseMemoryReservationBlock(
    _In_ const void*                reservationBlock,
    _In_ uint32_t                   size,
    _In_ struct __MemoryMapBuilder* context)
{
    const uint8_t* bytes = reservationBlock;

    // Reservations have no separate length in the header. The validated gap
    // before the structure block bounds the terminator search; zero RAM beyond
    // that gap must never masquerade as a valid terminator.
    for (uint32_t offset = 0; size - offset >= 16; offset += 16) {
        uint64_t base = __ReadMemoryCells(bytes + offset, 2);
        uint64_t length = __ReadMemoryCells(bytes + offset + 8, 2);
        oserr_t  status;
        if (!base && !length) {
            return OS_EOK;
        }
        
        // Non-terminator entries must describe nonempty ranges without wrapping.
        if (!length || length > UINT64_MAX - base) {
            return OS_EINVALPARAMS;
        }
        
        status = __InsertMemoryRange(context, base, length, 1, 0);
        if (status != OS_EOK) {
            return status;
        }
    }
    return OS_EINVALPARAMS;
}

oserr_t
DeviceTreeReserveMemoryWithAttributes(
    _In_ struct RpiBootContext* context,
    _In_ uint64_t               physicalBase,
    _In_ uint64_t               length,
    _In_ uint64_t               attributes)
{
    struct __MemoryMapBuilder builder = {.Platform = context};
    oserr_t                   status;
    
    if (!context) {
        return OS_EINVALPARAMS;
    }
    
    // Reject malformed ranges before insertion can index or change the ownership map.
    if (context->MemoryMapCount > RPI_MEMORY_MAP_CAPACITY ||
        !length || length > UINT64_MAX - physicalBase) {
        context->MemoryMapCount = 0;
        return OS_EINVALPARAMS;
    }
    
    status = __InsertMemoryRange(&builder, physicalBase, length, 1, attributes);
    if (status != OS_EOK) {
        context->MemoryMapCount = 0;
    }
    return status;
}

oserr_t
DeviceTreeReserveMemory(
    _In_ struct RpiBootContext* context,
    _In_ uint64_t               physicalBase,
    _In_ uint64_t               length)
{
    return DeviceTreeReserveMemoryWithAttributes(
        context,
        physicalBase,
        length,
        0
    );
}

oserr_t
DeviceTreeParseEarlyPlatform(
    _In_ const void*            deviceTree,
    _In_ uint32_t               deviceTreeSize,
    _In_ struct RpiBootContext* context)
{
    const uint8_t*   p = deviceTree;
    struct FDTHeader header;
    oserr_t          oserr;
    uintptr_t        tree;
    uintptr_t        output;
    
    struct __MemoryMapBuilder memoryMapBuilder = {
        .FrameIndex = -1,
        .Platform = context,
    };
    
    struct FdtParser parser = {
        .BeginNode = __ParseMemoryMapBeginNode,
        .EndNode = __ParseMemoryMapEndNode,
        .Property = __ParseProperty,
        .UserData = &memoryMapBuilder,
    };

    // Zero out some members first
    context->MemoryMapCount = 0;
    context->ReservationCount = 0;
    context->ExternalPayloadBase = 0;
    context->ExternalPayloadLength = 0;
    
    if (!deviceTree) {
        return OS_EINVALPARAMS;
    }
    // Root compatibility is defined only for the two supported board families.
    if (context->Board != 4 && context->Board != 5) {
        return OS_EINVALPARAMS;
    }
    
    memoryMapBuilder.CompatibleBoard = context->Board == 4 ? "brcm,bcm2711" : "brcm,bcm2712";

    // In-place output could overwrite properties that subsequent callbacks
    // still borrow. The entire context must have independent storage for the
    // walk, including the count and external-payload descriptors we publish.
    tree = (uintptr_t)deviceTree;
    output = (uintptr_t)context;
    // Establish representable end addresses before comparing the two storage ranges.
    if (deviceTreeSize > UINTPTR_MAX - tree || sizeof(*context) > UINTPTR_MAX - output) {
        return OS_EINVALPARAMS;
    }
    if (output < tree + deviceTreeSize && tree < output + sizeof(*context)) {
        return OS_EINVALPARAMS;
    }
    
    oserr = FdtParseHeader(deviceTree, deviceTreeSize, &header);
    if (oserr != OS_EOK) {
        return oserr;
    }

    // One structure traversal collects both RAM ownership and Pi boot facts.
    // The separate reservation block is not another traversal of the tree.
    oserr = FdtParseStructure(
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
    
    // No boot facts can be published without a matching root compatibility entry.
    if (!memoryMapBuilder.CompatibleSeen || !memoryMapBuilder.CompatibleMatched) {
        oserr = OS_EINVALPARAMS;
        goto failed;
    }

    // An initrd interval requires both endpoints; a single endpoint is not ownership.
    if (memoryMapBuilder.InitrdSeen && memoryMapBuilder.InitrdSeen != 3) {
        oserr = OS_EINVALPARAMS;
        goto failed;
    }
    
    if (memoryMapBuilder.InitrdSeen) {
        // Compare endpoints before subtraction so the transport length cannot wrap.
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
