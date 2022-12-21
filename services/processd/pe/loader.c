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
#include <string.h>
#include <stdlib.h>

extern oserr_t
__AddImportDependency(
        _In_ list_t*    importsList,
        _In_ mstring_t* moduleName,
        _In_ int        moduleID);

static oserr_t
__PostProcessImage(
        _In_ struct PEImageLoadContext* loadContext,
        _In_ struct ModuleMapping*      moduleMapping,
        _In_ list_t*                    importsList)
{
    oserr_t oserr;
    TRACE("__PostProcessImage()");

    oserr = PEImportsProcess(loadContext, moduleMapping, importsList);
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = PERuntimeRelocationsProcess(moduleMapping);
    if (oserr != OS_EOK) {
        return oserr;
    }
    return OS_EOK;
}

oserr_t
PEImageLoad(
        _In_ struct PEImageLoadContext* loadContext,
        _In_ mstring_t*                 path,
        _In_ bool                       dependency)
{
    struct ModuleMapping* moduleMapping;
    mstring_t*            resolvedPath;
    oserr_t               oserr;
    struct ModuleMapEntry moduleMapEntry;
    TRACE("PEImageLoad(path=%ms)", path);

    oserr = PEResolvePath(
            loadContext,
            path,
            &resolvedPath
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = MapperLoadModule(
            loadContext,
            resolvedPath,
            &moduleMapping
    );
    if (oserr != OS_EOK) {
        mstr_delete(resolvedPath);
        return oserr;
    }

    // Before we process imports and other stuff, we register this module
    // as loaded, and allocate an ID. We want to ensure the root module is set
    // and allocated.
    // Unfortunately doing it like this, we must after processing imports update
    // the list of dependencies for the image.
    moduleMapEntry.ID = PEImageLoadContextGetID(loadContext);
    moduleMapEntry.Name = mstr_path_basename(resolvedPath);
    moduleMapEntry.Path = resolvedPath;
    moduleMapEntry.BaseMapping = moduleMapping->MappingBase;
    moduleMapEntry.Module = moduleMapping->Module;
    moduleMapEntry.Dependency = dependency;
    list_construct(&moduleMapEntry.Imports);
    if (moduleMapEntry.ID == 0) {
        loadContext->RootModule = moduleMapEntry.Name;
    }
    hashtable_set(&loadContext->ModuleMap, &moduleMapEntry);

    // prepare the list for dependencies
    oserr = __PostProcessImage(loadContext, moduleMapping, &moduleMapEntry.Imports);
    if (oserr != OS_EOK) {
        mstr_delete(resolvedPath);
        return oserr;
    }

    // Update the entry by overwriting it
    hashtable_set(&loadContext->ModuleMap, &moduleMapEntry);

    // Cleanup the mappings
    ModuleMappingDelete(moduleMapping);
    return OS_EOK;
}

oserr_t
PEImageUnload(
        _In_ struct PEImageLoadContext* loadContext,
        _In_ void*                      imageKey,
        _In_ bool                       force)
{
    struct ModuleMapEntry* entry;
    list_t                 imports;
    mstring_t*             name;
    mstring_t*             path;

    if (loadContext == NULL || imageKey == NULL) {
        return OS_EINVALPARAMS;
    }

    // Verify library isn't already loaded, and if it is, we should
    // increase the reference count.
    entry = hashtable_get(
            &loadContext->ModuleMap,
            &(struct ModuleMapEntry) {
                    .Name = imageKey
            }
    );
    if (entry == NULL) {
        return OS_EINVALPARAMS;
    }

    // If the module we are trying to unload is marked a dependency, then
    // we *can* only allow it if <force> is set, which it only will be when
    // we unload the entire memory space.
    if (entry->Dependency && !force) {
        return OS_EPERMISSIONS;
    }

    // Store some resources before we unload, as we are going to have a data-race
    // with nested unloads
    name = entry->Name;
    path = entry->Path;
    memcpy(&imports, &entry->Imports, sizeof(list_t));

    // At this point, we allow to unload.
    PEImageLoadContextPutID(loadContext, entry->ID);
    hashtable_remove(
            &loadContext->ModuleMap,
            &(struct ModuleMapEntry) {
                .Name = imageKey
            }
    );

    // Cleanup the ModuleMapEntry resources.
    mstr_delete(path);
    mstr_delete(name);
    foreach_nolink (n, &imports) {
        element_t* tmp = n->next;
        PEImageUnload(loadContext, n->value, force);
        free(n);
        n = tmp;
    }
    return OS_EOK;
}

oserr_t
PEImageLoadLibrary(
        _In_  struct PEImageLoadContext* loadContext,
        _In_  mstring_t*                 libraryPath,
        _Out_ void**                     imageKeyOut,
        _Out_ uintptr_t*                 imageEntryPointOut)
{
    struct ModuleMapEntry* existingEntry;
    mstring_t*             baseName;
    oserr_t                oserr;
    int                    id;

    // We want to know in advance which id is assinged to the loaded image. To do this
    // we allocate and deallocate the id.
    id = PEImageLoadContextGetID(loadContext);
    PEImageLoadContextPutID(loadContext, id);

    // Get basename of path, we use it as the key for the hashtable
    baseName = mstr_path_basename(libraryPath);
    if (baseName == NULL) {
        return OS_EOOM;
    }

    // Verify library isn't already loaded, and if it is, we should
    // increase the reference count.
    existingEntry = hashtable_get(
            &loadContext->ModuleMap,
            &(struct ModuleMapEntry) {
                    .Name = baseName
            }
    );
    if (existingEntry != NULL) {
        mstr_delete(baseName);
        *imageKeyOut = existingEntry->Name;
        *imageEntryPointOut = existingEntry->BaseMapping + existingEntry->Module->EntryPointRVA;
        return OS_EEXISTS;
    }

    oserr = PEImageLoad(loadContext, libraryPath, false);
    if (oserr != OS_EOK) {
        return oserr;
    }

    // Insert this as a dependency of the root image of the load context.
    existingEntry = hashtable_get(
            &loadContext->ModuleMap,
            &(struct ModuleMapEntry) {
                    .Name = loadContext->RootModule
            }
    );
    if (existingEntry == NULL) {
        return OS_EUNKNOWN;
    }

    // Store data from the new entry
    *imageKeyOut = existingEntry->Name;
    *imageEntryPointOut = existingEntry->BaseMapping + existingEntry->Module->EntryPointRVA;

    // NOTE here: we are actually modifying the object directly in the hashtable doing this.
    // If we ever expect this to be remotely safe, we should wrap this in a lock.
    return __AddImportDependency(&existingEntry->Imports, baseName, id);
}

uintptr_t
PEImageFindExport(
        _In_ struct PEImageLoadContext* loadContext,
        _In_ void*                      imageKey,
        _In_ const char*                functionName)
{
    struct ModuleMapEntry*   imageEntry;
    struct ExportedFunction* function;

    imageEntry = hashtable_get(
            &loadContext->ModuleMap,
            &(struct ModuleMapEntry) {
                .Name = imageKey
            }
    );
    if (imageEntry == NULL) {
        return OS_EINVALPARAMS;
    }

    function = hashtable_get(
            &imageEntry->Module->ExportedNames,
            &(struct ExportedFunction) {
                .Name = functionName
            }
    );
    if (function == NULL) {
        return OS_ENOENT;
    }
    return imageEntry->BaseMapping + function->RVA;
}
