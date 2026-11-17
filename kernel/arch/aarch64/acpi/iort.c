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
 */

#include <ddk/types/interrupt.h>
#include <heap.h>
#include <debug.h>
#include <string.h>

#include "tables.h"
#include "iort-private.h"

// Fixed byte sizes for each node record, stopping before any variable-length
// lists or strings at the end. ACPICA places a 1-byte array placeholder at the
// end of these structs, so sizeof() would include extra bytes that belong to
// the next record. Under Arm's IORT specification (revisions B through E.d),
// every node header is 16 bytes and each ID mapping entry is 20 bytes. Revision
// 4 of the PCI root node adds 4 bytes beyond revision 3, which QEMU 11.1 emits.
#define IORT_NODE_HEADER_SIZE offsetof(ACPI_IORT_NODE, NodeData)
#define IORT_SMMU_FIXED_SIZE  (IORT_NODE_HEADER_SIZE + offsetof(ACPI_IORT_SMMU, Interrupts))
#define IORT_NAMED_NAME       (IORT_NODE_HEADER_SIZE + offsetof(ACPI_IORT_NAMED_COMPONENT, DeviceName))

// An SMMUv3 uses two 64 KiB register pages (128 KiB total), and the IORT node
// gives only the base address. Reserved memory ranges must align to 64 KiB.
#define IORT_V3_REGISTER_SIZE 0x20000ULL
#define IORT_RMR_GRANULARITY  0x10000ULL

struct __IortNodeRange {
    uint32_t Offset;
    uint32_t Length;
};

// Temporary parse record pointing directly into the raw ACPI table memory.
// ArmIortCreate copies out everything callers need before handing the table
// back to ACPICA.
struct __IortNode {
    // Node header inside the raw table.
    const ACPI_IORT_NODE* Header;
    // Byte position of the node from the start of the table.
    uint32_t             Offset;
    // Index of this node's first mapping in __IortParser.Mappings.
    uint32_t             FirstMapping;
    // Size in bytes of the fixed part of the node. Lists and strings follow it.
    uint32_t             FixedLength;
    // Byte ranges inside the node already used by mappings, names or lists.
    // They are tracked so that no two of them can overlap.
    uint32_t               UsedRangeCount;
    struct __IortNodeRange UsedRanges[4];
    // Used by __CheckGraph. Set once all nodes this node maps to are resolved.
    bool                 Resolved;
    // OS_EOK if the node uses only features we support. Only filled in for SMMUs.
    oserr_t              Status;
    // Register range of an SMMU, used to detect overlapping SMMUs.
    uint64_t             RegisterBase;
    uint64_t             RegisterLength;
};

// One ID mapping read from the table, with the node it points to resolved to
// an index in __IortParser.Nodes.
struct __IortMapping {
    ACPI_IORT_ID_MAPPING Data;
    uint32_t             Target;
    bool                 MapsAllInputsToOne;
    bool                 IsOwnInterruptMapping;
};

// Working state while the table is checked. The arrays are sized by the node
// and mapping limits from iort-private.h.
struct __IortParser {
    const ACPI_TABLE_IORT* Table;
    uint32_t               NodeCount;
    uint32_t               MappingCount;
    // Reserved memory ranges seen so far across all RMR nodes.
    uint32_t               RmrDescriptorCount;
    struct __IortNode      Nodes[ARM_IORT_MAX_NODES];
    struct __IortMapping   Mappings[ARM_IORT_MAX_RANGES];
};

// Returns true if the two inclusive ranges share at least one value.
static bool
__Overlaps(
    _In_ uint64_t first,
    _In_ uint64_t last,
    _In_ uint64_t otherFirst,
    _In_ uint64_t otherLast)
{
    return first <= otherLast && otherFirst <= last;
}

// Records a variable-length range (such as a mapping array or device name)
// inside a node and checks that it stays within the node bounds without
// overlapping the fixed fields or another range.
static oserr_t
__AddNodeRange(
    _InOut_ struct __IortNode* node,
    _In_    uint32_t           offset,
    _In_    uint32_t           count,
    _In_    uint32_t           entrySize)
{
    uint64_t length = (uint64_t)count * entrySize;

    if (count == 0) {
        return offset == 0 || (offset >= node->FixedLength && offset <= node->Header->Length)
            ? OS_EOK : OS_EINVALPARAMS;
    }
    if (offset < node->FixedLength || offset > node->Header->Length ||
        length > node->Header->Length - offset) {
        return OS_EINVALPARAMS;
    }

    for (uint32_t i = 0; i < node->UsedRangeCount; i++) {
        const struct __IortNodeRange* range = &node->UsedRanges[i];

        if (__Overlaps(offset, offset + length - 1,
                       range->Offset, range->Offset + range->Length - 1)) {
            return OS_EINVALPARAMS;
        }
    }
    if (node->UsedRangeCount == SIZEOF_ARRAY(node->UsedRanges)) {
        return OS_EINVALPARAMS;
    }

    node->UsedRanges[node->UsedRangeCount++] = (struct __IortNodeRange){
        offset,
        (uint32_t)length
    };
    return OS_EOK;
}

