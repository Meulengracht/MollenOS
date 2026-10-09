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

#include <firmware/reader.h>
#include <string.h>

/**
 * @brief Holds the nodes along the path currently being read from the tree.
 * The first entry is the root, and each later entry is a child of the one
 * before it. When a node is complete, the callback function supplied by the
 * caller receives this temporary array. It may copy individual node
 * descriptions, but must not keep the array itself. Each description's name
 * and property data still point into the firmware data, so they remain usable
 * only while that data is available and unchanged.
 */
struct __FdtWalk {
    struct FdtNode Nodes[FDT_MAX_DEPTH];
    int            Depth;
    const uint8_t* Structure;
    const char*    Strings;
    uint32_t       StringsLength;
    FdtNodeFn      Visitor;
    void*          Context;
};

/**
 * @brief Holds the name of a property to search for and its matching data.
 * The property bytes are not copied; the pointer refers to the original
 * firmware data. This allows callers to read any property, including ones
 * that do not have a separate field in FdtNode.
 */
struct __FdtPropertyQuery {
    const char*    Name;
    const uint8_t* Value;
    uint32_t       Length;
};

/**
 * @brief Holds the result of searching for a node by its numeric ID.
 * A node's phandle is the number other nodes use to refer to it. This search
 * counts every matching node, including disabled ones, so it can reject an ID
 * that appears more than once instead of choosing an uncertain match.
 */
struct __FdtNodeQuery {
    uint32_t       Phandle;
    unsigned int   Count;
    struct FdtNode Node;
};

static oserr_t
__ReaderBegin(
    _InOut_ void*    context,
    _In_ const char* name,
    _In_ uint32_t    length)
{
    struct __FdtWalk*     walk = context;
    struct FdtNode*       node = &walk->Nodes[++walk->Depth];
    const struct FdtNode* parent = walk->Depth ? node - 1 : NULL;

    memset(node, 0, sizeof(*node));
    node->Name = name;
    node->NodeOffset = (uint32_t)((const uint8_t*)name - walk->Structure) - 4;
    node->Properties = (const uint8_t*)name + ((length + 4) & ~3U);
    node->Strings = walk->Strings;
    node->StringsLength = walk->StringsLength;
    if (parent != NULL) {
        node->AncestorDisabled = parent->Disabled || parent->AncestorDisabled;
        node->AncestorMalformed = parent->Malformed || parent->AncestorMalformed;
    }
    return OS_EOK;
}

static oserr_t
__ReaderProperty(
    _InOut_ void*    context,
    _In_ const char* name,
    _In_ const void* value,
    _In_ uint32_t    length)
{
    struct __FdtWalk* walk = context;
    struct FdtNode*   node = &walk->Nodes[walk->Depth];
    const uint8_t*    bytes = value;
    const uint8_t*    end;
    uint32_t          offset = 0;
    uint32_t          phandle;

    node->PropertiesLength = (uint32_t)(bytes - node->Properties) + ((length + 3) & ~3U);
    if (!strcmp(name, "compatible") || !strcmp(name, "status")) {
        if (!length) {
            node->Malformed = 1;
        }
        
        while (offset < length) {
            end = memchr(bytes + offset, 0, length - offset);
            if (end == NULL || end == bytes + offset) {
                node->Malformed = 1;
                break;
            }
            offset = (uint32_t)(end - bytes) + 1;
            if (!strcmp(name, "status") && offset != length) {
                node->Malformed = 1;
            }
        }
        
        if (!strcmp(name, "status")) {
            node->Disabled = !((length == 3 && !memcmp(bytes, "ok", 3)) ||
                (length == 5 && !memcmp(bytes, "okay", 5)));
        }
    } else if (!strcmp(name, "phandle") || !strcmp(name, "linux,phandle")) {
        if (length != 4) {
            node->Malformed = 1;
        } else {
            phandle = FdtReadBe32(bytes);
            node->Malformed |= !phandle || phandle == UINT32_MAX ||
                (node->Phandle && node->Phandle != phandle);
            node->Phandle = phandle;
        }
    }
    return OS_EOK;
}

static oserr_t
__ReaderEnd(
    _InOut_ void* context)
{
    struct __FdtWalk* walk = context;

    if (walk->Visitor != NULL) {
        walk->Visitor(walk->Nodes, walk->Depth, walk->Context);
    }
    walk->Depth--;
    return OS_EOK;
}

