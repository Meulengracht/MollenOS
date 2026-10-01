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

#include <devicetree.h>
#include "private.h"

static oserr_t
__SkipPaddedValue(
    _In_    const uint8_t* block,
    _In_    uint32_t       size,
    _InOut_ uint32_t*      cursor,
    _In_    uint32_t       length)
{
    uint32_t padding;

    // Subtract before adding: a corrupt length must not wrap the cursor into
    // an earlier, apparently valid portion of the blob.
    if (length > size - *cursor) {
        return OS_EINVALPARAMS;
    }
    *cursor += length;
    
    padding = (4 - (*cursor & 3)) & 3;
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

oserr_t
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
    while ((structureBlockSize - cursor) >= 4) {
        uint32_t token = __ReadBe32(block + cursor);
        cursor += 4;
        
        switch (token) {
        case FDT_BEGIN_NODE: {
            const char*    name;
            const uint8_t* end;
            uint32_t       length;

            end = memchr(block + cursor, 0, structureBlockSize - cursor);
            if ((!depth && rootSeen)) {
                return OS_EINVALPARAMS;
            }
            
            length = (uint32_t)(end - (block + cursor));
            name = (const char*)block + cursor;
            
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
            uint32_t length;
            uint32_t nameOffset;
            const char* name;
            const void* value;

            if (!depth || childrenStarted[depth - 1] || structureBlockSize - cursor < 8) {
                return OS_EINVALPARAMS;
            }
            
            length = __ReadBe32(block + cursor);
            nameOffset = __ReadBe32(block + cursor + 4);
            name = __GetStringFromBlock(stringBlock, stringBlockSize, nameOffset);
            if (!name || !name[0]) {
                return OS_EINVALPARAMS;
            }
            
            cursor += 8;
            
            value = block + cursor;
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

oserr_t
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
    header->SizeDtStrings = __ReadBe32(p + 32);
    header->SizeDtStruct = __ReadBe32(p + 36);
    return __ValidateFDTHeader(header);
}