// Sets FixedLength from the node type and revision, and rejects revisions we
// do not know. FixedLength is the size in bytes of the fixed part of the node
// for that revision, including the 16-byte node header, as listed in the IORT
// specification.
static oserr_t
__SetLayout(
    _InOut_ struct __IortNode* node)
{
    uint8_t revision = node->Header->Revision;

    // Reject node revisions this parser does not recognize. Reading a newer
    // layout as an older one could silently miss fields that alter routing.
    switch (node->Header->Type) {
        case ACPI_IORT_NODE_ITS_GROUP:
            if (revision > 1) {
                return OS_ENOTSUPPORTED;
            }
            node->FixedLength = 20;
            break;
        case ACPI_IORT_NODE_NAMED_COMPONENT:
            if (revision > 4) {
                return OS_ENOTSUPPORTED;
            }
            node->FixedLength = IORT_NAMED_NAME;
            break;
        case ACPI_IORT_NODE_PCI_ROOT_COMPLEX:
            if (revision > 4) {
                return OS_ENOTSUPPORTED;
            }
            node->FixedLength = revision == 0 ? 32 : (revision == 4 ? 40 : 36);
            break;
        case ACPI_IORT_NODE_SMMU:
            if (revision > 3) {
                return OS_ENOTSUPPORTED;
            }
            node->FixedLength = IORT_SMMU_FIXED_SIZE;
            break;
        case ACPI_IORT_NODE_SMMU_V3:
            if (revision > 4) {
                return OS_ENOTSUPPORTED;
            }
            node->FixedLength = revision == 0 ? 60 : 68;
            break;
        case ACPI_IORT_NODE_PMCG:
            if (revision > 2) {
                return OS_ENOTSUPPORTED;
            }
            node->FixedLength = revision == 0 ? 32 : 40;
            break;
        case ACPI_IORT_NODE_RMR:
            // Only revision 3 of Reserved Memory Range (RMR) nodes is accepted,
            // since earlier drafts arranged the fields differently.
            if (revision != 3) {
                return OS_ENOTSUPPORTED;
            }
            node->FixedLength = 28;
            break;
        default:
            return OS_ENOTSUPPORTED;
    }
    return node->Header->Length >= node->FixedLength ? OS_EOK : OS_EINVALPARAMS;
}

// Table revision 5 requires every node identifier to be unique. Revision 0
// does not use the Identifier field, so nothing is checked there.
static oserr_t
__CheckUniqueIdentifier(
    _In_ const struct __IortParser* parser,
    _In_ uint32_t                   index)
{
    const struct __IortNode* node = &parser->Nodes[index];

    if (parser->Table->Header.Revision != 5) {
        return OS_EOK;
    }

    for (uint32_t i = 0; i < index; i++) {
        if (node->Header->Identifier == parser->Nodes[i].Header->Identifier) {
            return OS_EINVALPARAMS;
        }
    }
    return OS_EOK;
}

// Checks the node that starts at the given table offset, remembers where it
// is, and reserves room for its mappings in the shared mapping array. Only the
// node's own bytes are checked here. Mapping contents are read later, once
// every node's position is known.
static oserr_t
__IndexNode(
    _InOut_ struct __IortParser* parser,
    _In_    uint32_t             index,
    _In_    uint32_t             offset)
{
    uint32_t           tableLength = parser->Table->Header.Length;
    struct __IortNode* node = &parser->Nodes[index];
    oserr_t            status;

    if (tableLength - offset < IORT_NODE_HEADER_SIZE) {
        return OS_EINVALPARAMS;
    }

    node->Header = (const void*)((const uint8_t*)parser->Table + offset);
    node->Offset = offset;
    node->Status = OS_ENOTSUPPORTED;
    if (node->Header->Length < IORT_NODE_HEADER_SIZE || node->Header->Length > tableLength - offset) {
        return OS_EINVALPARAMS;
    }

    status = __SetLayout(node);
    if (status != OS_EOK) {
        ERROR("IORT: unsupported or truncated node at %u, status %u", offset, status);
        return status;
    }

    status = __AddNodeRange(
        node,
        node->Header->MappingOffset,
        node->Header->MappingCount,
        sizeof(ACPI_IORT_ID_MAPPING)
    );
    if (status != OS_EOK) {
        return status;
    }

    status = __CheckUniqueIdentifier(parser, index);
    if (status != OS_EOK) {
        return status;
    }

    if (node->Header->MappingCount > ARM_IORT_MAX_RANGES - parser->MappingCount) {
        return OS_ENOTSUPPORTED;
    }

    node->FirstMapping = parser->MappingCount;
    parser->MappingCount += node->Header->MappingCount;
    return OS_EOK;
}

// First pass over the table. Records every node's position and size, so that
// mappings can refer to nodes by their table offset in later passes.
static oserr_t
__IndexNodes(
    _InOut_ struct __IortParser* parser)
{
    uint32_t offset = parser->Table->NodeOffset;
    uint32_t length = parser->Table->Header.Length;
    oserr_t  status;

    if (parser->Table->Reserved != 0 || offset < sizeof(ACPI_TABLE_IORT) || offset > length) {
        return OS_EINVALPARAMS;
    }

    if (parser->Table->NodeCount > ARM_IORT_MAX_NODES) {
        return OS_ENOTSUPPORTED;
    }

    parser->NodeCount = parser->Table->NodeCount;
    for (uint32_t i = 0; i < parser->NodeCount; i++) {
        status = __IndexNode(parser, i, offset);
        if (status != OS_EOK) {
            return status;
        }
        offset += parser->Nodes[i].Header->Length;
    }

    // The walked nodes must consume the exact table length. Leaving trailing
    // bytes unchecked could hide a node or reserved memory range.
    return offset == length ? OS_EOK : OS_EINVALPARAMS;
}

// Looks up a node by its byte position in the table, which is how mappings and
// PMCG nodes refer to other nodes. The position has to match the start of a
// node exactly.
static oserr_t
__FindNode(
    _In_  const struct __IortParser* parser,
    _In_  uint32_t                   offset,
    _Out_ uint32_t*                  indexOut)
{
    for (uint32_t i = 0; i < parser->NodeCount; i++) {
        if (parser->Nodes[i].Offset == offset) {
            *indexOut = i;
            return OS_EOK;
        }
    }
    return OS_EINVALPARAMS;
}

