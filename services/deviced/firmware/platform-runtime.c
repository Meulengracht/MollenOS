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

#include <firmware/platform-runtime.h>
#include <ddk/utils.h>
#include <string.h>
#include <stdio.h>

struct __PlatformRam {
    DmDmaMap_t Map;
    oserr_t    Status;
};

static int
__PlatformHasManagedProperties(
    _In_ const struct FdtNode* node)
{
    for (size_t i = 0; i < node->PropertyCount; i++) {
        const char* name = node->Properties[i].Name;
        size_t      length = strlen(name);
        
        if (!strcmp(name, "clocks") || !strcmp(name, "resets") || !strcmp(name, "power-domains")) {
            return 1;
        }

        if (!strcmp(name, "phys") || !strcmp(name, "gpios") || !strncmp(name, "pinctrl-", 8)) {
            return 1;
        }
        
        if (!strncmp(name, "assigned-clock", 14) ||
            (length >= 7 && !strcmp(name + length - 7, "-supply")) ||
            (length >= 6 && !strcmp(name + length - 6, "-gpios"))) {
            return 1;
        }
    }
    return 0;
}

static oserr_t
__PlatformDependencyAppend(
    _InOut_ struct FdtPlatformDependencies*     plan,
    _In_    const struct FdtPlatformDependency* dependency)
{
    if (plan->Count == FDT_PLATFORM_MAX_DEPENDENCIES) {
        ERROR("__PlatformDependencyAppend: dependency of kind %u exceeds the %u entry limit",
              (uint32_t)dependency->Kind, (uint32_t)FDT_PLATFORM_MAX_DEPENDENCIES);
        return OS_ENOTSUPPORTED;
    }
    plan->Entries[plan->Count++] = *dependency;
    return OS_EOK;
}

static oserr_t
__PlatformReferences(
    _In_    const struct FdtTree*           tree,
    _In_    const struct FdtResources*      node,
    _In_    const char*                     property,
    _In_    const char*                     cells,
    _In_    enum FdtPlatformDependencyKind  kind,
    _InOut_ struct FdtPlatformDependencies* plan)
{
    const uint8_t* bytes;
    uint32_t       length;
    uint32_t       offset = 0;

    bytes = FdtProperty(&node->View, property, &length);
    while (offset < length) {
        struct FdtPlatformDependency dependency;
        struct FdtResources          resource;
        const uint8_t*               arguments;
        uint32_t                     count;
        oserr_t                      status;
        
        status = FdtNextReference(
            tree,
            bytes,
            length,
            cells,
            0,
            &offset,
            &resource,
            &arguments
        );
        if (status != OS_EOK) {
            ERROR("__PlatformReferences: node %u has an invalid %s entry at byte %u (%u)",
                  node->NodeOffset, property, offset, status);
            return status;
        }

        status = FdtScalar(&resource.View, cells, &count);
        if (status != OS_EOK) {
            ERROR("__PlatformReferences: node %u %s resource %u has no %s (%u)",
                  node->NodeOffset, property, resource.NodeOffset, cells, status);
            return status;
        }

        if (count > FDT_PLATFORM_MAX_ARGUMENTS) {
            ERROR("__PlatformReferences: node %u %s resource %u needs %u arguments, limit is %u",
                  node->NodeOffset, property, resource.NodeOffset, count,
                  (uint32_t)FDT_PLATFORM_MAX_ARGUMENTS);
            return OS_ENOTSUPPORTED;
        }

        memset(&dependency, 0, sizeof(struct FdtPlatformDependency));
        dependency.Kind = kind;
        dependency.NodeOffset = resource.NodeOffset;
        dependency.ArgumentCount = count;
        
        for (uint32_t i = 0; i < count; i++) {
            dependency.Arguments[i] = FdtReadBe32(arguments + 4 * i);
        }

        if (kind == FdtPlatformClock && FdtCompatible(&resource.View, "fixed-clock")) {
            uint32_t rate;

            status = FdtScalar(&resource.View, "clock-frequency", &rate);
            if (status != OS_EOK) {
                ERROR("__PlatformReferences: fixed-clock %u has no clock-frequency (%u)",
                      resource.NodeOffset, status);
                return OS_EINVALPARAMS;
            }

            if (!rate || count != 0) {
                ERROR("__PlatformReferences: fixed-clock %u has rate %u and %u cells",
                      resource.NodeOffset, rate, count);
                return OS_EINVALPARAMS;
            }
            
            // A fixed clock needs no runtime control only when it has no gates
            // or dependencies. GPIO-gated and programmable clocks need a driver.
            if (!__PlatformHasManagedProperties(&resource.View)) {
                dependency.Fixed = 1;
            }
        }

        status = __PlatformDependencyAppend(plan, &dependency);
        if (status != OS_EOK) {
            ERROR("__PlatformReferences: node %u could not record %s resource %u (%u)",
                  node->NodeOffset, property, resource.NodeOffset, status);
            return status;
        }
    }
    return OS_EOK;
}

