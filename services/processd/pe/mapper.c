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
#define __need_minmax
#include <assert.h>
#include <ds/mstring.h>
#include <ddk/memory.h>
#include <ddk/utils.h>
#include <os/memory.h>
#include <string.h>
#include <stdlib.h>
#include <module.h>
#include "private.h"

static oserr_t
__CreateMapping(
        _In_ uuid_t                 memorySpaceHandle,
        _In_ struct SectionMapping* mapping)
{
    struct MemoryMappingParameters Parameters;
    Parameters.VirtualAddress = mapping->MappedAddress;
    Parameters.Length         = mapping->Length;
    Parameters.Flags          = mapping->Flags;
    return CreateMemoryMapping(
            memorySpaceHandle,
            &Parameters,
            (void**)&mapping->LocalAddress
    );
}

static inline void
__DestroyMapping(
        _In_ struct SectionMapping* mapping)
{
    MemoryFree((void*)mapping->LocalAddress, mapping->Length);
}

static uintptr_t
__AllocateLoadSpace(
        _In_    struct Module* module,
        _InOut_ uintptr_t*     loadAddress,
        _In_    size_t         size)
{
    size_t    alignment = module->SectionAlignment;
    size_t    count     = DIVUP(size, alignment);
    uintptr_t address   = *loadAddress;
    *loadAddress += count * alignment;
    return address;
}

static oserr_t
__MapMetaData(
        _In_    struct Module* module,
        _In_    uuid_t         memorySpace,
        _InOut_ uintptr_t*     loadAddress)
{
    struct SectionMapping metaData;
    oserr_t               oserr;
    TRACE("__MapMetaData()");

    metaData.MappedAddress = __AllocateLoadSpace(module, loadAddress, module->MetaDataSize);
    metaData.Length = module->MetaDataSize;
    metaData.Flags = MEMORY_READ | MEMORY_WRITE;
    oserr = __CreateMapping(memorySpace, &metaData);
    if (oserr != OS_EOK) {
        return oserr;
    }

    // Now we have mapping into the memory space, let's copy the metadata
    memcpy(metaData.LocalAddress, module->ImageBuffer, module->MetaDataSize);
    __DestroyMapping(&metaData);
    return OS_EOK;
}

static oserr_t
__MapSection(
        _In_    struct Module*         module,
        _In_    struct Section*        section,
        _In_    uuid_t                 memorySpace,
        _InOut_ uintptr_t*             loadAddress,
        _In_    struct SectionMapping* mapping)
{
    size_t  sectionLength = MAX(section->FileLength, section->MappedLength);
    oserr_t oserr;
    TRACE("__MapSection(name=%s, size=%" PRIuIN ", rva=0x%x)",
          &section->Name[0], sectionLength, section->RVA);

    // Initialize the section mapping
    mapping->RVA    = section->RVA;
    mapping->Length = sectionLength;
    mapping->Flags  = section->MapFlags;
    mapping->MappedAddress = __AllocateLoadSpace(module, loadAddress, sectionLength);
    oserr = __CreateMapping(memorySpace, mapping);
    if (oserr != OS_EOK) {
        return oserr;
    }

    // Copy the section in, there are different kinds of sections here we need
    // to handle.
    if (section->Zero) {
        memset(mapping->LocalAddress, 0, sectionLength);
    } else {
        uint8_t* pointer = mapping->LocalAddress;
        if (section->FileData != NULL) {
            memcpy(pointer, section->FileData, section->FileLength);
            pointer += section->FileLength;
        }

        // In some cases, we will need to zero extend
        if (section->MappedLength > section->FileLength) {
            memset(pointer, 0, section->MappedLength - section->FileLength);
        }
    }
    return OS_EOK;
}

static oserr_t
__MapSections(
        _In_    struct Module*          module,
        _In_    uuid_t                  memorySpace,
        _InOut_ uintptr_t*              loadAddress,
        _Out_   struct SectionMapping** mappingsOut)
{
    struct SectionMapping* mappings;
    oserr_t                oserr = OS_EOK;
    TRACE("__MapSections()");

    mappings = malloc(sizeof(struct SectionMapping) * module->SectionCount);
    if (mappings == NULL) {
        return OS_EOOM;
    }
    memset(mappings, 0, sizeof(struct SectionMapping) * module->SectionCount);

    for (int i = 0; i < module->SectionCount; i++) {
        oserr = __MapSection(
                module,
                &module->Sections[i],
                memorySpace,
                loadAddress,
                &mappings[i]
        );
        // On errors, we break, the cleanup of all this mess must
        // be cleaned up, otherwise we pollute the memory space of
        // the process loader, which is unwarranted.
        // Let the caller handle the cleanup.
        if (oserr != OS_EOK) {
            break;
        }
    }
    *mappingsOut = mappings;
    return oserr;
}