// Finds which mapping of an SMMUv3 node carries the SMMU's own interrupt
// messages. The result is UINT32_MAX when the node is not an SMMUv3 or when
// none of its mappings is used for that purpose.
static oserr_t
__FindControlMapping(
    _In_  const struct __IortNode* node,
    _Out_ uint32_t*                controlMappingOut)
{
    const ACPI_IORT_SMMU_V3* smmu = (const void*)node->Header->NodeData;

    *controlMappingOut = UINT32_MAX;
    if (node->Header->Type != ACPI_IORT_NODE_SMMU_V3 ||
        node->Header->Revision < 1 || node->Header->MappingCount == 0) {
        return OS_EOK;
    }

    // A GSIV (interrupt line number) of 0 means the SMMUv3 sends that
    // event as a message-signaled interrupt instead of on a wire.
    // IdMappingIndex picks the mapping entry used for those messages,
    // and it applies regardless of whether SINGLE_MAPPING is set.
    if (smmu->EventGsiv == 0 || smmu->PriGsiv == 0 || smmu->GerrGsiv == 0 || smmu->SyncGsiv == 0) {
        if (smmu->IdMappingIndex >= node->Header->MappingCount) {
            return OS_EINVALPARAMS;
        }
        *controlMappingOut = smmu->IdMappingIndex;
    }
    return OS_EOK;
}

// Only these node types may use SINGLE_MAPPING, which makes every input ID
// map to one output ID.
static bool
__SupportsSingleMapping(
    _In_ uint8_t nodeType)
{
    return nodeType == ACPI_IORT_NODE_NAMED_COMPONENT ||
           nodeType == ACPI_IORT_NODE_PCI_ROOT_COMPLEX ||
           nodeType == ACPI_IORT_NODE_SMMU_V3 ||
           nodeType == ACPI_IORT_NODE_PMCG;
}

// Checks that the ID ranges of a non-single mapping do not wrap around the
// 32-bit (or, for PCI roots, 16-bit) ID space.
static oserr_t
__CheckMappingRange(
    _In_ const struct __IortNode*    node,
    _In_ const ACPI_IORT_ID_MAPPING* entry)
{
    // Mappings with MapsAllInputsToOne ignore InputBase and IdCount. RMR entries ignore
    // InputBase, using IdCount only for the output range size. Because
    // IdCount is (count - 1), the last output ID is OutputBase + IdCount.
    // Guard against 32-bit wrap-around on that addition.
    if ((uint64_t)entry->OutputBase + entry->IdCount > UINT32_MAX ||
        (node->Header->Type != ACPI_IORT_NODE_RMR &&
         (uint64_t)entry->InputBase + entry->IdCount > UINT32_MAX)) {
        return OS_EINVALPARAMS;
    }

    if (node->Header->Type == ACPI_IORT_NODE_PCI_ROOT_COMPLEX &&
        (uint64_t)entry->InputBase + entry->IdCount > UINT16_MAX) {
        return OS_EINVALPARAMS;
    }
    return OS_EOK;
}

// Reads one ID mapping entry of a node, checks it, and resolves the node it
// points to. A mapping used for interrupt messages of the node itself (the
// control mapping) must point to an ITS group.
static oserr_t
__ReadMapping(
    _InOut_ struct __IortParser* parser,
    _In_    const struct __IortNode* node,
    _In_    uint32_t             index,
    _In_    uint32_t             controlMapping)
{
    const uint8_t*        bytes = (const void*)node->Header;
    struct __IortMapping* mapping = &parser->Mappings[node->FirstMapping + index];
    ACPI_IORT_ID_MAPPING* entry = &mapping->Data;
    bool                  single;
    bool                  control;
    oserr_t               status;

    memcpy(
        entry,
        bytes + node->Header->MappingOffset + index * sizeof(*entry),
        sizeof(*entry)
    );
    if (entry->Flags & ~ACPI_IORT_ID_SINGLE_MAPPING) {
        return OS_EINVALPARAMS;
    }

    single = (entry->Flags & ACPI_IORT_ID_SINGLE_MAPPING) != 0;
    if (single && !__SupportsSingleMapping(node->Header->Type)) {
        return OS_EINVALPARAMS;
    }

    // A PMCG (performance counter block) has at most one mapping for its
    // own interrupt messages. It ignores input IDs even if firmware
    // forgot to set SINGLE_MAPPING.
    control = node->Header->Type == ACPI_IORT_NODE_PMCG || index == controlMapping;
    if (control) {
        single = true;
    }

    mapping->MapsAllInputsToOne = single;
    mapping->IsOwnInterruptMapping = control;

    if (!single) {
        status = __CheckMappingRange(node, entry);
        if (status != OS_EOK) {
            return status;
        }
    }

    status = __FindNode(
        parser,
        entry->OutputReference,
        &mapping->Target
    );
    if (status != OS_EOK) {
        return status;
    }

    if (control && parser->Nodes[mapping->Target].Header->Type != ACPI_IORT_NODE_ITS_GROUP) {
        return OS_EINVALPARAMS;
    }
    return OS_EOK;
}

// Second pass over the table. Reads every node's ID mappings now that all node
// positions are known, so each mapping can be resolved to a node index.
static oserr_t
__ReadMappings(
    _InOut_ struct __IortParser* parser)
{
    for (uint32_t i = 0; i < parser->NodeCount; i++) {
        const struct __IortNode* node = &parser->Nodes[i];
        uint32_t                 controlMapping;
        oserr_t                  status;

        status = __FindControlMapping(node, &controlMapping);
        if (status != OS_EOK) {
            return status;
        }

        for (uint32_t j = 0; j < node->Header->MappingCount; j++) {
            status = __ReadMapping(parser, node, j, controlMapping);
            if (status != OS_EOK) {
                return status;
            }
        }
    }
    return OS_EOK;
}

// Third pass over the table. Verifies that following ID mappings from any node
// never leads back to the same node.
static oserr_t
__CheckGraph(
    _InOut_ struct __IortParser* parser)
{
    uint32_t remaining = parser->NodeCount;

    // Mark a node done once every node it points to is already done. If a full
    // pass makes no progress, the remaining nodes form a cycle. Doing this
    // iteratively avoids recursion so a malformed table cannot overflow the
    // kernel stack.
    while (remaining != 0) {
        uint32_t before = remaining;

        for (uint32_t i = 0; i < parser->NodeCount; i++) {
            struct __IortNode* node = &parser->Nodes[i];
            bool              ready = true;

            if (node->Resolved) {
                continue;
            }

            for (uint32_t j = 0; j < node->Header->MappingCount; j++) {
                uint32_t target = parser->Mappings[node->FirstMapping + j].Target;

                if (!parser->Nodes[target].Resolved) {
                    ready = false;
                    break;
                }
            }

            if (ready) {
                node->Resolved = true;
                remaining--;
            }
        }
        
        if (remaining == before) {
            return OS_EINVALPARAMS;
        }
    }
    return OS_EOK;
}