static oserr_t
__PlatformSupplies(
    _In_    const struct FdtTree*           tree,
    _In_    const struct FdtResources*      node,
    _InOut_ struct FdtPlatformDependencies* plan)
{
    struct FdtResources          resource;
    struct FdtPlatformDependency dependency = { .Kind = FdtPlatformSupply };
    oserr_t                      status;

    for (size_t i = 0; i < node->View.PropertyCount; i++) {
        const struct FdtProperty* property = &node->View.Properties[i];
        size_t                    length = strlen(property->Name);
        
        if (length <= 7 || strcmp(property->Name + length - 7, "-supply") != 0) {
            continue;
        }
        
        if (property->Length != 4) {
            ERROR("__PlatformSupplies: node %u property %s has length %u, expected 4",
                  node->NodeOffset, property->Name, property->Length);
            return OS_EINVALPARAMS;
        }

        status = FdtFindResources(tree, FdtReadBe32(property->Value), &resource);
        if (status != OS_EOK) {
            ERROR("__PlatformSupplies: node %u property %s references an unknown node (%u)",
                  node->NodeOffset, property->Name, status);
            return status;
        }
        
        dependency.NodeOffset = resource.NodeOffset;
        status = __PlatformDependencyAppend(plan, &dependency);
        if (status != OS_EOK) {
            ERROR("__PlatformSupplies: node %u could not record supply resource %u (%u)",
                  node->NodeOffset, resource.NodeOffset, status);
            return status;
        }
    }
    return OS_EOK;
}

static oserr_t
__PlatformPins(
    _In_    const struct FdtTree*           tree,
    _In_    const struct FdtResources*      node,
    _InOut_ struct FdtPlatformDependencies* plan)
{
    char           property[32];
    const uint8_t* names;
    const uint8_t* states;
    uint32_t       length;
    uint32_t       offset = 0;
    uint32_t       index = 0;
    uint32_t       defaultIndex = 0;
    int            found = 0;
    oserr_t        status;

    names = FdtProperty(&node->View, "pinctrl-names", &length);
    while (offset < length) {
        const char* end = memchr(names + offset, 0, length - offset);
        if (end == NULL || end == (const char*)names + offset) {
            ERROR("__PlatformPins: node %u has a malformed pinctrl-names entry at byte %u",
                  node->NodeOffset, offset);
            return OS_EINVALPARAMS;
        }

        if (strcmp((const char*)names + offset, "default") == 0) {
            if (found) {
                ERROR("__PlatformPins: node %u lists more than one default pinctrl state",
                      node->NodeOffset);
                return OS_EINVALPARAMS;
            }
            defaultIndex = index;
            found = 1;
        }
        offset = (uint32_t)(end - (const char*)names) + 1;
        index++;
    }
    if (names != NULL && !found) {
        // Do not guess which named state would make a device safe to start.
        ERROR("__PlatformPins: node %u has no default pinctrl state", node->NodeOffset);
        return OS_ENOTSUPPORTED;
    }

    snprintf(property, sizeof(property), "pinctrl-%u", defaultIndex);
    states = FdtProperty(&node->View, property, &length);
    if (states == NULL) {
        if (names != NULL) {
            ERROR("__PlatformPins: node %u names a default state but has no %s",
                  node->NodeOffset, property);
            return OS_EINVALPARAMS;
        }
        return OS_EOK;
    }

    if (length & 3) {
        ERROR("__PlatformPins: node %u property %s has length %u, expected a multiple of 4",
              node->NodeOffset, property, length);
        return OS_EINVALPARAMS;
    }

    for (offset = 0; offset < length; offset += 4) {
        struct FdtPlatformDependency dependency;
        struct FdtResources          state;
        const struct FdtResources*   parent;
        
        status = FdtFindResources(tree, FdtReadBe32(states + offset), &state);
        if (status != OS_EOK) {
            ERROR("__PlatformPins: node %u property %s references an unknown node at byte %u (%u)",
                  node->NodeOffset, property, offset, status);
            return status;
        }

        parent = FdtTreeParent(tree, state.NodeOffset);
        while (parent != NULL && parent->Compatible == NULL) {
            parent = FdtTreeParent(tree, parent->NodeOffset);
        }

        if (parent == NULL) {
            ERROR("__PlatformPins: node %u pin state %u has no controller",
                  node->NodeOffset, state.NodeOffset);
            return OS_ENOTSUPPORTED;
        }

        dependency.Kind = FdtPlatformPins;
        dependency.NodeOffset = parent->NodeOffset;
        dependency.Arguments[0] = state.NodeOffset;
        dependency.ArgumentCount = 1;
        
        status = __PlatformDependencyAppend(plan, &dependency);
        if (status != OS_EOK) {
            ERROR("__PlatformPins: node %u could not record pin controller %u (%u)",
                  node->NodeOffset, parent->NodeOffset, status);
            return status;
        }
    }
    return OS_EOK;
}

