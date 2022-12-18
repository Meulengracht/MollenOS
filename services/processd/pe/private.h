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

#ifndef __PRIVATE_H__
#define __PRIVATE_H__

#include <ds/hashtable.h>
#include <ds/mstring.h>
#include <os/osdefs.h>
#include <os/usched/mutex.h>
#include <pe.h>

struct Module;

struct ExportedFunction {
    const char* Name;
    // ForwardName gives the DLL name and the name of the export
    // (for example, "MYDLL.expfunc") or the DLL name and the
    // ordinal number of the export (for example, "MYDLL.#27").
    const char* ForwardName;
    int         Ordinal;
    uintptr_t   RVA;
};

struct SectionMapping {
    unsigned int Flags;
    uintptr_t    MappedAddress;
    uint8_t*     LocalAddress;
    uintptr_t    RVA;
    size_t       Length;
};

struct ModuleMapping {
    uintptr_t              MappingBase;
    struct SectionMapping* Mappings;
    int                    MappingCount;
    struct Module*         Module;
};

struct ModuleMapEntry {
    mstring_t*     Name; // Primary Key
    int            ID; // Secondary Key
    struct Module* Module;
    uintptr_t      BaseMapping;
    bool           Dependency;
    list_t         Imports; // list<name>
};

static void*
SectionMappingFromRVA(
        _In_ struct SectionMapping* mappings,
        _In_ int                    mappingCount,
        _In_ uint32_t               rva)
{
    for (int i = 0; i < mappingCount; i++) {
        if (rva >= mappings->RVA && rva < (mappings->RVA + mappings->Length)) {
            return (mappings->LocalAddress + (rva - mappings->RVA));
        }
    }
    return NULL;
}

/**
 * @brief
 * @param path
 * @param moduleOut
 * @return
 */
oserr_t
PECacheGet(
        _In_  mstring_t*      path,
        _Out_ struct Module** moduleOut);

/**
 * @brief Resolves a path for a binary based on th
 * @param processId
 * @param paths
 * @param path
 * @param fullPathOut
 * @return
 */
oserr_t
PEResolvePath(
        _In_  struct PEImageLoadContext* loadContext,
        _In_  mstring_t*                 path,
        _Out_ mstring_t**                fullPathOut);

/**
 * @brief
 * @param module
 * @return
 */
oserr_t
PEParseModule(
        _In_ struct Module* module);

/**
 * @brief
 * @param path
 * @param memorySpace
 * @param loadAddress
 * @param moduleMappingOut
 * @return
 */
oserr_t
MapperLoadModule(
        _In_  struct PEImageLoadContext* loadContext,
        _In_  mstring_t*                 path,
        _Out_ struct ModuleMapping**     moduleMappingOut);

/**
 * @brief
 * @param moduleMapping
 */
void
ModuleMappingDelete(
        _In_ struct ModuleMapping* moduleMapping);

oserr_t
PEImportsProcess(
        _In_ struct PEImageLoadContext* loadContext,
        _In_ struct ModuleMapping*      moduleMapping,
        _In_ list_t*                    importsList);

oserr_t
PERuntimeRelocationsProcess(
        _In_ struct ModuleMapping* moduleMapping);

#endif //!__PRIVATE_H__
