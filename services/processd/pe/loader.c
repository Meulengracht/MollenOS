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
#include "mapper.h"
#include <string.h>

struct PEImage {
};

static oserr_t
__ResolveImport(
        _In_ struct PEImageLoadContext* loadContext,
        _In_ mstring_t*                 moduleName,
        _In_ struct ModuleMapEntry*     moduleMapEntry)
{
    struct ModuleMapEntry* mapEntry;
    oserr_t                oserr;

    mapEntry = hashtable_get(
            &loadContext->ModuleMap,
            &(struct ModuleMapEntry) { .Name = moduleName }
    );
    if (mapEntry) {
        memcpy(moduleMapEntry, mapEntry, sizeof(struct ModuleMapEntry));
        return OS_EOK;
    }

    // We need to load the module into the namespace
    oserr = PEImageLoad(loadContext, NULL, moduleName, NULL);
    if (oserr != OS_EOK) {
        return oserr;
    }
    return OS_EOK;
}

static inline oserr_t
__GetFunctionByOrdinal(
        _In_  struct ModuleMapEntry* moduleMapEntry,
        _In_  uint32_t               ordinal,
        _Out_ uint32_t*              functionRVA)
{
    struct ExportedFunction* function;

    function = hashtable_get(
            &moduleMapEntry->Module->ExportedOrdinals,
            &(struct ExportedFunction) {
                .Ordinal = (int)ordinal
            }
    );
    if (function == NULL) {
        return OS_ENOENT;
    }
    if (function->ForwardName != NULL) {
        ERROR("__GetFunctionByOrdinal encounted forwarded export: %s", function->ForwardName);
        return OS_ENOTSUPPORTED;
    }
    *functionRVA = function->RVA;
    return OS_EOK;
}

static inline oserr_t
__GetFunctionByName(
        _In_  struct ModuleMapEntry* moduleMapEntry,
        _In_  const char*            name,
        _Out_ uint32_t*              functionRVA)
{
    struct ExportedFunction* function;

    function = hashtable_get(
            &moduleMapEntry->Module->ExportedOrdinals,
            &(struct ExportedFunction) {
                    .Name = name
            }
    );
    if (function == NULL) {
        return OS_ENOENT;
    }
    if (function->ForwardName != NULL) {
        ERROR("__GetFunctionByOrdinal encounted forwarded export: %s", function->ForwardName);
        return OS_ENOTSUPPORTED;
    }
    *functionRVA = function->RVA;
    return OS_EOK;
}

static oserr_t
__ProcessUnboundImportTable32(
        _In_ struct ModuleMapEntry* moduleMapEntry,
        _In_ struct ModuleMapping*  moduleMapping,
        _In_ void*                  iatData)
{
    uint32_t* thunk = iatData;
    while (*thunk) {
        uint32_t thunkValue = *thunk;
        uint32_t rva;
        oserr_t  oserr;

        // If the upper bit is set, then it's import by ordinal
        if (thunkValue & PE_IMPORT_ORDINAL_32) {
            uint32_t ordinal = thunkValue & 0xFFFF;
            oserr = __GetFunctionByOrdinal(
                    moduleMapEntry,
                    ordinal,
                    &rva
            );
            if (oserr != OS_EOK) {
                ERROR("__ProcessUnboundImportTable32 failed to import function (%u)", ordinal);
                return OS_EUNKNOWN;
            }
        } else {
            PeImportNameDescriptor_t* nameDescriptor = SectionMappingFromRVA(
                    moduleMapping->Mappings,
                    moduleMapping->MappingCount,
                    thunkValue & PE_IMPORT_NAMEMASK
            );
            if (nameDescriptor == NULL) {
                return OS_EUNKNOWN;
            }
            oserr = __GetFunctionByName(moduleMapEntry, (const char*)&nameDescriptor->Name[0], &rva);
            if (oserr != OS_EOK) {
                ERROR("__ProcessUnboundImportTable32 failed to import function (%s)", &nameDescriptor->Name[0]);
                return OS_EUNKNOWN;
            }
        }

        *thunk = (uint32_t)moduleMapEntry->BaseMapping + rva;
        thunk++;
    }
    return OS_EOK;
}

static oserr_t
__ProcessUnboundImportTable64(
        _In_ struct ModuleMapEntry* moduleMapEntry,
        _In_ struct ModuleMapping*  moduleMapping,
        _In_ void*                  iatData)
{
    uint64_t* thunk = iatData;
    while (*thunk) {
        uint64_t thunkValue = *thunk;
        uint32_t rva;
        oserr_t  oserr;

        if (thunkValue & PE_IMPORT_ORDINAL_64) {
            uint32_t ordinal = thunkValue & 0xFFFF;
            oserr = __GetFunctionByOrdinal(
                    moduleMapEntry,
                    ordinal,
                    &rva
            );
            if (oserr != OS_EOK) {
                ERROR("__ProcessUnboundImportTable64 failed to import function (%u)", ordinal);
                return OS_EUNKNOWN;
            }
        } else {
            PeImportNameDescriptor_t* nameDescriptor = SectionMappingFromRVA(
                    moduleMapping->Mappings,
                    moduleMapping->MappingCount,
                    thunkValue & PE_IMPORT_NAMEMASK
            );
            if (nameDescriptor == NULL) {
                return OS_EUNKNOWN;
            }
            oserr = __GetFunctionByName(moduleMapEntry, (const char*)&nameDescriptor->Name[0], &rva);
            if (oserr != OS_EOK) {
                ERROR("__ProcessUnboundImportTable64 failed to import function (%s)", &nameDescriptor->Name[0]);
                return OS_EUNKNOWN;
            }
        }

        *thunk = (uint64_t)moduleMapEntry->BaseMapping + rva;
        thunk++;
    }
    return OS_EOK;
}