static oserr_t
__PlatformNodeDependencies(
    _In_    const struct FdtTree*           tree,
    _In_    const struct FdtResources*      resource,
    _InOut_ struct FdtPlatformDependencies* plan)
{
    uint32_t length;
    oserr_t  status;

    if (resource == NULL || resource->Malformed || resource->ParentMalformed) {
        ERROR("__PlatformNodeDependencies: node is missing or malformed");
        return OS_EINVALPARAMS;
    }
    
    if (resource->Disabled || resource->ParentDisabled) {
        TRACE("__PlatformNodeDependencies: node %u is disabled", resource->NodeOffset);
        return OS_ENOENT;
    }
    
    if (FdtProperty(&resource->View, "assigned-clocks", &length) != NULL ||
        FdtProperty(&resource->View, "assigned-clock-parents", &length) != NULL ||
        FdtProperty(&resource->View, "assigned-clock-rates", &length) != NULL) {
        ERROR("__PlatformNodeDependencies: node %u needs assigned-clock setup",
              resource->NodeOffset);
        return OS_ENOTSUPPORTED;
    }

    // GPIO-controlled resets, enables, and legacy PHY references require a
    // driver that understands their binding before the device can start.
    for (size_t i = 0; i < resource->View.PropertyCount; i++) {
        const char* name = resource->View.Properties[i].Name;
        size_t      nameLength = strlen(name);
        
        if (!strcmp(name, "gpios") || !strcmp(name, "phy-handle") || !strcmp(name, "usb-phy")) {
            ERROR("__PlatformNodeDependencies: node %u property %s needs a driver",
                  resource->NodeOffset, name);
            return OS_ENOTSUPPORTED;
        }
        
        if (nameLength >= 6 && !strcmp(name + nameLength - 6, "-gpios")) {
            ERROR("__PlatformNodeDependencies: node %u property %s needs a driver",
                  resource->NodeOffset, name);
            return OS_ENOTSUPPORTED;
        }
    }
    
    status = __PlatformReferences(
        tree,
        resource,
        "power-domains",
        "#power-domain-cells",
        FdtPlatformPower,
        plan
    );
    if (status != OS_EOK) {
        ERROR("__PlatformNodeDependencies: node %u power domains rejected (%u)",
              resource->NodeOffset, status);
        return status;
    }

    status = __PlatformSupplies(tree, resource, plan);
    if (status != OS_EOK) {
        ERROR("__PlatformNodeDependencies: node %u supplies rejected (%u)",
              resource->NodeOffset, status);
        return status;
    }

    status = __PlatformReferences(
        tree,
        resource,
        "clocks",
        "#clock-cells",
        FdtPlatformClock,
        plan
    );
    if (status != OS_EOK) {
        ERROR("__PlatformNodeDependencies: node %u clocks rejected (%u)",
              resource->NodeOffset, status);
        return status;
    }

    status = __PlatformReferences(
        tree,
        resource,
        "resets",
        "#reset-cells",
        FdtPlatformReset,
        plan
    );
    if (status != OS_EOK) {
        ERROR("__PlatformNodeDependencies: node %u resets rejected (%u)",
              resource->NodeOffset, status);
        return status;
    }

    status = __PlatformPins(tree, resource, plan);
    if (status != OS_EOK) {
        ERROR("__PlatformNodeDependencies: node %u pin control rejected (%u)",
              resource->NodeOffset, status);
        return status;
    }

    status = __PlatformReferences(
        tree,
        resource,
        "phys",
        "#phy-cells",
        FdtPlatformPhy,
        plan
    );
    if (status != OS_EOK) {
        ERROR("__PlatformNodeDependencies: node %u phys rejected (%u)",
              resource->NodeOffset, status);
        return status;
    }

    return OS_EOK;
}

