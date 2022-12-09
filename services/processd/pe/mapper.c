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
#include <ds/hashtable.h>
#include <ds/list.h>
#include <ds/mstring.h>
#include <ddk/utils.h>
#include <os/usched/mutex.h>
#include <os/memory.h>
#include <string.h>
#include <stdlib.h>
#include "pe.h"
#include "mapper.h"

struct Section {
    char        Name[PE_SECTION_NAME_LENGTH + 1];
    bool        Zero;
    const void* FileData;
    uintptr_t   RVA;
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

    // ExportedFunctions is a hashtable with the following
    // structure: <ordinal, struct ExportedFunction>. It contains
    // all the functions exported by the module.
    hashtable_t ExportedFunctions;
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
};

static uint64_t __module_hash(const void* element);
static int      __module_cmp(const void* element1, const void* element2);
static uint64_t __path_hash(const void* element);
static int      __path_cmp(const void* element1, const void* element2);

static struct MappingManager g_mapper = { 0 };

oserr_t
MapperInitialize(void)
{
    int status;

    usched_mtx_init(&g_mapper.ModulesMutex, USCHED_MUTEX_PLAIN);
    usched_mtx_init(&g_mapper.PathsMutex, USCHED_MUTEX_PLAIN);

    status = hashtable_construct(
            &g_mapper.Modules, 0, sizeof(struct __ModuleEntry),
            __module_hash, __module_cmp);
    if (status) {
        return OS_EOOM;
    }

    status = hashtable_construct(
            &g_mapper.Paths, 0, sizeof(struct __PathEntry),
            __path_hash, __path_cmp);
    if (status) {
        return OS_EOOM;
    }
    return OS_EOK;
}