// Converts an SMMU interrupt from the table into our interrupt description. A
// line of 0 means the interrupt is not wired and is left unused (UINT32_MAX).
static oserr_t
__ReadInterrupt(
    _In_  uint32_t            line,
    _In_  uint32_t            flags,
    _Out_ ArmSMMUInterrupt_t* interrupt)
{
    if (flags & ~1U) {
        return OS_EINVALPARAMS;
    }
    
    interrupt->Line = line == 0 ? UINT32_MAX : line;
    interrupt->Flags = 0;
    
    if (line == 0) {
        return OS_EOK;
    }

    // Wired SMMU interrupts must be GIC Shared Peripheral Interrupts (SPIs),
    // which occupy global IDs 32 through 1019.
    if (line < ARM64_GIC_SPI_BASE || line >= ARM64_GIC_SPECIAL_ID_BASE) {
        return OS_ENOTSUPPORTED;
    }

    // IORT uses bit 0 set for edge-triggered (fires on signal transition),
    // while our kernel flags set INTERRUPT_ACPICONFORM_TRIGGERMODE for
    // level-triggered (stays active while asserted). Flip the meaning here.
    interrupt->Flags = INTERRUPT_ACPICONFORM_PRESENT;
    if (!(flags & 1U)) {
        interrupt->Flags |= INTERRUPT_ACPICONFORM_TRIGGERMODE;
    }
    return OS_EOK;
}

// Reads an SMMU node of the older type (SMMUv1 or SMMUv2). Only the Arm
// MMU-500 model is supported. Its register range and interrupt lists are
// checked against the node bounds before any interrupt is read.
static oserr_t
__ReadSMMU500(
    _InOut_ struct __IortNode*    node,
    _Out_   ArmSMMUDescription_t* description)
{
    const ACPI_IORT_SMMU* smmu = (const void*)node->Header->NodeData;
    const uint8_t*        bytes = (const void*)node->Header;
    ACPI_IORT_SMMU_GSI    global;
    oserr_t               status;

    node->RegisterBase = smmu->BaseAddress;
    node->RegisterLength = smmu->Span;

    status = __AddNodeRange(
        node,
        smmu->GlobalInterruptOffset,
        1,
        sizeof(global)
    );
    if (status != OS_EOK) {
        return status;
    }

    status = __AddNodeRange(
        node,
        smmu->ContextInterruptOffset,
        smmu->ContextInterruptCount,
        sizeof(uint64_t)
    );
    if (status != OS_EOK) {
        return status;
    }

    status = __AddNodeRange(
        node,
        smmu->PmuInterruptOffset,
        smmu->PmuInterruptCount,
        sizeof(uint64_t)
    );
    if (status != OS_EOK) {
        return status;
    }

    if (smmu->Model != ACPI_IORT_SMMU_CORELINK_MMU500 ||
        smmu->ContextInterruptCount == 0 || smmu->ContextInterruptCount > 128 ||
        (smmu->Flags & ~(ACPI_IORT_SMMU_DVM_SUPPORTED | ACPI_IORT_SMMU_COHERENT_WALK))) {
        return OS_ENOTSUPPORTED;
    }

    description->Kind = ArmSMMU500;
    description->TableCoherent = (smmu->Flags & ACPI_IORT_SMMU_COHERENT_WALK) != 0;
    description->Mmu500.ContextInterruptCount = smmu->ContextInterruptCount;
    memcpy(&global, bytes + smmu->GlobalInterruptOffset, sizeof(global));
    
    status = __ReadInterrupt(
        global.NSgIrpt,
        global.NSgIrptFlags,
        &description->Mmu500.GlobalFault
    );
    if (status != OS_EOK) {
        return status;
    }

    status = __ReadInterrupt(
        global.NSgCfgIrpt,
        global.NSgCfgIrptFlags,
        &description->Mmu500.ConfigurationFault
    );
    if (status != OS_EOK) {
        return status;
    }

    for (uint32_t i = 0; i < smmu->ContextInterruptCount; i++) {
        uint32_t pair[2];

        memcpy(pair, bytes + smmu->ContextInterruptOffset + i * sizeof(pair), sizeof(pair));
        status = __ReadInterrupt(
            pair[0],
            pair[1],
            &description->Mmu500.ContextInterrupts[i]
        );
        if (status != OS_EOK) {
            return status;
        }
    }
    return OS_EOK;
}

// Reads an SMMUv3 node. Only the generic SMMUv3 model is supported.
static oserr_t
__ReadSMMUv3(
    _InOut_ struct __IortNode*    node,
    _Out_   ArmSMMUDescription_t* description)
{
    const ACPI_IORT_SMMU_V3* smmu = (const void*)node->Header->NodeData;
    oserr_t                  status;

    node->RegisterBase = smmu->BaseAddress;
    node->RegisterLength = IORT_V3_REGISTER_SIZE;

    if (smmu->Reserved != 0) {
        return OS_EINVALPARAMS;
    }

    if (smmu->Model != ACPI_IORT_SMMU_V3_GENERIC || (smmu->Flags & ~0xfU)) {
        return OS_ENOTSUPPORTED;
    }

    description->Kind = ArmSMMUv3;
    description->TableCoherent = (smmu->Flags & ACPI_IORT_SMMU_V3_COHACC_OVERRIDE) != 0;
    
    // SMMUv3 event and global error interrupts always fire on a signal edge.
    // The IORT table omits a trigger flag for them, so pass 1 (edge-triggered).
    status = __ReadInterrupt(
        smmu->EventGsiv,
        1,
        &description->V3.EventQueue
    );
    if (status != OS_EOK) {
        return status;
    }

    status = __ReadInterrupt(
        smmu->GerrGsiv,
        1,
        &description->V3.GlobalError
    );
    if (status != OS_EOK) {
        return status;
    }
    return OS_EOK;
}

