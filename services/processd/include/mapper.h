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

#ifndef __PE_MAPPER_H__
#define __PE_MAPPER_H__

#include <os/osdefs.h>
#include <ds/hashtable.h>
#include <ds/list.h>
#include <pe.h>

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
    struct SectionMapping* Mappings;
    int                    MappingCount;

    // DataDirectories is the data directory list
    // of the loaded module.
    PeDataDirectory_t* DataDirectories;
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

#endif //!__PE_MAPPER_H__
