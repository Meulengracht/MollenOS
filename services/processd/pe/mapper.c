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
#include <ds/mstring.h>
#include <ddk/memory.h>
#include <ddk/utils.h>
#include <os/memory.h>
#include <string.h>
#include <stdlib.h>
#include "pe.h"
#include "mapper.h"
#include "private.h"

static uint64_t __expfn_hash(const void* element);
static int      __expfn_cmp(const void* element1, const void* element2);

struct Module*
ModuleNew(
        _In_ void*  moduleBuffer,
        _In_ size_t bufferSize)
{
    struct Module* module;
    int            status;

    module = malloc(sizeof(struct Module));
    if (module == NULL) {
        return NULL;
    }
    memset(module, 0, sizeof(struct Module));

    module->ImageBuffer = moduleBuffer;
    module->ImageBufferSize = bufferSize;
    usched_mtx_init(&module->Mutex, USCHED_MUTEX_PLAIN);
    status = hashtable_construct(
            &module->ExportedFunctions, 0, sizeof(struct ExportedFunction),
            __expfn_hash, __expfn_cmp);
    if (status) {
        free(module);
        return NULL;
    }
    return module;
}

void
ModuleDelete(
        _In_ struct Module* module)
{
    if (module == NULL) {
        return;
    }

    // No further cleanup is needed for exported functions, as no allocations
    // are made for the structs themselves.
    hashtable_destroy(&module->ExportedFunctions);
    free(module->Sections);
    free(module->ImageBuffer);
    free(module);
}

