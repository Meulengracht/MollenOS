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
struct ModuleMapping;

struct ModuleMapEntry {
    mstring_t*     Name;
    struct Module* Module;
};

struct PEImageLoadContext {
    uuid_t    Scope;
    uuid_t    MemorySpace;
    uintptr_t LoadAddress;
    char*     Paths;

    // ModuleMap is the map of loaded modules for
    // this process context. It's constructed with the following
    // format: <string, Module*>
    hashtable_t ModuleMap;
};

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

    PeDataDirectory_t DataDirectories[PE_NUM_DIRECTORIES];

    // ExportedFunctions is a hashtable with the following
    // structure: <ordinal, struct ExportedFunction>. It contains
    // all the functions exported by the module.
    hashtable_t ExportedFunctions;
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

#endif //!__PRIVATE_H__