static oserr_t
__ProcessUnboundImportTable(
        _In_ struct ModuleMapEntry* moduleMapEntry,
        _In_ struct ModuleMapping*  moduleMapping,
        _In_ void*                  iatData)
{
    if (moduleMapping->ModuleArchitecture == PE_ARCHITECTURE_32) {
        return __ProcessUnboundImportTable32(moduleMapEntry, moduleMapping, iatData);
    } else if (moduleMapping->ModuleArchitecture == PE_ARCHITECTURE_64) {
        return __ProcessUnboundImportTable64(moduleMapEntry, moduleMapping, iatData);
    }
    return OS_ENOTSUPPORTED;
}

static oserr_t
__ProcessImportDescriptorTable(
        _In_ struct PEImageLoadContext* loadContext,
        _In_ struct ModuleMapping*      moduleMapping,
        _In_ void*                      data)
{
    struct ModuleMapEntry moduleMapEntry;
    PeImportDescriptor_t* i = data;

    // The import descriptor table is a list of import tables. Each entry describes
    // a DLL that it imports from, and an import entry table that contains a list of
    // functions.
    while (i->ImportAddressTable != 0) {
        mstring_t* moduleName;
        void*      iat;
        void*      nameData;
        oserr_t    oserr;

        // Let's do a few verification checks, currently we do not support
        // bound import tables, but we don't expect anyone using these for now
        if (i->TimeStamp != 0) {
            ERROR("__ProcessImportDescriptorTable encounted bound import entry, not supported.");
            return OS_ENOTSUPPORTED;
        }

        iat = SectionMappingFromRVA(
                moduleMapping->Mappings,
                moduleMapping->MappingCount,
                i->ImportAddressTable
        );
        if (iat == NULL) {
            return OS_EUNKNOWN;
        }

        nameData = SectionMappingFromRVA(
                moduleMapping->Mappings,
                moduleMapping->MappingCount,
                i->ModuleName
        );
        if (nameData == NULL) {
            return OS_EUNKNOWN;
        }

        moduleName = mstr_new_u8((const char*)nameData);
        if (moduleName == NULL) {
            return OS_EOOM;
        }

        oserr = __ResolveImport(loadContext, moduleName, &moduleMapEntry);
        mstr_delete(moduleName);
        if (oserr != OS_EOK) {
            return oserr;
        }

        oserr = __ProcessUnboundImportTable(moduleMapping, iat);
        if (oserr != OS_EOK) {
            return oserr;
        }
        i++;
    }
    return OS_EOK;
}

static oserr_t
__ProcessImports(
        _In_ struct PEImageLoadContext* loadContext,
        _In_ struct ModuleMapping*      moduleMapping)
{
    uint32_t rva  = moduleMapping->DataDirectories[PE_SECTION_IMPORT].AddressRVA;
    uint32_t size = moduleMapping->DataDirectories[PE_SECTION_IMPORT].Size;
    void*    data;
    TRACE("__ProcessImports()");

    if (rva == 0 || size == 0) {
        // no imports
        return OS_EOK;
    }

    data = SectionMappingFromRVA(
            moduleMapping->Mappings,
            moduleMapping->MappingCount,
            rva
    );
    if (data == NULL) {
        return OS_EUNKNOWN;
    }
    return __ProcessImportDescriptorTable(loadContext, moduleMapping, data);
}

static oserr_t
__ProcessRuntimeRelocations(
        _In_ struct ModuleMapping* moduleMapping)
{
    uint32_t rva  = moduleMapping->DataDirectories[PE_SECTION_GLOBAL_PTR].AddressRVA;
    uint32_t size = moduleMapping->DataDirectories[PE_SECTION_GLOBAL_PTR].Size;
    TRACE("__ProcessRuntimeRelocations()");

    if (rva == 0 || size == 0) {
        // no runtime relocations
        return OS_EOK;
    }
}

static oserr_t
__PostProcessImage(
        _In_ struct PEImageLoadContext* loadContext,
        _In_ struct ModuleMapping*      moduleMapping)
{
    oserr_t oserr;
    TRACE("__PostProcessImage()");

    oserr = __ProcessImports(loadContext, moduleMapping);
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = __ProcessRuntimeRelocations(moduleMapping);
    if (oserr != OS_EOK) {
        return oserr;
    }
    return OS_EOK;
}

oserr_t
PEImageLoad(
        _In_  struct PEImageLoadContext* loadContext,
        _In_  struct PEImage*            parent,
        _In_  mstring_t*                 path,
        _Out_ struct PEImage**           imageOut)
{
    struct ModuleMapping* moduleMapping;
    mstring_t*            resolvedPath;
    oserr_t               oserr;
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

    // Post-processing of image, we need to take care of the
    // following:
    // Imports
    // Runtime relocations
    oserr = __PostProcessImage(loadContext, moduleMapping);
    if (oserr != OS_EOK) {
        return oserr;
    }
    return OS_EOK;
}