// Checks that the register range of the SMMU at the given index is page
// aligned, does not wrap around the address space, and does not overlap the
// registers of any SMMU earlier in the table.
static oserr_t
__CheckRegisterRange(
    _In_ const struct __IortParser* parser,
    _In_ uint32_t                   index)
{
    const struct __IortNode* node = &parser->Nodes[index];

    if (node->RegisterLength == 0 || node->RegisterBase == 0 ||
        node->RegisterLength - 1 > UINT64_MAX - node->RegisterBase ||
        (node->RegisterBase & 0xfff) || (node->RegisterLength & 0xfff)) {
        return OS_EINVALPARAMS;
    }

    for (uint32_t i = 0; i < index; i++) {
        const struct __IortNode* other = &parser->Nodes[i];

        if (other->RegisterLength != 0 && __Overlaps(node->RegisterBase,
            node->RegisterBase + node->RegisterLength - 1, other->RegisterBase,
            other->RegisterBase + other->RegisterLength - 1)) {
            return OS_EINVALPARAMS;
        }
    }
    return OS_EOK;
}

// Any ID mappings on an SMMU must lead directly to an ITS group for
// interrupts. Our DMA code handles at most one SMMU per device, so
// reject configurations that chain into another SMMU or other node.
static bool
__MappingsLeadToItsGroups(
    _In_ const struct __IortParser* parser,
    _In_ const struct __IortNode*   node)
{
    for (uint32_t i = 0; i < node->Header->MappingCount; i++) {
        uint32_t target = parser->Mappings[node->FirstMapping + i].Target;

        if (parser->Nodes[target].Header->Type != ACPI_IORT_NODE_ITS_GROUP) {
            return false;
        }
    }
    return true;
}

// Reads every SMMU node. An SMMU with features we do not support is kept in the
// catalogue with its DmaStatus set, so interrupt lookups through it still work,
// but no SMMU description is stored for it.
static oserr_t
__ReadSmmus(
    _InOut_ struct __IortParser* parser,
    _InOut_ ArmIortCatalogue_t*  catalogue)
{
    for (uint32_t i = 0; i < parser->NodeCount; i++) {
        struct __IortNode*    node = &parser->Nodes[i];
        ArmSMMUDescription_t  description = {0};
        ArmSMMUDescription_t* copy;
        oserr_t               status;

        if (node->Header->Type == ACPI_IORT_NODE_SMMU) {
            status = __ReadSMMU500(node, &description);
        } else if (node->Header->Type == ACPI_IORT_NODE_SMMU_V3) {
            status = __ReadSMMUv3(node, &description);
        } else {
            continue;
        }

        if (status != OS_EOK && status != OS_ENOTSUPPORTED) {
            return status;
        }

        node->Status = status;
        status = __CheckRegisterRange(parser, i);
        if (status != OS_EOK) {
            return status;
        }

        if (!__MappingsLeadToItsGroups(parser, node)) {
            node->Status = OS_ENOTSUPPORTED;
        }

        catalogue->Nodes[i].DmaStatus = node->Status;
        if (node->Status != OS_EOK) {
            WARNING("IORT: SMMU at node %u needs unsupported features", node->Offset);
            continue;
        }

        description.RegisterBase = node->RegisterBase;
        description.RegisterLength = node->RegisterLength;
        
        copy = kmalloc(sizeof(ArmSMMUDescription_t));
        if (copy == NULL) {
            return OS_EOOM;
        }

        *copy = description;
        catalogue->Nodes[i].Smmu = copy;
    }
    return OS_EOK;
}

// Reads the memory properties shared by PCI roots and named components. These
// say how many address bits devices can drive and whether device accesses stay
// in sync with the CPU caches. Malformed values fail the table. Hints or flags
// we do not understand only mark DMA through the node as unsupported.
static oserr_t
__ReadMemoryProperties(
    _In_    uint64_t       properties,
    _In_    uint8_t        addressBits,
    _InOut_ ArmIortNode_t* node)
{
    ACPI_IORT_MEMORY_ACCESS memory;

    memcpy(&memory, &properties, sizeof(memory));

    if (addressBits > 64 || memory.CacheCoherency > 1 || memory.Reserved != 0) {
        return OS_EINVALPARAMS;
    }

    node->AddressLimit = addressBits == 64 ? UINT64_MAX : (1ULL << addressBits) - 1;
    node->HardwareCoherent = (memory.MemoryFlags & ACPI_IORT_MF_COHERENCY) != 0;
    node->UsesDeviceMemoryAttributes = (memory.MemoryFlags & ACPI_IORT_MF_ATTRIBUTES) != 0;
    node->CachePolicy = memory.CacheCoherency == ACPI_IORT_NODE_COHERENT
        ? SHMDeviceCacheCoherent : SHMDeviceCacheNonCoherent;
    if (addressBits == 0 || (memory.Hints & ~0xfU) || (memory.MemoryFlags & ~3U)) {
        node->DmaStatus = OS_ENOTSUPPORTED;
    }
    return OS_EOK;
}

// Checks that name is an ACPI namespace path (for example "\_SB.ETH0"). Paths
// start with '\' followed by dot-separated segments of 1 to 4 uppercase
// letters, underscores, or trailing digits. The name must end with a zero byte
// before maxLength bytes have been read. On success, lengthOut is the length
// of the path without the zero byte.
static oserr_t
__MeasureAcpiPath(
    _In_  const char* name,
    _In_  uint32_t    maxLength,
    _Out_ uint32_t*   lengthOut)
{
    uint32_t length = 0;
    uint32_t segmentLength = 0;

    if (maxLength == 0 || name[0] != '\\') {
        return OS_EINVALPARAMS;
    }

    while (length < maxLength && name[length] != 0) {
        char c = name[length];

        if (length != 0) {
            if (c == '.') {
                if (segmentLength == 0) {
                    return OS_EINVALPARAMS;
                }
                segmentLength = 0;
            } else {
                if (!((c >= 'A' && c <= 'Z') || c == '_' ||
                      (segmentLength != 0 && c >= '0' && c <= '9')) || ++segmentLength > 4) {
                    return OS_EINVALPARAMS;
                }
            }
        }
        length++;
    }

    if (length == maxLength || segmentLength == 0) {
        return OS_EINVALPARAMS;
    }

    *lengthOut = length;
    return OS_EOK;
}

