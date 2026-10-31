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

#include <ddk/utils.h>
#include <stdlib.h>
#include <string.h>

#include "tree-private.h"

struct __FdtNodeVisitor {
    FdtNodeFn Visitor;
    void*     Context;
};

struct __FdtBuilder {
    struct FdtTree*  Tree;
    const uint8_t*   Structure;
    size_t           Capacity;
    size_t           ParentPath[FDT_MAX_DEPTH];
    int              Depth;
};

static oserr_t
__ReaderBegin(
    _InOut_ void*       context,
    _In_    const char* name,
    _In_    uint32_t    length)
{
    struct __FdtBuilder*  builder = context;
    struct FdtTree*       tree = builder->Tree;
    struct __FdtEntry*    entries;
    struct __FdtEntry*    entry;
    const struct FdtNode* parent;
    size_t                capacity;
    (void)length;

    if (tree->Count == builder->Capacity) {
        capacity = builder->Capacity ? builder->Capacity * 2 : 64;
        if (capacity < builder->Capacity || capacity > SIZE_MAX / sizeof(*entries)) {
            return OS_EOOM;
        }
        entries = realloc(tree->Entries, capacity * sizeof(*entries));
        if (entries == NULL) {
            return OS_EOOM;
        }
        tree->Entries = entries;
        builder->Capacity = capacity;
    }

    builder->Depth++;
    entry = &tree->Entries[tree->Count];
    
    memset(entry, 0, sizeof(struct __FdtEntry));
    entry->Depth = builder->Depth;
    entry->Parent = builder->Depth ? builder->ParentPath[builder->Depth - 1] : SIZE_MAX;
    entry->Resources.View.Name = name;
    entry->Resources.View.NodeOffset = (uint32_t)((const uint8_t*)name - builder->Structure) - 4;
    
    if (entry->Parent != SIZE_MAX) {
        parent = &tree->Entries[entry->Parent].Resources.View;
        entry->Resources.View.ParentDisabled = parent->Disabled || parent->ParentDisabled;
        entry->Resources.View.ParentMalformed = parent->Malformed || parent->ParentMalformed;
    }

    builder->ParentPath[builder->Depth] = tree->Count++;
    return OS_EOK;
}

