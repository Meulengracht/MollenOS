/**
 * Copyright 2022, Philip Meulengracht
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

//#define __TRACE
#include <ddk/utils.h>
#include <module.h>
#include "private.h"
#include "pe.h"
#include <stdlib.h>
#include <string.h>

struct __ModuleElement {
    struct Module* Module;
    uintptr_t      BaseMapping;
    void*          ModuleKey;
};

struct __ModuleMapElement {
    int            ID;
    struct Module* Module;
    uintptr_t      BaseMapping;
    void*          ModuleKey;
    bool           Vertices[PROCESS_MAXMODULES];
    int            VerticeCount;
};

struct __ModuleMapEnumContext {
    struct __ModuleMapElement* Elements;
    int                        Index;
};

void __enum(int index, const void* element, void* userContext)
{
    struct __ModuleMapEnumContext* context = userContext;
    const struct ModuleMapEntry*   entry   = element;
    _CRT_UNUSED(index);

    // Create a new element at elements[index]
    context->Elements[context->Index].ID = entry->ID;
    context->Elements[context->Index].Module = entry->Module;
    context->Elements[context->Index].BaseMapping = entry->BaseMapping;
    context->Elements[context->Index].ModuleKey = entry->Name;
    context->Elements[context->Index].VerticeCount = 0;
    memset(
            &context->Elements[context->Index].Vertices[0],
            0,
            sizeof(bool) * PROCESS_MAXMODULES
    );

    // Go over all imports and mark their respective id's
    foreach (n, &entry->Imports) {
        int importID = (int)(uintptr_t)n->key;
        context->Elements[context->Index].Vertices[importID] = true;
        context->Elements[context->Index].VerticeCount++;
    }
    context->Index++;
}

// __BuildModuleDependencyList converts the following tree structure
//               foo.app
//                /   \           \
//             c.dll  gracht.dll   bar.dll
//             /                    \
//          some.dll                c.dll
// into the following linear structure, where dependencies are resolved such
// that we get a dependency-free list where the first element represents some.dll that
// contains no dependencies.
// [some.dll, c.dll, gracht.dll, bar.dll, foo.app]
static struct __ModuleElement*
__BuildModuleDependencyList(
        _In_ struct PEImageLoadContext* loadContext)
{
    struct __ModuleMapEnumContext context;
    struct __ModuleElement*       moduleList;
    int                           elementCount;
    int                           mli;
    TRACE("__BuildModuleDependencyList()");

    // Allocate the result structure, we can preallocate the correct memory as
    // we know the number of output elements.
    moduleList = malloc(sizeof(struct __ModuleElement) * loadContext->ModuleMap.element_count);
    if (moduleList == NULL) {
        return NULL;
    }

    // Construct the module map with an identical size to the module map
    // in load context.
    context.Elements = malloc(sizeof(struct __ModuleMapElement) * loadContext->ModuleMap.element_count);
    if (context.Elements == NULL) {
        free(moduleList);
        return NULL;
    }
    context.Index = 0;

    // Build the element list up with correct list of vertices.
    TRACE("__BuildModuleDependencyList preparing module list");
    hashtable_enumerate(&loadContext->ModuleMap, __enum, &context);

    // Run over the list, removing freestanding elements, and then removing
    // those from vertices untill nothing's left.
    TRACE("__BuildModuleDependencyList building dependency list of %u items",
          (uint32_t)loadContext->ModuleMap.element_count);
    mli = 0;
    elementCount = (int)loadContext->ModuleMap.element_count;
    while (elementCount > 0) {
        for (size_t i = 0; i < loadContext->ModuleMap.element_count; i++) {
            if (context.Elements[i].ID == -1) {
                continue;
            }
            TRACE("__BuildModuleDependencyList checking %i: vertices left: %i",
                  context.Elements[i].ID, context.Elements[i].VerticeCount);

            if (context.Elements[i].VerticeCount == 0) {
                // we can remove this one and remove any reference to this in
                // other elements, unfortunately requires us to re-iterate.
                for (size_t j = 0; j < loadContext->ModuleMap.element_count; j++) {
                    if (j == i || context.Elements[j].ID == -1) {
                        continue;
                    }
                    if (context.Elements[j].Vertices[context.Elements[i].ID]) {
                        context.Elements[j].Vertices[context.Elements[i].ID] = false;
                        context.Elements[j].VerticeCount--;
                    }
                }

                // insert into module list
                moduleList[mli].ModuleKey = context.Elements[i].ModuleKey;
                moduleList[mli].BaseMapping = context.Elements[i].BaseMapping;
                moduleList[mli].Module = context.Elements[i].Module;
                context.Elements[i].ID = -1;
                elementCount--;
                mli++;
            }
        }
    }
    free(context.Elements);
    return moduleList;
}

oserr_t
PEModuleKeys(
        _In_  struct PEImageLoadContext* loadContext,
        _Out_ Handle_t*                  moduleKeys,
        _Out_ int*                       moduleCountOut)
{
    struct __ModuleElement* moduleList;
    TRACE("PEModuleKeys()");

    if (loadContext == NULL || moduleKeys == NULL || moduleCountOut == NULL) {
        return OS_EINVALPARAMS;
    }

    moduleList = __BuildModuleDependencyList(loadContext);
    if (moduleList == NULL) {
        return OS_EOOM;
    }

    for (size_t i = 0; i < loadContext->ModuleMap.element_count; i++) {
        moduleKeys[i] = moduleList[i].ModuleKey;
    }
    free(moduleList);
    *moduleCountOut = (int)loadContext->ModuleMap.element_count;
    return OS_EOK;
}

oserr_t
PEModuleEntryPoints(
        _In_  struct PEImageLoadContext* loadContext,
        _Out_ uintptr_t*                 moduleAddresses,
        _Out_ int*                       moduleCountOut)
{
    struct __ModuleElement* moduleList;
    TRACE("PEModuleEntryPoints()");

    if (loadContext == NULL || moduleAddresses == NULL || moduleCountOut == NULL) {
        return OS_EINVALPARAMS;
    }

    moduleList = __BuildModuleDependencyList(loadContext);
    if (moduleList == NULL) {
        return OS_EOOM;
    }

    for (size_t i = 0; i < loadContext->ModuleMap.element_count; i++) {
        moduleAddresses[i] = moduleList[i].BaseMapping + moduleList[i].Module->EntryPointRVA;
        TRACE("PEModuleEntryPoints entry 0x%" PRIxIN, moduleAddresses[i]);
    }
    free(moduleList);
    *moduleCountOut = (int)loadContext->ModuleMap.element_count;
    return OS_EOK;
}