// Validates the device name of a named component node and stores a copy of it
// in the catalogue node. The name sits directly after the fixed fields of the
// node and ends before the mappings, or at the end of the node if it has none.
static oserr_t
__CopyName(
    _InOut_ struct __IortNode* node,
    _InOut_ ArmIortNode_t*     catalogueNode)
{
    const char* name = (const char*)node->Header + IORT_NAMED_NAME;
    uint32_t    end = node->Header->MappingCount != 0 ? node->Header->MappingOffset : node->Header->Length;
    uint32_t    length;
    char*       copy;
    oserr_t     status;

    if (end <= IORT_NAMED_NAME) {
        return OS_EINVALPARAMS;
    }

    status = __MeasureAcpiPath(name, end - IORT_NAMED_NAME, &length);
    if (status != OS_EOK) {
        return status;
    }

    status = __AddNodeRange(node, IORT_NAMED_NAME, length + 1, 1);
    if (status != OS_EOK) {
        return status;
    }

    copy = kmalloc(length + 1);
    if (copy == NULL) {
        return OS_EOOM;
    }

    memcpy(copy, name, length + 1);
    catalogueNode->AcpiPath = copy;
    return OS_EOK;
}

// Reads the memory properties and ACPI path of a named component node (a fixed
// non-PCI device).
static oserr_t
__ReadNamedComponent(
    _InOut_ struct __IortNode* node,
    _InOut_ ArmIortNode_t*     catalogueNode)
{
    const ACPI_IORT_NAMED_COMPONENT* named = (const void*)node->Header->NodeData;
    oserr_t                          status;

    status = __CopyName(node, catalogueNode);
    if (status != OS_EOK) {
        return status;
    }

    status = __ReadMemoryProperties(
        named->MemoryProperties,
        named->MemoryAddressLimit,
        catalogueNode
    );
    if (status != OS_EOK) {
        return status;
    }

    if (named->NodeFlags & ~(ACPI_IORT_NC_STALL_SUPPORTED | ACPI_IORT_NC_PASID_BITS)) {
        catalogueNode->DmaStatus = OS_ENOTSUPPORTED;
    }
    return OS_EOK;
}

// Reads the PCI segment and memory properties of a PCI root node. Revision 0
// roots have no address width field, so they get a width of 0 bits, which marks
// DMA through them as unsupported.
static oserr_t
__ReadPciRoot(
    _In_    const struct __IortNode* node,
    _InOut_ ArmIortNode_t*           catalogueNode)
{
    const ACPI_IORT_ROOT_COMPLEX* root = (const void*)node->Header->NodeData;
    uint8_t                       bits = node->Header->Revision == 0 ? 0 : root->MemoryAddressLimit;

    catalogueNode->Segment = root->PciSegmentNumber;
    return __ReadMemoryProperties(root->MemoryProperties, bits, catalogueNode);
}

// Checks one pair of devices (PCI roots and named components) for conflicts.
// Named components are told apart by their ACPI path. PCI roots on the same
// segment must not have overlapping requester ID ranges. When sameNode is set,
// both arguments are the same node, and only its own mappings are compared
// against each other.
static oserr_t
__CheckDevicePair(
    _In_ const ArmIortNode_t* node,
    _In_ const ArmIortNode_t* other,
    _In_ bool                 sameNode)
{
    if (node->AcpiPath != NULL || other->AcpiPath != NULL) {
        if (!sameNode && node->AcpiPath != NULL && other->AcpiPath != NULL &&
            !strcmp(node->AcpiPath, other->AcpiPath)) {
            return OS_EINVALPARAMS;
        }
        return OS_EOK;
    }

    if (node->Segment != other->Segment) {
        return OS_EOK;
    }

    for (uint32_t a = 0; a < node->MappingCount; a++) {
        const ArmIortMapping_t* left = &node->Mappings[a];

        for (uint32_t b = 0; b < (sameNode ? a : other->MappingCount); b++) {
            const ArmIortMapping_t* right = &other->Mappings[b];

            if (__Overlaps(left->InputFirst, left->InputLast, right->InputFirst, right->InputLast)) {
                return OS_EINVALPARAMS;
            }
        }
    }
    return OS_EOK;
}

// Multiple PCI roots can share a segment as long as their requester ID
// ranges do not overlap. Named components are distinguished by their ACPI
// paths instead. Since named components use single mappings that ignore
// input IDs, overlapping input values across them are harmless.
static oserr_t
__CheckDeviceConflicts(
    _In_ const ArmIortCatalogue_t* catalogue)
{
    for (uint32_t i = 0; i < catalogue->NodeCount; i++) {
        const ArmIortNode_t* node = &catalogue->Nodes[i];

        if (node->Type != ArmIortPciRoot && node->Type != ArmIortNamedComponent) {
            continue;
        }

        for (uint32_t j = 0; j <= i; j++) {
            const ArmIortNode_t* other = &catalogue->Nodes[j];
            oserr_t              status;

            if (other->Type != ArmIortPciRoot && other->Type != ArmIortNamedComponent) {
                continue;
            }

            status = __CheckDevicePair(node, other, i == j);
            if (status != OS_EOK) {
                return status;
            }
        }
    }
    return OS_EOK;
}

