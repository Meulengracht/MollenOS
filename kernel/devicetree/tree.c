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

#include <fdt/reader.h>
#include <devicetree.h>
#include <heap.h>
#include <string.h>

struct __TreeBuilder {
    DeviceTree_t*     Tree;
    DeviceTreeNode_t* Current;
    DeviceTreeNode_t* Last;
};

const DeviceTreeProperty_t*
DeviceTreeGetProperty(
    _In_ const DeviceTreeNode_t* node,
    _In_ const char*             name)
{
    const DeviceTreeProperty_t* property;

    for (property = node->Properties; property; property = property->Next) {
        if (!strcmp(property->Name, name)) {
            return property;
        }
    }
    return NULL;
}

const char*
DeviceTreeReadString(
    _In_ const DeviceTreeNode_t* node,
    _In_ const char*             name)
{
    const DeviceTreeProperty_t* property = DeviceTreeGetProperty(node, name);

    if (!property || !property->Length ||
        !memchr(property->Value, 0, property->Length)) {
        return NULL;
    }
    return property->Value;
}

static uint64_t
__ReadCells(
    _In_ const unsigned char* data,
    _In_ uint32_t             count)
{
    uint64_t value = 0;

    for (uint32_t i = 0; i < count; i++) {
        value = (value << 32) | FdtReadBe32(data + i * 4);
    }
    return value;
}

oserr_t
DeviceTreeReadInteger(
    _In_ const DeviceTreeNode_t* node,
    _In_ const char*             name,
    _In_ uint64_t*               valueOut)
{
    const DeviceTreeProperty_t* property;

    property = DeviceTreeGetProperty(node, name);
    if (!property) {
        return OS_ENOENT;
    }
    
    if (property->Length != 4 && property->Length != 8) {
        return OS_EINVALPARAMS;
    }
    
    *valueOut = __ReadCells(property->Value, property->Length / 4);
    return OS_EOK;
}

int
DeviceTreeIsCompatible(
    _In_ const DeviceTreeNode_t* node,
    _In_ const char*             compatible)
{
    const DeviceTreeProperty_t* property;
    uint32_t                    offset = 0;

    property = DeviceTreeGetProperty(node, "compatible");
    if (!property) {
        return 0;
    }
    
    while (offset < property->Length) {
        const char* value = (const char*)property->Value + offset;
        const char* end = memchr(value, 0, property->Length - offset);

        if (!end) {
            return 0;
        }
        if (!strcmp(value, compatible)) {
            return 1;
        }
        offset += (uint32_t)(end - value) + 1;
    }
    return 0;
}

int
DeviceTreeIsEnabled(
    _In_ const DeviceTreeNode_t* node)
{
    while (node) {
        const DeviceTreeProperty_t* property = DeviceTreeGetProperty(node, "status");

        if (property) {
            const char* status = DeviceTreeReadString(node, "status");

            if (!status || (strcmp(status, "okay") && strcmp(status, "ok"))) {
                return 0;
            }
        }
        node = node->Parent;
    }
    return 1;
}

const DeviceTreeNode_t*
DeviceTreeFindPhandle(
    _In_ const DeviceTree_t* tree,
    _In_ uint32_t            phandle)
{
    const DeviceTreeNode_t* node;

    if (!phandle || phandle == UINT32_MAX) {
        return NULL;
    }
    
    for (node = tree->Nodes; node; node = node->NextNode) {
        if (node->Phandle == phandle) {
            return node;
        }
    }
    return NULL;
}

static const DeviceTreeNode_t*
__FindAbsolutePath(
    _In_ const DeviceTree_t* tree,
    _In_ const char*         path)
{
    const DeviceTreeNode_t* node = tree->Root;

    if (*path++ != '/') {
        return NULL;
    }
    
    while (*path && *path != ':') {
        size_t length = 0;
        const DeviceTreeNode_t* child;

        while (path[length] && path[length] != '/' && path[length] != ':') {
            length++;
        }
        
        for (child = node->Children; child; child = child->Next) {
            if (strlen(child->Name) == length && !memcmp(child->Name, path, length)) {
                break;
            }
        }
        
        if (!child) {
            return NULL;
        }
        
        node = child;
        path += length;
        if (*path == '/') {
            path++;
        }
    }
    return node;
}

const DeviceTreeNode_t*
DeviceTreeFindPath(
    _In_ const DeviceTree_t* tree,
    _In_ const char*         path)
{
    const DeviceTreeNode_t*     aliases;
    const DeviceTreeProperty_t* property;
    size_t                      length;

    if (*path == '/') {
        return __FindAbsolutePath(tree, path);
    }
    
    aliases = __FindAbsolutePath(tree, "/aliases");
    if (!aliases) {
        return NULL;
    }
    
    length = 0;
    while (path[length] && path[length] != ':') {
        length++;
    }
    
    for (property = aliases->Properties; property; property = property->Next) {
        if (strlen(property->Name) == length && !memcmp(property->Name, path, length)) {
            const char* target = DeviceTreeReadString(aliases, property->Name);

            return target ? __FindAbsolutePath(tree, target) : NULL;
        }
    }
    return NULL;
}

