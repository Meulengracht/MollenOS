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

#define __TRACE
#include <ddk/utils.h>
#include "private.h"
#include "pe.h"

struct __ModuleMapElement {
    int            ID;
    struct Module* Module;
    int            Vertices[64];
};

struct __ModuleMapEnumContext {
    struct Module* ModuleList;
    int            Index;
};

static uint64_t __hash(const void* element)
{
    const struct __ModuleMapElement* el = element;
    return (uint64_t)el->ID;
}

static int __cmp(const void* element1, const void* element2)
{
    const struct __ModuleMapElement* el1 = element1;
    const struct __ModuleMapElement* el2 = element2;
    return el1->ID == el2->ID ? 0 : -1;
}

static oserr_t
__BuildModuleHashtable(
        _In_ struct PEImageLoadContext* loadContext,
        _In_ hashtable_t*               out)
{

}

void __enum(int index, const void* element, void* userContext)
{
    struct __ModuleMapEnumContext*   context = userContext;
    const struct __ModuleMapElement* mapElement = element;
    _CRT_UNUSED(index);

    // Start by filtering out any previously cleared vertice

    // Clear us if we have no vertices left
}

static oserr_t
__BuildModuleDependencyList(
        _In_ struct PEImageLoadContext* loadContext,
        _In_ struct Module*             moduleList)
{
    hashtable_t                   moduleMap;
    struct __ModuleMapEnumContext context = {
            .ModuleList = moduleList,
            .Index = 0
    };

    // Construct the module map with an identical size to the module map
    // in load context.
    hashtable_construct(
            &moduleMap,
            loadContext->ModuleMap.capacity,
            sizeof(struct __ModuleMapElement),
            __hash, __cmp
    );

    // Iterate through the hashmap, removing entries as we go that has no external
    // dependencies.
    hashtable_enumerate(&moduleMap, __enum, &context);
    return OS_EOK;
}

oserr_t
PeGetModuleHandles(
    _In_  PeExecutable_t* executable,
    _Out_ Handle_t*       moduleList,
    _Out_ int*            moduleCount)
{
    int maxCount;
    int index;

    if (!executable || !moduleList || !moduleCount) {
        return OS_EINVALPARAMS;
    }
    
    index    = 0;
    maxCount = *moduleCount;

    // Copy base over
    moduleList[index++] = (Handle_t)executable->VirtualAddress;
    if (executable->Libraries != NULL) {
        foreach(i, executable->Libraries) {
            PeExecutable_t *Library = i->value;
            moduleList[index++]     = (Handle_t)Library->VirtualAddress;
        
            if (index == maxCount) {
                break;
            }
        }
    }
    
    *moduleCount = index;
    return OS_EOK;
}

oserr_t
PeGetModuleEntryPoints(
    _In_  PeExecutable_t* executable,
    _Out_ Handle_t*       moduleList,
    _Out_ int*            moduleCount)
{
    int maxCount;
    int index;

    if (!executable || !moduleList || !moduleCount) {
        return OS_EINVALPARAMS;
    }
    
    index    = 0;
    maxCount = *moduleCount;

    if (executable->Libraries != NULL) {
        foreach(i, executable->Libraries) {
            PeExecutable_t *Library = i->value;
            
            dstrace("[pe] [get_modules] %i: library %ms => 0x%" PRIxIN,
                index, Library->Name, Library->EntryAddress);
            if (Library->EntryAddress != 0) {
                moduleList[index++] = (Handle_t)Library->EntryAddress;
            }
        
            if (index == maxCount) {
                break;
            }
        }
    }
    
    *moduleCount = index;
    return OS_EOK;
}