static oserr_t
__ReaderProperty(
    _InOut_ void*       context,
    _In_    const char* name,
    _In_    const void* value,
    _In_    uint32_t    length)
{
    struct __FdtBuilder* builder = context;
    struct __FdtEntry*   entry = &builder->Tree->Entries[builder->ParentPath[builder->Depth]];
    struct FdtNode*      node = &entry->Resources.View;
    struct FdtProperty*  properties;
    const uint8_t*       bytes = value;

    // Duplicate names would leave the cached value dependent on property order.
    for (size_t i = 0; i < node->PropertyCount; i++) {
        if (!strcmp(node->Properties[i].Name, name)) {
            return OS_EINVALPARAMS;
        }
    }

    if (node->PropertyCount >= SIZE_MAX / sizeof(*properties) - 1) {
        return OS_EOOM;
    }

    properties = realloc(
        (void*)node->Properties,
        (node->PropertyCount + 1) * sizeof(*properties)
    );
    if (properties == NULL) {
        return OS_EOOM;
    }
    
    node->Properties = properties;
    properties[node->PropertyCount++] = (struct FdtProperty) { name, bytes, length };
    
    if (!strcmp(name, "compatible") || !strcmp(name, "status")) {
        uint32_t offset = 0;
        
        if (!length) {
            node->Malformed = 1;
        }

        while (offset < length) {
            const uint8_t* end = memchr(bytes + offset, 0, length - offset);
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
            uint32_t phandle = FdtReadBe32(bytes);
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
    struct __FdtBuilder* builder = context;

    // The subtree ends immediately before the next node appended to the array.
    builder->Tree->Entries[builder->ParentPath[builder->Depth]].End = builder->Tree->Count;
    builder->Depth--;
    return OS_EOK;
}

static int
__CompareProviders(
    _In_ const void* left,
    _In_ const void* right)
{
    const struct __FdtEntry* a = *(const struct __FdtEntry* const*)left;
    const struct __FdtEntry* b = *(const struct __FdtEntry* const*)right;

    return (a->Resources.Phandle > b->Resources.Phandle) -
        (a->Resources.Phandle < b->Resources.Phandle);
}

static size_t
__FindOffset(
    _In_ const struct FdtTree* tree,
    _In_ uint32_t              offset)
{
    size_t low = 0;
    size_t high = tree->Count;
    size_t middle;

    while (low < high) {
        middle = low + (high - low) / 2;
        
        if (tree->Entries[middle].Resources.NodeOffset < offset) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }

    if (low == tree->Count || tree->Entries[low].Resources.NodeOffset != offset) {
        return SIZE_MAX;
    }
    return low;
}

static const struct FdtResources*
__FindPath(
    _In_ const struct FdtTree* tree,
    _In_ const char*           path,
    _In_ size_t                length)
{
    size_t parent = 0;
    size_t position = 1;

    if (!length || path[0] != '/') {
        return NULL;
    }

    while (position < length) {
        size_t end = position;
        size_t match;
        
        while (end < length && path[end] != '/') {
            end++;
        }
        match = SIZE_MAX;

        // End skips each child's subtree so only direct children are compared.
        for (size_t child = parent + 1; child < tree->Entries[parent].End;
             child = tree->Entries[child].End) {
            const char* name = tree->Entries[child].Resources.Name;
            
            if (strlen(name) == end - position && !memcmp(name, path + position, end - position)) {
                if (match != SIZE_MAX) {
                    return NULL;
                }
                match = child;
            }
        }

        if (match == SIZE_MAX) {
            return NULL;
        }
        
        parent = match;
        position = end + 1;
    }
    return &tree->Entries[parent].Resources;
}

static void
__ResolveConsole(
    _InOut_ struct FdtTree* tree)
{
    const struct FdtResources* chosen = __FindPath(tree, "/chosen", 7);
    const struct FdtResources* aliases;
    const struct FdtResources* console;
    const struct FdtProperty*  property;
    const uint8_t*             path;
    uint32_t                   length;
    size_t                     nameLength;
    size_t                     i;

    if (chosen == NULL || chosen->Malformed || chosen->Disabled) {
        return;
    }

    path = FdtProperty(&chosen->View, "stdout-path", &length);
    if (path == NULL || !length) {
        return;
    }

    if (path[length - 1] || strlen((const char*)path) != length - 1) {
        return;
    }

    // A colon separates the node path or alias from console options.
    nameLength = strcspn((const char*)path, ":");
    if (path[0] != '/') {
        aliases = __FindPath(tree, "/aliases", 8);
        
        if (aliases == NULL || aliases->Malformed || aliases->Disabled) {
            return;
        }
        
        for (i = 0; i < aliases->View.PropertyCount; i++) {
            property = &aliases->View.Properties[i];
            if (strlen(property->Name) == nameLength && !memcmp(property->Name, path, nameLength)) {
                break;
            }
        }
        
        if (i == aliases->View.PropertyCount) {
            return;
        }

        path = property->Value;
        length = property->Length;
        if (!length || path[length - 1] || strlen((const char*)path) != length - 1) {
            return;
        }
        
        nameLength = strcspn((const char*)path, ":");
    }

    console = __FindPath(tree, (const char*)path, nameLength);
    if (console == NULL) {
        return;
    }
    
    if (console->Disabled || console->ParentDisabled) {
        return;
    }
    
    if (console->Malformed || console->ParentMalformed) {
        return;
    }
    tree->Console = console;
}

oserr_t
FdtTreeCreate(
    _In_  const void*      blob,
    _In_  size_t           length,
    _Out_ struct FdtTree** treeOut)
{
    struct FDTHeader     header;
    struct FdtTree*      tree;
    struct FdtResources* parentPath;
    struct __FdtEntry*   entry;
    oserr_t              status;
    size_t               i;
    
    struct __FdtBuilder  builder = { 
        .Depth = -1
    };
    struct FdtParser     parser = { 
        __ReaderBegin,
        __ReaderProperty,
        __ReaderEnd,
        &builder
    };
    
    status = FdtParseHeader(
        blob,
        length > UINT32_MAX ? UINT32_MAX : (uint32_t)length,
        &header
    );
    if (status != OS_EOK) {
        return status;
    }

    tree = calloc(1, sizeof(struct FdtTree));
    if (tree == NULL) {
        return OS_EOOM;
    }
    
    atomic_init(&tree->References, 1);
    builder.Tree = tree;
    builder.Structure = (const uint8_t*)blob + header.OffDtStruct;
    
    status = FdtParseStructure(
        builder.Structure,
        header.SizeDtStruct,
        (const char*)blob + header.OffDtStrings,
        header.SizeDtStrings,
        &parser
    );
    if (status != OS_EOK) {
        FdtTreeRelease(tree);
        return status;
    }

    // The parser visits parents first, so their decoded records are available
    // when a child needs inherited address and interrupt settings.
    parentPath = malloc(FDT_MAX_DEPTH * sizeof(*parentPath));
    tree->Providers = malloc(tree->Count * sizeof(*tree->Providers));
    if (parentPath == NULL || tree->Providers == NULL) {
        free(parentPath);
        FdtTreeRelease(tree);
        return OS_EOOM;
    }
    
    for (i = 0; i < tree->Count; i++) {
        entry = &tree->Entries[i];
        FdtDecodeResources(&entry->Resources.View, parentPath, entry->Depth);
        entry->Resources = parentPath[entry->Depth];
        if (entry->Resources.Phandle) {
            tree->Providers[tree->ProviderCount++] = entry;
        }
    }
    free(parentPath);
    
    qsort(
        tree->Providers,
        tree->ProviderCount,
        sizeof(*tree->Providers),
        __CompareProviders
    );
    __ResolveConsole(tree);
    
    *treeOut = tree;
    return OS_EOK;
}

void
FdtTreeAddReference(
    _In_ struct FdtTree* tree)
{
    atomic_fetch_add(&tree->References, 1);
}

void
FdtTreeRelease(
    _In_ struct FdtTree* tree)
{
    if (tree == NULL || atomic_fetch_sub(&tree->References, 1) != 1) {
        return;
    }
    
    for (size_t i = 0; i < tree->Count; i++) {
        free((void*)tree->Entries[i].Resources.View.Properties);
    }
    
    free(tree->Providers);
    free(tree->Entries);
    free(tree);
}

const uint8_t*
FdtProperty(
    _In_  const struct FdtNode* node,
    _In_  const char*           name,
    _Out_ uint32_t*             length)
{
    for (size_t i = 0; i < node->PropertyCount; i++) {
        if (!strcmp(node->Properties[i].Name, name)) {
            *length = node->Properties[i].Length;
            return node->Properties[i].Value;
        }
    }
    
    *length = 0;
    return NULL;
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
        const uint8_t* end = memchr(value + offset, 0, length - offset);
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
        ERROR("FdtScalar: node %u property %s has length %u, expected 4",
            node->NodeOffset, name, length);
        return OS_EINVALPARAMS;
    }

    *value = FdtReadBe32(property);
    return OS_EOK;
}

const struct FdtResources*
FdtTreeNode(
    _In_ const struct FdtTree* tree,
    _In_ uint32_t              offset)
{
    size_t index = __FindOffset(tree, offset);

    return index == SIZE_MAX ? NULL : &tree->Entries[index].Resources;
}

const struct FdtResources*
FdtTreeConsole(
    _In_ const struct FdtTree* tree)
{
    return tree->Console;
}

const struct FdtResources*
FdtTreeParent(
    _In_ const struct FdtTree* tree,
    _In_ uint32_t             offset)
{
    size_t index = __FindOffset(tree, offset);

    if (index == SIZE_MAX || tree->Entries[index].Parent == SIZE_MAX) {
        return NULL;
    }
    return &tree->Entries[tree->Entries[index].Parent].Resources;
}

oserr_t
FdtFindResources(
    _In_  const struct FdtTree* tree,
    _In_  uint32_t              phandle,
    _Out_ struct FdtResources*  provider)
{
    const struct FdtResources* result;
    size_t                     low = 0;
    size_t                     high = tree->ProviderCount;

    if (!phandle) {
        return OS_EINVALPARAMS;
    }

    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (tree->Providers[middle]->Resources.Phandle < phandle) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }

    if (low == tree->ProviderCount || tree->Providers[low]->Resources.Phandle != phandle) {
        return OS_ENOENT;
    }
    
    if (low + 1 < tree->ProviderCount && tree->Providers[low + 1]->Resources.Phandle == phandle) {
        return OS_EINVALPARAMS;
    }
    
    result = &tree->Providers[low]->Resources;
    if (result->Malformed || result->ParentMalformed) {
        return OS_EINVALPARAMS;
    }
    
    if (result->Disabled || result->ParentDisabled) {
        return OS_ENOENT;
    }
    
    *provider = *result;
    return OS_EOK;
}