oserr_t
FdtPlatformDependencies(
    _In_  const struct FdtTree*           tree,
    _In_  uint32_t                        node,
    _Out_ struct FdtPlatformDependencies* dependencies)
{
    const struct FdtResources*     parentNodes[FDT_MAX_DEPTH];
    const struct FdtResources*     resource = FdtTreeNode(tree, node);
    struct FdtPlatformDependencies ldeps = { 0 };
    uint32_t                       count = 0;

    if (resource == NULL) {
        ERROR("FdtPlatformDependencies: node %u does not exist", node);
        return OS_ENOENT;
    }

    while (resource != NULL) {
        if (count == FDT_MAX_DEPTH) {
            ERROR("FdtPlatformDependencies: node %u is nested deeper than %u levels",
                  node, (uint32_t)FDT_MAX_DEPTH);
            return OS_EINVALPARAMS;
        }
        parentNodes[count++] = resource;
        resource = FdtTreeParent(tree, resource->NodeOffset);
    }

    // Simple-bus parents can also require power or clocks. List their dependencies
    // first so runtime setup enables them before the child's dependencies.
    while (count) {
        oserr_t status = __PlatformNodeDependencies(tree, parentNodes[--count], &ldeps);
        if (status != OS_EOK) {
            ERROR("FdtPlatformDependencies: node %u rejected at tree depth %u (%u)",
                  node, count, status);
            return status;
        }
    }

    memcpy(dependencies, &ldeps, sizeof(struct FdtPlatformDependencies));
    return OS_EOK;
}

static void
__PlatformCollectRam(
    _In_    const struct FdtResources* nodes,
    _In_    int                        depth,
    _InOut_ void*                      context)
{
    struct __PlatformRam*      ram = context;
    const struct FdtResources* node = &nodes[depth];
    uint32_t                   stride;

    if (!node->IsMemory || node->Disabled || node->ParentDisabled) {
        return;
    }
    
    if (ram->Status != OS_EOK) {
        return;
    }
    
    if (!depth || node->Malformed || node->ParentMalformed) {
        ERROR("__PlatformCollectRam: memory node %u is at the root or malformed",
              node->NodeOffset);
        ram->Status = OS_EINVALPARAMS;
        return;
    }
    
    stride = (node->ParentAddressCells + node->ParentSizeCells) * 4;
    if (!stride || !node->RegLength || node->RegLength % stride) {
        ERROR("__PlatformCollectRam: memory node %u has reg length %u for a stride of %u",
              node->NodeOffset, node->RegLength, stride);
        ram->Status = OS_EINVALPARAMS;
        return;
    }

    for (uint32_t i = 0; i < node->RegLength / stride; i++) {
        uint64_t base;
        uint64_t length;

        ram->Status = FdtRawRegister(node, i, &base, &length);
        if (ram->Status != OS_EOK) {
            ERROR("__PlatformCollectRam: memory node %u range %u cannot be read (%u)",
                  node->NodeOffset, i, ram->Status);
            return;
        }
        
        if (!length || length - 1 > UINT64_MAX - base ||
            !FdtTranslateAddress(nodes, depth - 1, length, &base, 0)) {
            ERROR("__PlatformCollectRam: memory node %u range %u cannot be translated",
                  node->NodeOffset, i);
            ram->Status = OS_EINVALPARAMS;
            return;
        }

        if (ram->Map.Count == DM_DMA_MAX_RANGES) {
            ERROR("__PlatformCollectRam: memory node %u exceeds the %u range limit",
                  node->NodeOffset, (uint32_t)DM_DMA_MAX_RANGES);
            ram->Status = OS_ENOTSUPPORTED;
            return;
        }

        ram->Map.Ranges[ram->Map.Count++] = (DmDmaRange_t) { 
            base,
            base,
            length
        };
    }
}