void
MapperDestroy(void)
{
    hashtable_destroy(&g_mapper.Modules);
    hashtable_destroy(&g_mapper.Paths);
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

static void
__AddModuleHash(
        _In_ mstring_t* path,
        _In_ uint32_t   hash)
{
    usched_mtx_lock(&g_mapper.PathsMutex);
    hashtable_set(
            &g_mapper.Paths,
            &(struct __PathEntry) {
                .Path = path, .ModuleHash = hash
            }
    );
    usched_mtx_unlock(&g_mapper.PathsMutex);
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
__AddModule(
        _In_ struct Module* module,
        _In_ uint32_t       moduleHash)
{
    struct __ModuleEntry  entry;
    struct __ModuleEntry* existingEntry;
    oserr_t               oserr;

    entry.Hash = moduleHash;
    entry.References = 0;
    entry.Module = module;

    usched_mtx_lock(&g_mapper.ModulesMutex);
    existingEntry = hashtable_get(&g_mapper.Modules, &(struct __ModuleEntry) { .Hash = moduleHash });
    if (existingEntry) {
        oserr = OS_EEXISTS;
    } else {
        hashtable_set(&g_mapper.Paths, &entry);
        oserr = OS_EOK;
    }
    usched_mtx_unlock(&g_mapper.ModulesMutex);
    return oserr;
}

static struct Module*
__ModuleNew(
        _In_ void*  moduleBuffer,
        _In_ size_t bufferSize)
{

}

static void
__ModuleDelete(
        _In_ struct Module* module)
{
    free(module->ImageBuffer);
    free(module);
}

static oserr_t
__LoadModule(
        _In_  mstring_t* path,
        _Out_ uint32_t*  hashOut)
{
    void*          moduleBuffer;
    struct Module* module;
    size_t         bufferSize;
    oserr_t        oserr;

    oserr = PELoadImage(path, (void**)&moduleBuffer, &bufferSize);
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = PEValidateImageChecksum(moduleBuffer, bufferSize, hashOut);
    if (oserr != OS_EOK) {
        free(moduleBuffer);
        return oserr;
    }

    // Insert the hash with the path, now that it's calculated we atleast
    // do not need to do this again in the future for this absolute path.
    __AddModuleHash(path, *hashOut);
    // TODO: this has one huge weakness, and that is two scopes share paths
    // but in reality use different versions of programs. I think it's vital
    // that we are aware of the scope the caller is loading in. In reality
    // we need to know this *anyway* as the caller may load a path that doesn't exist
    // in our root scope.

    // Pre-create the module instance, so we can try to insert it immediately,
    // and then modify it after insertion. This is to avoid too much pre-init
    // if the module already exists.
    module = __ModuleNew(moduleBuffer, bufferSize);
    if (module == NULL) {
        return OS_EOOM;
    }

    oserr = __AddModule(module, *hashOut);
    if (oserr == OS_EEXISTS) {
        // Module was already loaded, but we are loading under a new path.
        // This is now registered, so we can actually just abort here and return
        // OK as it should find it now with the hash.
        __ModuleDelete(moduleBuffer);
        return OS_EOK;
    }
    return oserr;
}

static void
__CopyDirectories(
        _In_ struct Module*     module,
        _In_ PeDataDirectory_t* directories)
{

}

static void
__ParsePE32Headers(
        _In_  struct Module*      module,
        _In_  PeOptionalHeader_t* optionalHeader,
        _Out_ void**              sectionHeadersOut)
{
    PeOptionalHeader32_t* header = (PeOptionalHeader32_t*)optionalHeader;

    module->ImageBase    = header->BaseAddress;
    module->MetaDataSize = header->SizeOfHeaders;
    __CopyDirectories(module, (PeDataDirectory_t*)&header->Directories[0]);

    *sectionHeadersOut = (void*)((uint8_t*)header + sizeof(PeOptionalHeader32_t));
}

static void
__ParsePE64Headers(
        _In_  struct Module*      module,
        _In_  PeOptionalHeader_t* optionalHeader,
        _Out_ void**              sectionHeadersOut)
{
    PeOptionalHeader64_t* header = (PeOptionalHeader64_t*)optionalHeader;

    module->ImageBase    = (uintptr_t)header->BaseAddress;
    module->MetaDataSize = header->SizeOfHeaders;
    __CopyDirectories(module, (PeDataDirectory_t*)&header->Directories[0]);

     *sectionHeadersOut = (void*)((uint8_t*)header + sizeof(PeOptionalHeader64_t));
}

static oserr_t
__ParseModuleHeaders(
        _In_  struct Module* module,
        _Out_ void**         sectionHeadersOut,
        _Out_ int*           sectionCountOut)
{
    MzHeader_t*         dosHeader;
    PeHeader_t*         peHeader;
    PeOptionalHeader_t* optionalHeader;

    // Avoid doing any further checks on DOS/PE headers as they have been validated
    // earlier. We do however match against current machine and architecture
    dosHeader      = (MzHeader_t*)module->ImageBuffer;
    peHeader       = (PeHeader_t*)((uint8_t*)module->ImageBuffer + dosHeader->PeHeaderAddress);
    if (peHeader->Machine != PE_CURRENT_MACHINE) {
        ERROR("__ParseModuleHeaders The image as built for machine type 0x%x, "
              "which is not the current machine type.", peHeader->Machine);
        return OS_EUNKNOWN;
    }
    *sectionCountOut = peHeader->NumSections;

    optionalHeader = (PeOptionalHeader_t*)((uint8_t*)peHeader + sizeof(PeHeader_t));
    if (optionalHeader->Architecture != PE_CURRENT_ARCH) {
        ERROR("__ParseModuleHeaders The image was built for architecture 0x%x, "
              "and was not supported by the current architecture.", optionalHeader->Architecture);
        return OS_EUNKNOWN;
    }

    if (optionalHeader->Architecture == PE_ARCHITECTURE_32) {
        __ParsePE32Headers(module, optionalHeader, sectionHeadersOut);
    } else if (optionalHeader->Architecture == PE_ARCHITECTURE_64) {
        __ParsePE64Headers(module, optionalHeader, sectionHeadersOut);
    } else {
        ERROR("__ParseModuleHeaders Unsupported architecture %x", optionalHeader->Architecture);
        return OS_EUNKNOWN;
    }
    return OS_EOK;
}

static unsigned int
__GetSectionPageFlags(
        _In_ PeSectionHeader_t* section)
{
    unsigned int flags = MEMORY_READ;
    if (section->Flags & PE_SECTION_EXECUTE) {
        flags |= MEMORY_EXECUTABLE;
    }
    if (section->Flags & PE_SECTION_WRITE) {
        flags |= MEMORY_WRITE;
    }
    return flags;
}

static oserr_t
__ParseModuleSections(
        _In_ struct Module* module,
        _In_ void*          sectionHeadersData,
        _In_ int            sectionCount)
{
    PeSectionHeader_t* section = sectionHeadersData;

    module->Sections = malloc(sizeof(struct Section) * sectionCount);
    if (module->Sections == NULL) {
        return OS_EOOM;
    }
    memset(module->Sections, 0, sizeof(struct Section) * sectionCount);

    for (int i = 0; i < sectionCount; i++, section++) {
        uint8_t* sectionFileData = ((uint8_t*)module->ImageBuffer + section->RawAddress);

        // Calculate page flags for the section
        memcpy(&module->Sections[i].Name[0], &section->Name[0], PE_SECTION_NAME_LENGTH);
        module->Sections[i].FileData     = (const void*)sectionFileData;
        module->Sections[i].MapFlags     = __GetSectionPageFlags(section);
        module->Sections[i].FileLength   = section->RawSize;
        module->Sections[i].MappedLength = section->VirtualSize;
        module->Sections[i].RVA          = section->VirtualAddress;

        // Is it a zero section?
        if (section->RawSize == 0 || (section->Flags & PE_SECTION_BSS)) {
            module->Sections[i].Zero = true;
        }
        module->SectionCount++;
    }
    return OS_EOK;
}

static oserr_t
__ParseModuleExportedFunctions(
        _In_ struct Module* module)
{

}

static oserr_t
__ParseModule(
        _In_ struct Module* module)
{
    void*   sectionHeaders;
    int     sectionCount;
    oserr_t oserr;

    oserr = __ParseModuleHeaders(module, &sectionHeaders, &sectionCount);
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = __ParseModuleSections(module, sectionHeaders, sectionCount);
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = __ParseModuleExportedFunctions(module);
    if (oserr != OS_EOK) {
        return oserr;
    }

    // more?

    // mark module parsed
    module->Parsed = true;
    return OS_EOK;
}

static oserr_t
__MapSection(
        _In_    struct Section*        section,
        _In_    uuid_t                 memorySpace,
        _InOut_ uintptr_t*             loadAddress,
        _In_    struct SectionMapping* mapping)
{

}

static oserr_t
__MapSections(
        _In_    struct Module*          module,
        _In_    uuid_t                  memorySpace,
        _InOut_ uintptr_t*              loadAddress,
        _Out_   struct SectionMapping** mappingsOut)
{
    struct SectionMapping* mappings;

    mappings = malloc(sizeof(struct SectionMapping) * module->SectionCount);
    if (mappings == NULL) {
        return OS_EOOM;
    }
    memset(mappings, 0, sizeof(struct SectionMapping) * module->SectionCount);

    for (int i = 0; i < module->SectionCount; i++) {
        oserr_t oserr = __MapSection(&module->Sections[i], memorySpace, loadAddress, &mappings[i]);
        if (oserr != OS_EOK) {
            free(mappings);
            return oserr;
        }
    }
    *mappingsOut = mappings;
    return OS_EOK;
}

static oserr_t
__MapModule(
        _In_    struct Module*          module,
        _In_    uuid_t                  memorySpace,
        _InOut_ uintptr_t*              loadAddress,
        _Out_   struct SectionMapping** mappingsOut)
{
    oserr_t oserr;

    // Let's get a lock on the module, we want to avoid any
    // double init of the module in a multithreaded scenario.
    usched_mtx_lock(&module->Mutex);
    if (!module->Parsed) {
        oserr = __ParseModule(module);
        if (oserr != OS_EOK) {
            usched_mtx_unlock(&module->Mutex);
            return oserr;
        }
    }
    usched_mtx_unlock(&module->Mutex);
    return __MapSections(module, memorySpace, loadAddress, mappingsOut);
}

static struct MapperModule*
__MapperModuleNew(
        _In_ struct Module* module)
{

}

oserr_t
MapperLoadModule(
        _In_    mstring_t*              path,
        _In_    uuid_t                  memorySpace,
        _InOut_ uintptr_t*              loadAddress,
        _Out_   uint32_t*               moduleKeyOut,
        _Out_   struct MapperModule**   moduleOut,
        _Out_   struct SectionMapping** mappingsOut)
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
    *moduleOut = __MapperModuleNew(module);
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

static uint64_t __module_hash(const void* element)
{

}

static int __module_cmp(const void* element1, const void* element2)
{

}

static uint64_t __path_hash(const void* element)
{

}

static int __path_cmp(const void* element1, const void* element2)
{

}