// Reads the DMA related properties of every PCI root and named component, then
// checks that no two of them conflict.
static oserr_t
__ReadDeviceNodes(
    _InOut_ struct __IortParser* parser,
    _InOut_ ArmIortCatalogue_t*  catalogue)
{
    for (uint32_t i = 0; i < parser->NodeCount; i++) {
        struct __IortNode* node = &parser->Nodes[i];
        ArmIortNode_t*     catalogueNode = &catalogue->Nodes[i];
        oserr_t            status;

        if (node->Header->Type == ACPI_IORT_NODE_NAMED_COMPONENT) {
            catalogueNode->DmaStatus = OS_EOK;
            status = __ReadNamedComponent(node, catalogueNode);
        } else if (node->Header->Type == ACPI_IORT_NODE_PCI_ROOT_COMPLEX) {
            catalogueNode->DmaStatus = OS_EOK;
            status = __ReadPciRoot(node, catalogueNode);
        } else {
            continue;
        }

        if (status != OS_EOK) {
            return status;
        }
    }
    return __CheckDeviceConflicts(catalogue);
}

// Copies the controller IDs of an ITS group node. The IDs follow the fixed
// fields of the node and match the translation IDs listed in the MADT.
static oserr_t
__ReadItsGroup(
    _InOut_ struct __IortNode* node,
    _InOut_ ArmIortNode_t*     catalogueNode)
{
    const ACPI_IORT_ITS_GROUP* its = (const void*)node->Header->NodeData;
    const uint8_t*             bytes = (const void*)node->Header;
    oserr_t                    status;

    if (node->Header->MappingCount != 0) {
        return OS_EINVALPARAMS;
    }

    status = __AddNodeRange(node, node->FixedLength, its->ItsCount, sizeof(uint32_t));
    if (status != OS_EOK) {
        return status;
    }

    if (its->ItsCount != 0) {
        uint32_t* identifiers = kmalloc(its->ItsCount * sizeof(uint32_t));

        if (identifiers == NULL) {
            return OS_EOOM;
        }
        memcpy(identifiers, bytes + node->FixedLength, its->ItsCount * sizeof(uint32_t));
        catalogueNode->ItsIdentifiers = identifiers;
    }
    catalogueNode->ItsCount = its->ItsCount;
    return OS_EOK;
}

// Checks a performance counter group (PMCG) node. Only its references are
// validated. A PMCG belongs to an SMMUv3, PCI root or named component, and has
// at most one mapping.
static oserr_t
__CheckPmcg(
    _In_ const struct __IortParser* parser,
    _In_ const struct __IortNode*   node)
{
    const ACPI_IORT_PMCG* pmcg = (const void*)node->Header->NodeData;
    uint32_t              target;
    oserr_t               status;

    status = __FindNode(parser, pmcg->NodeReference, &target);
    if (status != OS_EOK) {
        return status;
    }

    if (node->Header->MappingCount > 1 ||
        (parser->Nodes[target].Header->Type != ACPI_IORT_NODE_SMMU_V3 &&
         parser->Nodes[target].Header->Type != ACPI_IORT_NODE_PCI_ROOT_COMPLEX &&
         parser->Nodes[target].Header->Type != ACPI_IORT_NODE_NAMED_COMPONENT)) {
        return OS_EINVALPARAMS;
    }
    return OS_EOK;
}

// Checks the reserved memory ranges listed in an RMR node. Each range must
// be non-empty, aligned, inside the address space, and must not overlap another
// range of the same node.
static oserr_t
__CheckRmrRanges(
    _In_ const struct __IortNode* node,
    _In_ const ACPI_IORT_RMR*     rmr)
{
    const uint8_t* bytes = (const void*)node->Header;

    for (uint32_t a = 0; a < rmr->RmrCount; a++) {
        ACPI_IORT_RMR_DESC range;

        memcpy(&range, bytes + rmr->RmrOffset + a * sizeof(range), sizeof(range));
        if (range.Length == 0 || range.Length - 1 > UINT64_MAX - range.BaseAddress ||
            ((range.BaseAddress | range.Length) & (IORT_RMR_GRANULARITY - 1)) || range.Reserved != 0) {
            return OS_EINVALPARAMS;
        }

        for (uint32_t b = 0; b < a; b++) {
            ACPI_IORT_RMR_DESC other;

            memcpy(&other, bytes + rmr->RmrOffset + b * sizeof(other), sizeof(other));
            if (__Overlaps(range.BaseAddress, range.BaseAddress + range.Length - 1,
                           other.BaseAddress, other.BaseAddress + other.Length - 1)) {
                return OS_EINVALPARAMS;
            }
        }
    }
    return OS_EOK;
}

// Checks a reserved memory range (RMR) node. Its ranges are memory that firmware
// set aside for a device before boot, so every mapping must point to an SMMU.
static oserr_t
__CheckRmr(
    _InOut_ struct __IortParser* parser,
    _InOut_ struct __IortNode*   node)
{
    const ACPI_IORT_RMR* rmr = (const void*)node->Header->NodeData;
    oserr_t              status;

    if (rmr->RmrCount == 0 || node->Header->MappingCount == 0) {
        return OS_EINVALPARAMS;
    }

    status = __AddNodeRange(node, rmr->RmrOffset, rmr->RmrCount, sizeof(ACPI_IORT_RMR_DESC));
    if (status != OS_EOK) {
        return status;
    }

    // Cap total RMR ranges as well. Each range is compared against
    // earlier ones in the same node for overlaps, so capping only nodes
    // and mappings would still leave room for an oversized list to slow
    // down checks.
    if (rmr->RmrCount > ARM_IORT_MAX_RANGES - parser->RmrDescriptorCount) {
        return OS_ENOTSUPPORTED;
    }

    parser->RmrDescriptorCount += rmr->RmrCount;
    status = __CheckRmrRanges(node, rmr);
    if (status != OS_EOK) {
        return status;
    }

    for (uint32_t i = 0; i < node->Header->MappingCount; i++) {
        const struct __IortMapping* mapping = &parser->Mappings[node->FirstMapping + i];
        const struct __IortNode*    target = &parser->Nodes[mapping->Target];

        if (target->Header->Type != ACPI_IORT_NODE_SMMU &&
            target->Header->Type != ACPI_IORT_NODE_SMMU_V3) {
            return OS_EINVALPARAMS;
        }
    }
    return OS_EOK;
}