static void
__SectionMappingsDelete(
        _In_ struct SectionMapping* mappings,
        _In_ int                    count)
{
    if (mappings == NULL) {
        return;
    }

    for (int i = 0; i < count; i++) {
        if (mappings[i].Length == 0) {
            continue;
        }
        __DestroyMapping(&mappings[i]);
    }
}

static oserr_t
__MapModule(
        _In_    struct Module*          module,
        _In_    uuid_t                  memorySpace,
        _InOut_ uintptr_t*              loadAddress,
        _Out_   struct SectionMapping** mappingsOut)
{
    struct SectionMapping* mappings = NULL;
    oserr_t                oserr;
    TRACE("__MapModule()");

    // Let's get a lock on the module, we want to avoid any
    // double init of the module in a multithreaded scenario.
    usched_mtx_lock(&module->Mutex);
    if (!module->Parsed) {
        oserr = PEParseModule(module);
        if (oserr != OS_EOK) {
            usched_mtx_unlock(&module->Mutex);
            return oserr;
        }
    }
    usched_mtx_unlock(&module->Mutex);

    oserr = __MapMetaData(module, memorySpace, loadAddress);
    if (oserr != OS_EOK) {
        // Nothing to clean up here
        return oserr;
    }

    oserr = __MapSections(module, memorySpace, loadAddress, &mappings);
    if (oserr != OS_EOK) {
        // If this fails, we may end up with partial mappings, so we need to clean
        // them up here.
        __SectionMappingsDelete(mappings, module->SectionCount);
        return oserr;
    }

    *mappingsOut = mappings;
    return OS_EOK;
}

static oserr_t
__ProcessBaseRelocationTableEntry(
        _In_ uintptr_t imageDelta,
        _In_ uint8_t*  sectionData,
        _In_ uint16_t  entry)
{
    uint16_t type  = (entry >> 12);
    uint16_t value = entry & 0x0FFF;

    if (type == PE_RELOCATION_HIGHLOW || type == PE_RELOCATION_RELATIVE64) {
        uintptr_t* fixupAddress = (uintptr_t*)(sectionData + value);
        intptr_t   fixupValue = (intptr_t)(*fixupAddress);
        TRACE("__ProcessBaseRelocationTableEntry 0x%" PRIxIN " => 0x%" PRIxIN, fixupValue, fixupValue + imageDelta);
        *fixupAddress = fixupValue + imageDelta;
    } else if (type != PE_RELOCATION_ALIGN) {
        ERROR("__ProcessBaseRelocationTableEntry implement support for reloc type: %u", type);
        return OS_ENOTSUPPORTED;
    }
    return OS_EOK;
}

struct __RelocationTable {
    uint32_t RVA;
    uint32_t TableLength;
    uint16_t Entries[];
};

static oserr_t
__ProcessBaseRelocationTable(
        _In_ struct Module*            module,
        _In_ struct SectionMapping*    mappings,
        _In_ uintptr_t                 imageDelta,
        _In_ struct __RelocationTable* relocationTable)
{
    uint32_t numberOfEntries;
    uint8_t* sectionData;
    TRACE("__ProcessBaseRelocationTable(imageDelta=0x%" PRIxIN ")", imageDelta);
    if (relocationTable->RVA == 0 || relocationTable->TableLength == 0) {
        ERROR("__ProcessBaseRelocationTable invalid relocation table with zero length/rva");
        return OS_EUNKNOWN;
    }

    // When doing the base relocations, we must use the mappings created for this
    // instance of the module. Unfortunately we must do this for each time.
    numberOfEntries = (relocationTable->TableLength - 8) / sizeof(uint16_t);
    sectionData     = SectionMappingFromRVA(mappings, module->SectionCount, relocationTable->RVA);
    if (numberOfEntries == 0) {
        ERROR("__ProcessBaseRelocationTable invalid relocation table with zero entries");
        return OS_EUNKNOWN;
    }

    for (uint32_t i = 0; i < numberOfEntries; i++) {
        oserr_t oserr = __ProcessBaseRelocationTableEntry(
                imageDelta,
                sectionData,
                relocationTable->Entries[i]
        );
        if (oserr != OS_EOK) {
            return oserr;
        }
    }
    return OS_EOK;
}