static oserr_t
__BeginNode(
    _In_ void*       userData,
    _In_ const char* name,
    _In_ uint32_t    nameLength)
{
    struct __TreeBuilder* builder = userData;
    DeviceTreeNode_t*     node;
    DeviceTreeNode_t**    link;

    (void)nameLength;
    
    node = kmalloc(sizeof(*node));
    if (!node) {
        return OS_EOOM;
    }

    memset(node, 0, sizeof(*node));
    node->Name = name;
    node->Parent = builder->Current;
    
    if (builder->Last) {
        builder->Last->NextNode = node;
    } else {
        builder->Tree->Nodes = node;
    }
    
    builder->Last = node;
    if (node->Parent) {
        link = &node->Parent->Children;
        while (*link) {
            if (!strcmp((*link)->Name, name)) {
                return OS_EINVALPARAMS;
            }
            link = &(*link)->Next;
        }
        *link = node;
    } else {
        builder->Tree->Root = node;
    }
    
    builder->Current = node;
    return OS_EOK;
}

static oserr_t
__Property(
    _In_ void*       userData,
    _In_ const char* name,
    _In_ const void* value,
    _In_ uint32_t    length)
{
    struct __TreeBuilder* builder = userData;
    DeviceTreeProperty_t* property;

    if (DeviceTreeGetProperty(builder->Current, name)) {
        return OS_EINVALPARAMS;
    }
    
    property = kmalloc(sizeof(*property));
    if (!property) {
        return OS_EOOM;
    }
    
    property->Name = name;
    property->Value = value;
    property->Length = length;
    
    // Insert into the front of the property's list
    property->Next = builder->Current->Properties;
    builder->Current->Properties = property;
    return OS_EOK;
}

static oserr_t
__EndNode(
    _In_ void* userData)
{
    struct __TreeBuilder*       builder = userData;
    DeviceTreeNode_t*           node = builder->Current;
    const DeviceTreeProperty_t* property;
    const DeviceTreeProperty_t* legacy;
    uint32_t                    phandle;

    // get the phandle properties
    property = DeviceTreeGetProperty(node, "phandle");
    legacy = DeviceTreeGetProperty(node, "linux,phandle");

    if (property || legacy) {
        if (!property) {
            property = legacy;
        }
        
        if (property->Length != 4 || (legacy && (legacy->Length != 4 ||
            FdtReadBe32(legacy->Value) != FdtReadBe32(property->Value)))) {
            return OS_EINVALPARAMS;
        }
        
        phandle = FdtReadBe32(property->Value);
        if (!phandle || phandle == UINT32_MAX ||
            DeviceTreeFindPhandle(builder->Tree, phandle)) {
            return OS_EINVALPARAMS;
        }
        node->Phandle = phandle;
    }
    
    builder->Current = node->Parent;
    return OS_EOK;
}

void
DeviceTreeDestroy(
    _In_ DeviceTree_t* tree)
{
    DeviceTreeNode_t* node = tree->Nodes;

    while (node) {
        DeviceTreeNode_t* nextNode = node->NextNode;
        DeviceTreeProperty_t* property = node->Properties;

        while (property) {
            DeviceTreeProperty_t* nextProperty = property->Next;

            kfree(property);
            property = nextProperty;
        }
        kfree(node);
        node = nextNode;
    }
    kfree(tree->Blob);
    kfree(tree);
}

oserr_t
DeviceTreeCreate(
    _In_  const void*    blob,
    _In_  uint32_t       size,
    _Out_ DeviceTree_t** treeOut)
{
    struct FDTHeader header;
    DeviceTree_t*    tree;
    unsigned char*   copy;
    oserr_t          status;

    struct __TreeBuilder builder = { 0 };
    struct FdtParser parser = {
        __BeginNode,
        __Property,
        __EndNode,
        &builder
    };

    *treeOut = NULL;
    status = FdtParseHeader(blob, size, &header);
    if (status != OS_EOK) {
        return status;
    }
   
    tree = kmalloc(sizeof(*tree));
    if (!tree) {
        return OS_EOOM;
    }
    memset(tree, 0, sizeof(*tree));
    
    copy = kmalloc(header.TotalSize);
    tree->Blob = copy;
    if (!copy) {
        DeviceTreeDestroy(tree);
        return OS_EOOM;
    }
    
    memcpy(copy, blob, header.TotalSize);
    tree->Size = header.TotalSize;
    tree->Version = header.Version;
    
    builder.Tree = tree;
    
    status = FdtParseStructure(
        copy + header.OffDtStruct,
        header.SizeDtStruct,
        (const char*)copy + header.OffDtStrings,
        header.SizeDtStrings,
        &parser
    );
    if (status != OS_EOK) {
        DeviceTreeDestroy(tree);
        return status;
    }
    
    *treeOut = tree;
    return OS_EOK;
}
