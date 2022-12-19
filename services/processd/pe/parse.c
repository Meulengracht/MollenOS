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
#include <os/memory.h>
#include <module.h>
#include "private.h"
#include <stdlib.h>
#include <string.h>

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

    module->ImageBase        = header->BaseAddress;
    module->MetaDataSize     = header->SizeOfHeaders;
    module->SectionAlignment = header->SectionAlignment;
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

    module->ImageBase        = (uintptr_t)header->BaseAddress;
    module->MetaDataSize     = header->SizeOfHeaders;
    module->SectionAlignment = header->SectionAlignment;
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

    module->Architecture = optionalHeader->Architecture;
    module->EntryPointRVA = optionalHeader->EntryPointRVA;
    module->CodeBaseRVA = optionalHeader->BaseOfCode;
    module->CodeSize = optionalHeader->SizeOfCode;

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

        hashtable_set(&module->ExportedOrdinals, &(struct ExportedFunction) {
                .Name = name,
                .ForwardName = forwardName,
                .Ordinal = (int)ordinalTable[i],
                .RVA = fnRVA
        });
        hashtable_set(&module->ExportedNames, &(struct ExportedFunction) {
                .Name = name,
                .ForwardName = forwardName,
                .Ordinal = (int)ordinalTable[i],
                .RVA = fnRVA
        });
    }
    return OS_EOK;
}

oserr_t
PEParseModule(
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