static void*
__ConvertRVAToDataPointer(
        _In_ struct Module* module,
        _In_ uint32_t       rva)
{
    for (int i = 0; i < module->SectionCount; i++) {
        if (rva >= module->Sections[i].RVA && rva < (module->Sections[i].RVA + module->Sections[i].MappedLength)) {
            return (void*)((uint8_t*)module->Sections[i].FileData + (rva - module->Sections[i].RVA));
        }
    }
    return NULL;
}

static oserr_t
__ProcessBaseRelocations(
        _In_ struct Module*         module,
        _In_ struct SectionMapping* mappings,
        _In_ uintptr_t              imageDelta)
{
    uint32_t rva  = module->DataDirectories[PE_SECTION_BASE_RELOCATION].AddressRVA;
    uint32_t size = module->DataDirectories[PE_SECTION_BASE_RELOCATION].Size;
    uint8_t* data;
    TRACE("__ProcessBaseRelocations(rva=0x%x, size=0x%x)", rva, size);

    if (rva == 0 || size == 0) {
        // no relocations present
        return OS_EOK;
    }

    data = __ConvertRVAToDataPointer(module, rva);
    while (size) {
        struct __RelocationTable* table = (struct __RelocationTable*)data;
        oserr_t                   oserr;

        oserr = __ProcessBaseRelocationTable(module, mappings, imageDelta, table);
        if (oserr != OS_EOK) {
            return oserr;
        }
        data += table->TableLength;
        size -= table->TableLength;
    }
    return OS_EOK;
}

static oserr_t
__ProcessModule(
        _In_ struct Module*         module,
        _In_ struct SectionMapping* mappings,
        _In_ uintptr_t              imageDelta)
{
    oserr_t oserr;
    TRACE("__ProcessModule()");

    // Run relocations, we can do this as it's independant of other
    // modules.
    oserr = __ProcessBaseRelocations(module, mappings, imageDelta);
    if (oserr != OS_EOK) {
        return oserr;
    }

    return OS_EOK;
}

static struct ModuleMapping*
__MapperModuleNew(
        _In_ struct Module*         module,
        _In_ struct SectionMapping* mappings,
        _In_ uintptr_t              mappingBase)
{
    struct ModuleMapping* moduleMapping;

    moduleMapping = malloc(sizeof(struct ModuleMapping));
    if (moduleMapping == NULL) {
        return NULL;
    }

    moduleMapping->MappingBase = mappingBase;
    moduleMapping->Mappings    = mappings;
    moduleMapping->MappingCount = module->SectionCount;
    moduleMapping->Module = module;
    return moduleMapping;
}

oserr_t
MapperLoadModule(
        _In_  struct PEImageLoadContext* loadContext,
        _In_  mstring_t*                 path,
        _Out_ struct ModuleMapping**     moduleMappingOut)
{
    struct SectionMapping* mappings = NULL;
    struct Module*         module;
    oserr_t                oserr;
    uintptr_t              baseAddress;
    uintptr_t              imageDelta;
    TRACE("MapperLoadModule(path=%ms)", path);

    if (path == NULL || loadContext == NULL) {
        return OS_EINVALPARAMS;
    }

    oserr = PECacheGet(path, &module);
    if (oserr != OS_EOK) {
        return oserr;
    }

    // Store the initial load address for delta calculation
    baseAddress = loadContext->LoadAddress;

    // Map the module into the memory space provided
    oserr = __MapModule(
            module,
            loadContext->MemorySpace,
            &loadContext->LoadAddress,
            &mappings
    );
    if (oserr != OS_EOK) {
        goto cleanup;
    }

    // Calculate the image delta for processings
    NOTICE("MapperLoadModule(%ms) loaded at 0x%" PRIxIN, path, baseAddress);
    imageDelta = (baseAddress - module->ImageBase);

    // Run fixup's on the new mappings
    oserr = __ProcessModule(module, mappings, imageDelta);
    if (oserr != OS_EOK) {
        goto cleanup;
    }

    // Store the module hash so the loader can reduce the reference
    // count again later
    *moduleMappingOut = __MapperModuleNew(module, mappings, baseAddress);
    if (*moduleMappingOut == NULL) {
        oserr = OS_EOOM;
        goto cleanup;
    }
    return OS_EOK;

cleanup:
    __SectionMappingsDelete(mappings, module->SectionCount);
    loadContext->LoadAddress = baseAddress;
    return oserr;
}

void
ModuleMappingDelete(
        _In_ struct ModuleMapping* moduleMapping)
{
    if (moduleMapping == NULL) {
        return;
    }

    __SectionMappingsDelete(
            moduleMapping->Mappings,
            moduleMapping->MappingCount
    );
    free(moduleMapping);
}
