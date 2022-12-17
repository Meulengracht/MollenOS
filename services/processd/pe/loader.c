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
#include <module.h>
#include "private.h"
#include "pe.h"

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
    mstr_delete(resolvedPath);
    if (oserr != OS_EOK) {
        return oserr;
    }

    // prepare the list for dependencies
    list_construct(&moduleMapEntry.Imports);
    oserr = __PostProcessImage(loadContext, moduleMapping, &moduleMapEntry.Imports);
    if (oserr != OS_EOK) {
        return oserr;
    }

    // Only after a successful parse of the image do we insert
    // it into the maps and trees.
    moduleMapEntry.Name = mstr_path_basename(path);
    moduleMapEntry.BaseMapping = moduleMapping->MappingBase;
    moduleMapEntry.Module = moduleMapping->Module;
    moduleMapEntry.Dependency = dependency;
    hashtable_set(&loadContext->ModuleMap, &moduleMapEntry);

    // Cleanup the mappings
    ModuleMappingDelete(moduleMapping);
    return OS_EOK;
}

oserr_t
PEImageLoadLibrary(
        _In_  struct PEImageLoadContext* loadContext,
        _In_  mstring_t*                 libraryPath,
        _Out_ void**                     imageKey)
{
    struct ModuleMapEntry* existingEntry;
    mstring_t*             baseName;
    oserr_t                oserr;

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
        *imageKey = existingEntry->Name;
        return OS_EEXISTS;
    }

    oserr = PEImageLoad(loadContext, libraryPath, false);
    if (oserr != OS_EOK) {
        return oserr;
    }

    // Insert this as a dependency of the root image of the load context.
    // TODO
    return OS_EOK;
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