oserr_t
FdtWalkNodes(
    _In_ const void* blob,
    _In_ size_t length,
    _In_ FdtNodeFn visitor,
    _InOut_ void* context)
{
    struct FDTHeader header;
    struct __FdtWalk walk = { .Depth = -1 };
    struct FdtParser parser = { 
        __ReaderBegin,
        __ReaderProperty,
        __ReaderEnd,
        &walk
    };
    oserr_t          status;

    status = FdtParseHeader(
        blob,
        length > UINT32_MAX ? UINT32_MAX : (uint32_t)length,
        &header
    );
    if (status != OS_EOK) {
        return status;
    }

    walk.Structure = (const uint8_t*)blob + header.OffDtStruct;
    walk.Strings = (const char*)blob + header.OffDtStrings;
    walk.StringsLength = header.SizeDtStrings;
    
    // Check the complete tree before calling the visitor. Otherwise, a format
    // error near the end could be reported after the visitor has already acted
    // on earlier nodes. The second read delivers the nodes only after the first
    // read succeeds, so the caller must keep the firmware data unchanged until
    // both reads finish.
    status = FdtParseStructure(walk.Structure, header.SizeDtStruct,
        walk.Strings, walk.StringsLength, &parser);
    if (status != OS_EOK || visitor == NULL) {
        return status;
    }
    
    walk.Visitor = visitor;
    walk.Context = context;
    return FdtParseStructure(
        walk.Structure,
        header.SizeDtStruct,
        walk.Strings,
        walk.StringsLength,
        &parser
    );
}

static oserr_t
__ReaderFindProperty(
    _InOut_ void*    context,
    _In_ const char* name,
    _In_ const void* value,
    _In_ uint32_t    length)
{
    struct __FdtPropertyQuery* query = context;

    if (!strcmp(name, query->Name)) {
        query->Value = value;
        query->Length = length;
    }
    return OS_EOK;
}

const uint8_t*
FdtProperty(
    _In_  const struct FdtNode* node,
    _In_  const char*           name,
    _Out_ uint32_t*             length)
{
    struct __FdtPropertyQuery query = { .Name = name };
    struct FdtParser          parser = {
        .Property = __ReaderFindProperty,
        .UserData = &query
    };

    FdtVisitProperties(
        node->Properties,
        node->PropertiesLength,
        node->Strings,
        node->StringsLength,
        &parser
    );
    
    *length = query.Length;
    return query.Value;
}

int
FdtCompatible(
    _In_ const struct FdtNode* node,
    _In_ const char*           compatible)
{
    uint32_t       length;
    const uint8_t* value = FdtProperty(node, "compatible", &length);
    const uint8_t* end;
    uint32_t       offset = 0;

    if (node->Malformed) {
        return 0;
    }

    while (offset < length) {
        end = memchr(value + offset, 0, length - offset);
        if (end == NULL) {
            return 0;
        }
        
        if (!strcmp((const char*)value + offset, compatible)) {
            return 1;
        }
        offset = (uint32_t)(end - value) + 1;
    }
    return 0;
}

oserr_t
FdtScalar(
    _In_  const struct FdtNode* node,
    _In_  const char*           name,
    _Out_ uint32_t*             value)
{
    uint32_t       length;
    const uint8_t* property;

    property = FdtProperty(node, name, &length);
    if (property == NULL) {
        return OS_ENOENT;
    }
    if (length != 4) {
        return OS_EINVALPARAMS;
    }

    *value = FdtReadBe32(property);
    return OS_EOK;
}

static void
__ReaderFindNode(
    _In_    const struct FdtNode* nodes,
    _In_    int                   depth,
    _InOut_ void*                 context)
{
    struct __FdtNodeQuery* query = context;

    if (nodes[depth].Phandle == query->Phandle) {
        query->Node = nodes[depth];
        query->Count++;
    }
}

oserr_t
FdtFindNode(
    _In_  const void*     blob,
    _In_  size_t          length,
    _In_  uint32_t        phandle,
    _Out_ struct FdtNode* node)
{
    struct __FdtNodeQuery query = { .Phandle = phandle };
    oserr_t               status;

    if (!phandle || node == NULL) {
        return OS_EINVALPARAMS;
    }

    status = FdtWalkNodes(blob, length, __ReaderFindNode, &query);
    if (status != OS_EOK) {
        return status;
    }
    
    if (query.Count > 1 || query.Node.Malformed || query.Node.AncestorMalformed) {
        return OS_EINVALPARAMS;
    }
    if (!query.Count || query.Node.Disabled || query.Node.AncestorDisabled) {
        return OS_ENOENT;
    }
    
    *node = query.Node;
    return OS_EOK;
}