// Reads the node types that carry no DMA routing of their own: ITS groups,
// performance counter groups (PMCG) and reserved memory ranges (RMR).
static oserr_t
__ReadOtherNodes(
    _InOut_ struct __IortParser* parser,
    _InOut_ ArmIortCatalogue_t*  catalogue)
{
    for (uint32_t i = 0; i < parser->NodeCount; i++) {
        struct __IortNode* node = &parser->Nodes[i];
        oserr_t            status = OS_EOK;

        if (node->Header->Type == ACPI_IORT_NODE_ITS_GROUP) {
            status = __ReadItsGroup(node, &catalogue->Nodes[i]);
        } else if (node->Header->Type == ACPI_IORT_NODE_PMCG) {
            status = __CheckPmcg(parser, node);
        } else if (node->Header->Type == ACPI_IORT_NODE_RMR) {
            status = __CheckRmr(parser, node);
        }

        if (status != OS_EOK) {
            return status;
        }
    }
    return OS_EOK;
}

// Copies the type, position and ID mappings of every parsed node into the
// catalogue. This runs before the DMA checks because interrupt lookups still
// rely on the mappings even when a node's DMA is unsupported.
static void
__CopyNodes(
    _In_    const struct __IortParser* parser,
    _InOut_ ArmIortCatalogue_t*        catalogue)
{
    catalogue->NodeCount = parser->NodeCount;
    for (uint32_t i = 0; i < parser->NodeCount; i++) {
        const struct __IortNode* parsed = &parser->Nodes[i];
        ArmIortNode_t*           node = &catalogue->Nodes[i];

        node->Type = (enum ArmIortNodeType)parsed->Header->Type;
        node->Offset = parsed->Offset;
        node->Identifier = parsed->Header->Identifier;
        node->DmaStatus = OS_ENOTSUPPORTED;
        node->MappingCount = parsed->Header->MappingCount;
        node->Mappings = &catalogue->Mappings[parsed->FirstMapping];
        for (uint32_t j = 0; j < node->MappingCount; j++) {
            const struct __IortMapping* parsedMap = &parser->Mappings[parsed->FirstMapping + j];
            ArmIortMapping_t*           mapping = &catalogue->Mappings[parsed->FirstMapping + j];

            mapping->MapsAllInputsToOne = parsedMap->MapsAllInputsToOne;
            mapping->IsOwnInterruptMapping = parsedMap->IsOwnInterruptMapping;
            mapping->InputFirst = mapping->MapsAllInputsToOne || node->Type == ArmIortRmr ? 0 : parsedMap->Data.InputBase;
            mapping->InputLast = mapping->MapsAllInputsToOne ? UINT32_MAX : mapping->InputFirst + parsedMap->Data.IdCount;
            mapping->OutputFirst = parsedMap->Data.OutputBase;
            mapping->TargetIndex = parsedMap->Target;
        }
    }
}

// Walks and verifies the raw table in stages. First check that every node fits
// within the table bounds, then check all ID mappings, then verify the graph
// has no cycles. Only after that is the data copied into the catalogue and the
// per-node details read.
static oserr_t
__ParseTable(
    _InOut_ struct __IortParser* parser,
    _InOut_ ArmIortCatalogue_t*  catalogue)
{
    oserr_t status;

    status = __IndexNodes(parser);
    if (status != OS_EOK) {
        return status;
    }

    status = __ReadMappings(parser);
    if (status != OS_EOK) {
        return status;
    }

    status = __CheckGraph(parser);
    if (status != OS_EOK) {
        return status;
    }

    __CopyNodes(parser, catalogue);

    status = __ReadSmmus(parser, catalogue);
    if (status != OS_EOK) {
        return status;
    }

    status = __ReadDeviceNodes(parser, catalogue);
    if (status != OS_EOK) {
        return status;
    }
    return __ReadOtherNodes(parser, catalogue);
}

oserr_t
ArmIortCreate(
    _InOut_ ArmIortCatalogue_t** catalogueOut)
{
    ACPI_TABLE_HEADER*   header = NULL;
    struct __IortParser* parser = NULL;
    ArmIortCatalogue_t*  catalogue = NULL;
    oserr_t              status;

    if (catalogueOut == NULL) {
        return OS_EINVALPARAMS;
    }

    if (*catalogueOut != NULL) {
        return OS_EBUSY;
    }

    status = Arm64AcpiGetTable(ACPI_SIG_IORT, sizeof(ACPI_TABLE_IORT), &header);
    if (status != OS_EOK) {
        return status;
    }

    // Revision 0 uses the original layouts, while revision 5 matches revision
    // E.d of the IORT spec. Reject other revisions until someone checks their
    // node layouts against this code.
    if (header->Revision != 0 && header->Revision != 5) {
        status = OS_ENOTSUPPORTED;
        goto cleanup;
    }

    parser = kmalloc(sizeof(*parser));
    if (parser == NULL) {
        status = OS_EOOM;
        goto cleanup;
    }

    memset(parser, 0, sizeof(*parser));
    parser->Table = (const void*)header;
    
    catalogue = kmalloc(sizeof(ArmIortCatalogue_t));
    if (catalogue == NULL) {
        status = OS_EOOM;
        goto cleanup;
    }
    memset(catalogue, 0, sizeof(ArmIortCatalogue_t));

    // Only publish the catalogue once every stage passes.
    status = __ParseTable(parser, catalogue);
    if (status != OS_EOK) {
        goto cleanup;
    }

    *catalogueOut = catalogue;
    catalogue = NULL;

cleanup:
    if (status != OS_EOK) {
        ERROR("IORT: discovery failed, status %u", status);
    }
    ArmIortDestroy(&catalogue);
    kfree(parser);
    AcpiPutTable(header);
    return status;
}