oserr_t
FdtPlatformDma(
    _In_  const struct FdtTree* tree,
    _In_  uint32_t              node,
    _Out_ DmDmaMap_t*           map)
{
    const struct FdtResources* parentNodes[FDT_MAX_DEPTH];
    const struct FdtResources* resource = FdtTreeNode(tree, node);
    DmDmaMap_t                 result;
    uint32_t                   count = 0;
    oserr_t                    status;
    struct __PlatformRam       ram = { 0 };

    struct FdtDmaRanges ranges = {
        .State = FdtDmaPropertyIdentity,
        .ChildLimit = UINT64_MAX
    };

    if (resource == NULL) {
        ERROR("FdtPlatformDma: node %u does not exist", node);
        return OS_ENOENT;
    }

    while (resource != NULL) {
        uint32_t length;

        if (count == FDT_MAX_DEPTH) {
            ERROR("FdtPlatformDma: node %u is nested deeper than %u levels",
                  node, (uint32_t)FDT_MAX_DEPTH);
            return OS_EINVALPARAMS;
        }

        if (resource->Malformed || resource->ParentMalformed) {
            ERROR("FdtPlatformDma: node %u has a malformed parent %u",
                  node, resource->NodeOffset);
            return OS_EINVALPARAMS;
        }
        
        if (resource->Disabled || resource->ParentDisabled) {
            TRACE("FdtPlatformDma: node %u is disabled by parent %u",
                  node, resource->NodeOffset);
            return OS_ENOENT;
        }
        
        if (resource->IsPci) {
            ERROR("FdtPlatformDma: node %u sits below PCI host %u",
                  node, resource->NodeOffset);
            return OS_ENOTSUPPORTED;
        }
        
        if (FdtProperty(&resource->View, "iommus", &length) != NULL ||
            FdtProperty(&resource->View, "iommu-map", &length) != NULL) {
            ERROR("FdtPlatformDma: node %u is translated by an IOMMU at node %u",
                  node, resource->NodeOffset);
            return OS_ENOTSUPPORTED;
        }
        
        if (FdtProperty(&resource->View, "dma-parent", &length) != NULL) {
            ERROR("FdtPlatformDma: node %u uses dma-parent at node %u",
                  node, resource->NodeOffset);
            return OS_ENOTSUPPORTED;
        }
        
        parentNodes[count++] = resource;
        resource = FdtTreeParent(tree, resource->NodeOffset);
    }

    status = FdtWalkResources(tree, __PlatformCollectRam, &ram);
    if (status != OS_EOK) {
        ERROR("FdtPlatformDma: node %u could not walk the tree for RAM (%u)", node, status);
        return status;
    }

    if (ram.Status != OS_EOK) {
        ERROR("FdtPlatformDma: node %u found unusable RAM ranges (%u)", node, ram.Status);
        return ram.Status;
    }
    
    if (!ram.Map.Count) {
        ERROR("FdtPlatformDma: node %u found no RAM ranges", node);
        return OS_ENOENT;
    }

    // Validate RAM overlap and sort ranges before applying bus DMA translations.
    status = FdtComposeDmaRanges(&ranges, &ram.Map, &result);
    if (status != OS_EOK) {
        ERROR("FdtPlatformDma: node %u could not compose the RAM ranges (%u)", node, status);
        return status;
    }

    for (uint32_t i = count - 1; i > 1; i--) {
        resource = parentNodes[i - 1];
        if (!resource->IsSimpleBus) {
            ERROR("FdtPlatformDma: node %u has a non simple-bus parent %u",
                  node, resource->NodeOffset);
            return OS_ENOTSUPPORTED;
        }

        status = FdtDecodeDmaRanges(
            resource,
            FdtDmaAddressSimple,
            FdtDmaAddressSimple,
            &ranges
        );
        if (status != OS_EOK) {
            ERROR("FdtPlatformDma: node %u could not decode dma-ranges of parent %u (%u)",
                  node, resource->NodeOffset, status);
            return status;
        }

        status = FdtComposeDmaRanges(&ranges, &result, &result);
        if (status != OS_EOK) {
            ERROR("FdtPlatformDma: node %u could not apply dma-ranges of parent %u (%u)",
                  node, resource->NodeOffset, status);
            return status;
        }
    }

    *map = result;
    return OS_EOK;
}
