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
#define __need_minmax
#include <assert.h>
#include <ds/ds.h>
#include <ds/hashtable.h>
#include <ds/list.h>
#include <ds/mstring.h>
#include <ddk/utils.h>
#include <os/memory.h>
#include <os/usched/mutex.h>
#include <string.h>
#include <stdlib.h>
#include "pe.h"

struct ModuleMapping {
    uintptr_t MappedAddress;
    uint8_t*  LocalAddress;
    uintptr_t RVA;
    size_t    Length;
};

struct Section {
    element_t ListHeader;
    bool      Valid;
    bool      Zero;
    uintptr_t Address;
    size_t    Length;
    uint32_t  MapFlags;
};

struct Module {
    list_t Sections;
};

struct __PathEntry {
    mstring_t* Path;
    uint32_t   ModuleHash;
};

struct __ModuleEntry {
    uint32_t       Hash;
    int            References;
    struct Module* Module;
};

struct MappingManager {
    hashtable_t       Paths;    // hashtable<string, hash>
    struct usched_mtx PathsMutex;
    hashtable_t       Modules;  // hashtable<hash, module>
    struct usched_mtx ModulesMutex;
    uuid_t            MemorySpace;
};

static struct MappingManager g_mapper = { 0 };

oserr_t
MapperInitialize(void)
{

}

void
MapperDestroy(void)
{

}

static oserr_t
__GetModuleHash(
        _In_  mstring_t* path,
        _Out_ uint32_t*  hashOut)
{
    struct __PathEntry* entry;
    oserr_t             oserr = OS_ENOENT;
    usched_mtx_lock(&g_mapper.PathsMutex);
    entry = hashtable_get(&g_mapper.Paths, &(struct __PathEntry) { .Path = path });
    if (entry) {
        *hashOut = entry->ModuleHash;
        oserr = OS_EOK;
    }
    usched_mtx_unlock(&g_mapper.PathsMutex);
    return oserr;
}

static oserr_t
__GetModule(
        _In_  uint32_t        hash,
        _Out_ struct Module** moduleOut)
{
    struct __ModuleEntry* entry;
    oserr_t               oserr = OS_ENOENT;
    usched_mtx_lock(&g_mapper.ModulesMutex);
    entry = hashtable_get(&g_mapper.Modules, &(struct __ModuleEntry) { .Hash = hash });
    if (entry) {
        entry->References++;
        *moduleOut = entry->Module;
        oserr = OS_EOK;
    }
    usched_mtx_unlock(&g_mapper.ModulesMutex);
    return oserr;
}

static oserr_t
__LoadModule(
        _In_  mstring_t* path,
        _Out_ uint32_t*  hashOut)
{


    return OS_EOK;
}

static oserr_t
__MapSection(
        _In_    struct Section*       section,
        _In_    uuid_t                memorySpace,
        _InOut_ uintptr_t*            loadAddress,
        _In_    struct ModuleMapping* mapping)
{

}

static oserr_t
__MapModule(
        _In_    struct Module*         module,
        _In_    uuid_t                 memorySpace,
        _InOut_ uintptr_t*             loadAddress,
        _Out_   struct ModuleMapping** mappingsOut)
{
    struct ModuleMapping* mappings;
    int                   mappingCount;
    int                   j = 0;

    mappingCount = list_count(&module->Sections);
    mappings = malloc(sizeof(struct ModuleMapping) * mappingCount);
    if (mappings == NULL) {
        return OS_EOOM;
    }
    memset(mappings, 0, sizeof(struct ModuleMapping) * mappingCount);

    foreach (i, &module->Sections) {
        struct Section* section = i->value;
        if (section->Valid) {
            oserr_t oserr = __MapSection(section, memorySpace, loadAddress, &mappings[j]);
            if (oserr != OS_EOK) {
                free(mappings);
                return oserr;
            }
            j++;
        }
    }
    *mappingsOut = mappings;
    return OS_EOK;
}

oserr_t
MapperLoadModule(
        _In_    mstring_t*             path,
        _In_    uuid_t                 memorySpace,
        _InOut_ uintptr_t*             loadAddress,
        _Out_   uint32_t*              moduleKeyOut,
        _Out_   struct ModuleMapping** mappingsOut)
{
    uint32_t       moduleHash;
    struct Module* module;
    oserr_t        oserr;
    TRACE("MapperLoadModule(path=%ms)", path);

    if (path == NULL || loadAddress == NULL || moduleKeyOut == NULL) {
        return OS_EINVALPARAMS;
    }

    oserr = __GetModuleHash(path, &moduleHash);
    if (oserr != OS_EOK) {
        TRACE("MapperLoadModule module path entry was not stored, loading...");
        // path was not found, instantiate a load of the library
        oserr = __LoadModule(path, &moduleHash);
        if (oserr != OS_EOK) {
            ERROR("MapperLoadModule failed to load module: %u", oserr);
            return oserr;
        }
    }

    oserr = __GetModule(moduleHash, &module);
    if (oserr != OS_EOK) {
        // should not happen at this point
        ERROR("MapperLoadModule failed to find module hash (0x%x): %u", moduleHash, oserr);
        return oserr;
    }

    // Store the module hash so the loader can reduce the reference
    // count again later
    *moduleKeyOut = moduleHash;

    // Map the module into the memory space provided
    return __MapModule(module, memorySpace, loadAddress, mappingsOut);
}

oserr_t
MapperUnloadModule(
        _In_ uint32_t moduleKey)
{
    struct __ModuleEntry* entry;
    oserr_t               oserr = OS_ENOENT;
    usched_mtx_lock(&g_mapper.ModulesMutex);
    entry = hashtable_get(&g_mapper.Modules, &(struct __ModuleEntry) { .Hash = moduleKey });
    if (entry) {
        entry->References--;
        oserr = OS_EOK;
    }
    usched_mtx_unlock(&g_mapper.ModulesMutex);
    return oserr;
}