oserr_t
FdtFindNode(
    _In_  const struct FdtTree* tree,
    _In_  uint32_t              phandle,
    _Out_ struct FdtNode*       node)
{
    struct FdtResources resources;
    oserr_t             status;

    if (node == NULL) {
        return OS_EINVALPARAMS;
    }

    status = FdtFindResources(tree, phandle, &resources);
    if (status == OS_EOK) {
        *node = resources.View;
    }
    return status;
}

oserr_t
FdtVisitSubtree(
    _In_    const struct FdtTree* tree,
    _In_    uint32_t              offset,
    _In_    FdtResourceFn         visitor,
    _InOut_ void*                 context)
{
    struct FdtResources* parentPath;
    size_t               index = __FindOffset(tree, offset);
    size_t               parent;
    size_t               end;

    if (index == SIZE_MAX) {
        return OS_ENOENT;
    }

    parentPath = malloc(FDT_MAX_DEPTH * sizeof(struct FdtResources));
    if (parentPath == NULL) {
        return OS_EOOM;
    }
    
    parent = tree->Entries[index].Parent;
    while (parent != SIZE_MAX) {
        parentPath[tree->Entries[parent].Depth] = tree->Entries[parent].Resources;
        parent = tree->Entries[parent].Parent;
    }
    
    end = tree->Entries[index].End;
    for (; index < end; index++) {
        parentPath[tree->Entries[index].Depth] = tree->Entries[index].Resources;
        visitor(parentPath, tree->Entries[index].Depth, context);
    }
    
    free(parentPath);
    return OS_EOK;
}

oserr_t
FdtWalkResources(
    _In_    const struct FdtTree* tree,
    _In_    FdtResourceFn         visitor,
    _InOut_ void*                 context)
{
    return FdtVisitSubtree(
        tree,
        tree->Entries[0].Resources.NodeOffset,
        visitor,
        context
    );
}

static void
__VisitNodeViews(
    _In_    const struct FdtResources* resources,
    _In_    int                        depth,
    _InOut_ void*                      context)
{
    struct __FdtNodeVisitor* visitor = context;
    struct FdtNode           nodes[FDT_MAX_DEPTH];

    for (int i = 0; i <= depth; i++) {
        nodes[i] = resources[i].View;
    }
    visitor->Visitor(nodes, depth, visitor->Context);
}

oserr_t
FdtWalkNodes(
    _In_    const struct FdtTree* tree,
    _In_    FdtNodeFn             visitor,
    _InOut_ void*                 context)
{
    struct __FdtNodeVisitor adapter = { visitor, context };
    if (visitor == NULL) {
        return OS_EOK;
    }
    return FdtWalkResources(tree, __VisitNodeViews, &adapter);
}