static void
__CopyDirectories(
        _In_ struct Module*     module,
        _In_ PeDataDirectory_t* directories)
{
    memcpy(
            &module->DataDirectories[0],
            directories,
            sizeof(PeDataDirectory_t) * PE_NUM_DIRECTORIES
    );
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
__ParseModuleExportedFunctions(
        _In_ struct Module* module)
{
    PeDataDirectory_t*   directory;
    PeExportDirectory_t* exportDirectory;
    uint32_t*            nameTable;
    uint16_t*            ordinalTable;
    uint32_t*            addressTable;

    directory = &module->DataDirectories[PE_SECTION_EXPORT];
    if (directory->AddressRVA == 0 || directory->Size == 0) {
        // No exports
        return OS_EOK;
    }

    // Get the file data which the RVA points to
    exportDirectory = __ConvertRVAToDataPointer(
            module,
            module->DataDirectories[PE_SECTION_EXPORT].AddressRVA);
    if (exportDirectory == NULL) {
        ERROR("__ParseModuleExportedFunctions export directory was invalid");
        return OS_EUNKNOWN;
    }

    // Ordinals and names can exceed the number of exported function addresses. This is because
    // modules can export symbols from other modules, which may then exceed the number of actual
    // functions exported local to the module.
    nameTable = __ConvertRVAToDataPointer(module, exportDirectory->AddressOfNames);
    ordinalTable = __ConvertRVAToDataPointer(module, exportDirectory->AddressOfOrdinals);
    addressTable = __ConvertRVAToDataPointer(module, exportDirectory->AddressOfFunctions);
    for (int i = 0; i < exportDirectory->NumberOfNames; i++) {
        const char* name        = __ConvertRVAToDataPointer(module, nameTable[i]);
        uint32_t    ordinal     = (uint32_t)ordinalTable[i] - exportDirectory->OrdinalBase;
        uint32_t    fnRVA       = addressTable[ordinal];
        const char* forwardName = NULL;

        // The function RVA (fnRVA) is a field that uses one of two formats in the following table.
        // If the address specified is not within the export section (as defined by the address and
        // length that are indicated in the optional header), the field is an export RVA, which
        // is an actual address in code or data. Otherwise, the field is a forwarder RVA, which
        // names a symbol in another DLL.
        if (fnRVA >= directory->AddressRVA && fnRVA < (directory->AddressRVA + directory->Size)) {
            // The field is a forwarder RVA, which names a symbol in another DLL.
            forwardName = __ConvertRVAToDataPointer(module, fnRVA);
            fnRVA = 0; // fnRVA is invalid
        }

        hashtable_set(&module->ExportedFunctions, &(struct ExportedFunction) {
            .Name = name,
            .ForwardName = forwardName,
            .Ordinal = (int)ordinalTable[i],
            .RVA = fnRVA
        });
    }
    return OS_EOK;
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

    module->Parsed = true;
    return OS_EOK;
}

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
        _InOut_ uintptr_t* loadAddress,
        _In_    size_t     size)
{
    // TODO: this should use SectionAlignment not page size
    size_t    pageSize  = PECurrentPageSize();
    size_t    pageCount = DIVUP(size, pageSize);
    uintptr_t address   = *loadAddress;
    *loadAddress += pageCount * pageSize;
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

    metaData.MappedAddress = __AllocateLoadSpace(loadAddress, module->MetaDataSize);
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
        _In_    struct Section*        section,
        _In_    uuid_t                 memorySpace,
        _InOut_ uintptr_t*             loadAddress,
        _In_    struct SectionMapping* mapping)
{
    size_t  sectionLength;
    oserr_t oserr;
    TRACE("__MapSection(%s)", &section->Name[0]);

    sectionLength = MAX(section->FileLength, section->MappedLength);

    // Initialize the section mapping
    mapping->RVA    = section->RVA;
    mapping->Length = sectionLength;
    mapping->Flags  = section->MapFlags;
    mapping->MappedAddress = __AllocateLoadSpace(loadAddress, sectionLength);
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
        memcpy(pointer, section->FileData, section->FileLength);
        // In some cases, we will need to zero extend
        if (section->MappedLength > section->FileLength) {
            pointer += section->FileLength;
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

    oserr = __MapMetaData(module, memorySpace, loadAddress);
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = __MapSections(module, memorySpace, loadAddress, mappingsOut);
    if (oserr != OS_EOK) {
        return oserr;
    }
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
        uintptr_t  fixupValue = *fixupAddress + imageDelta;
        *fixupAddress = fixupValue;
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
    TRACE("__ProcessBaseRelocationTable()");
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
        _In_ struct Module* module)
{
    struct ModuleMapping* mapperModule;

    mapperModule = malloc(sizeof(struct ModuleMapping));
    if (mapperModule == NULL) {
        return NULL;
    }

    mapperModule->ExportedFunctions = &module->ExportedFunctions;
    return mapperModule;
}

oserr_t
MapperLoadModule(
        _In_  struct PEImageLoadContext* loadContext,
        _In_  mstring_t*                 path,
        _Out_ struct ModuleMapping**     moduleMappingOut)
{
    struct SectionMapping* mappings;
    struct Module*         module;
    uint32_t               moduleHash;
    oserr_t                oserr;
    uintptr_t              imageDelta;
    TRACE("MapperLoadModule(path=%ms)", path);

    if (path == NULL || loadContext == NULL) {
        return OS_EINVALPARAMS;
    }

    oserr = PECacheGet(path, &module);
    if (oserr != OS_EOK) {
        return oserr;
    }

    // Calculate the image delta for processings
    imageDelta = (loadContext->LoadAddress - module->ImageBase);

    // Map the module into the memory space provided
    oserr = __MapModule(
            module,
            loadContext->MemorySpace,
            &loadContext->LoadAddress,
            &mappings
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    // Run fixup's on the new mappings
    oserr = __ProcessModule(module, mappings, imageDelta);
    if (oserr != OS_EOK) {
        return oserr;
    }

    // Store the module hash so the loader can reduce the reference
    // count again later
    *moduleMappingOut = __MapperModuleNew(module);
    return OS_EOK;
}

oserr_t
MapperUnloadModule(
        _In_ struct ModuleMapping* moduleMapping)
{
    return OS_EOK;
}

static uint64_t __expfn_hash(const void* element)
{
    const struct ExportedFunction* function = element;
    return (uint64_t)function->Ordinal;
}

static int __expfn_cmp(const void* element1, const void* element2)
{
    const struct ExportedFunction* function1 = element1;
    const struct ExportedFunction* function2 = element2;
    return function1->Ordinal == function2->Ordinal ? 0 : -1;
}
