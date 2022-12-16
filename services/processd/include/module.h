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

#ifndef __MODULE_H__
#define __MODULE_H__

#include <ds/hashtable.h>
#include <os/pe.h>
#include <os/usched/mutex.h>

struct Section {
    char        Name[PE_SECTION_NAME_LENGTH + 1];
    bool        Zero;
    const void* FileData;
    uint32_t    RVA;
    size_t      FileLength;
    size_t      MappedLength;
    uint32_t    MapFlags;
};

struct Module {
    bool              Parsed;
    void*             ImageBuffer;
    size_t            ImageBufferSize;
    struct usched_mtx Mutex;
    struct Section*   Sections;
    int               SectionCount;

    uintptr_t ImageBase;
    size_t    MetaDataSize;
    uint32_t  SectionAlignment;
    uint32_t  Architecture;

    PeDataDirectory_t DataDirectories[PE_NUM_DIRECTORIES];

    // ExportedFunctions is a hashtable with the following
    // structure: <ordinal, struct ExportedFunction>. It contains
    // all the functions exported by the module.
    hashtable_t ExportedOrdinals;

    // ExportedNames is a hashtable with the following
    // structure: <string, struct ExportedFunction>. It contains
    // all the functions exported by the module, keyed with name.
    hashtable_t ExportedNames;
};

/**
 * @brief
 * @param moduleBuffer
 * @param bufferSize
 * @return
 */
struct Module*
ModuleNew(
        _In_ void*  moduleBuffer,
        _In_ size_t bufferSize);

/**
 * @brief
 * @param module
 */
void
ModuleDelete(
        _In_ struct Module* module);

/**
 * @brief
 * @param module
 * @return
 */
uint32_t
ModuleArchitecture(
        _In_ struct Module* module);

/**
 * @brief
 * @param module
 * @return
 */
PeDataDirectory_t*
ModuleDataDirectories(
        _In_ struct Module* module);

#endif //!__MODULE_H__
