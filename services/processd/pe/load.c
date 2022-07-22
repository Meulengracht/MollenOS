/**
 * MollenOS
 *
 * Copyright 2018, Philip Meulengracht
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
 *
 *
 * PE/COFF Image Loader
 *    - Implements support for loading and processing pe/coff image formats
 *      and implemented as a part of libds to share between services and kernel
 */

#define __TRACE

#include <assert.h>
#include <ds/ds.h>
#include <ds/list.h>
#include <ds/mstring.h>
#include <ddk/utils.h>
#include <os/mollenos.h>
#include <string.h>
#include <stdlib.h>
#include "pe.h"

typedef struct SectionMapping {
    MemoryMapHandle_t Handle;
    uint8_t*          BasePointer;
    uintptr_t         RVA;
    size_t            Size;
} SectionMapping_t;

#define OFFSET_IN_SECTION(Section, _RVA) (uintptr_t)((Section)->BasePointer + ((_RVA) - (Section)->RVA))

// Directory handlers
oserr_t PeHandleRuntimeRelocations(PeExecutable_t*, PeExecutable_t*, SectionMapping_t*, int, uint8_t*, size_t);
oserr_t PeHandleRelocations(PeExecutable_t*, PeExecutable_t*, SectionMapping_t*, int, uint8_t*, size_t);
oserr_t PeHandleExports(PeExecutable_t*, PeExecutable_t*, SectionMapping_t*, int, uint8_t*, size_t);
oserr_t PeHandleImports(PeExecutable_t*, PeExecutable_t*, SectionMapping_t*, int, uint8_t*, size_t);

typedef oserr_t(*DataDirectoryHandler)(PeExecutable_t*, PeExecutable_t*, SectionMapping_t*, int, uint8_t*, size_t);
static struct {
    int                  Index;
    DataDirectoryHandler Handler;
} DataDirectoryHandlers[] = {
    { PE_SECTION_BASE_RELOCATION, PeHandleRelocations },
    { PE_SECTION_EXPORT, PeHandleExports },

    // Delay handling of import, and make sure RuntimeRelocations is handled after that
    { PE_SECTION_IMPORT, PeHandleImports },
    { PE_SECTION_GLOBAL_PTR, PeHandleRuntimeRelocations },

    // EOL marker
    { PE_NUM_DIRECTORIES, NULL }
};

static SectionMapping_t*
GetSectionFromRVA(
    _In_ SectionMapping_t* SectionMappings,
    _In_ int               SectionCount,
    _In_ uintptr_t         RVA)
{
    int i;
    
    for (i = 0; i < SectionCount; i++) {
        uintptr_t SectionStart = SectionMappings[i].RVA;
        uintptr_t SectionEnd   = SectionMappings[i].RVA + SectionMappings[i].Size;
        if (RVA >= SectionStart && RVA < SectionEnd) {
            return &SectionMappings[i];
        }
    }

    // Debug why this happens
    ERROR("GetSectionFromRVA(0x%x) => error segment not found", RVA);
    for (i = 0; i < SectionCount; i++) {
        uintptr_t SectionStart = SectionMappings[i].RVA;
        uintptr_t SectionEnd   = SectionMappings[i].RVA + SectionMappings[i].Size;
        WARNING("section(%i) => 0x%x, 0x%x", i, SectionStart, SectionEnd);
    }
    assert(0);
    return NULL;
}

static uintptr_t
GetOffsetInSectionFromRVA(
    _In_ SectionMapping_t* SectionMappings,
    _In_ int               SectionCount,
    _In_ uintptr_t         RVA)
{
    SectionMapping_t* Section = GetSectionFromRVA(SectionMappings, SectionCount, RVA);
    if (Section != NULL) {
        return OFFSET_IN_SECTION(Section, RVA);
    }
    return 0;
}

static PeExportedFunction_t*
GetExportedFunctionByOrdinal(
    _In_ PeExportedFunction_t* Exports,
    _In_ int                   NumberOfExports,
    _In_ int                   Ordinal)
{
    for (int i = 0; i < NumberOfExports; i++) {
        if (Exports[i].Ordinal == Ordinal) {
            return &Exports[i];
        }
    }
    return NULL;
}

static PeExportedFunction_t*
GetExportedFunctionByNameDescriptor(
    _In_ PeExportedFunction_t*     Exports,
    _In_ int                       NumberOfExports,
    _In_ PeImportNameDescriptor_t* Descriptor)
{
    int i;

    // Use the hint if provided
    if (Descriptor->OrdinalHint != 0) {
        for (i = 0; i < NumberOfExports; i++) {
            if (Exports[i].Ordinal == Descriptor->OrdinalHint) {
                if (!strcmp(Exports[i].Name, (const char*)&Descriptor->Name[0])) {
                    return &Exports[i];
                }
            }
        }
    }

    // Hint was invalid or not present
    for (i = 0; i < NumberOfExports; i++) {
        if (Exports[i].Name != NULL) {
            if (!strcmp(Exports[i].Name, (const char*)&Descriptor->Name[0])) {
                return &Exports[i];
            }
        }
    }
    return NULL;
}

static oserr_t
PeHandleSections(
    _In_ PeExecutable_t*   parent,
    _In_ PeExecutable_t*   image,
    _In_ uint8_t*          data,
    _In_ uintptr_t         sectionsAddress,
    _In_ int               sectionCount,
    _In_ SectionMapping_t* sectionHandles)
{
    PeSectionHeader_t* section        = (PeSectionHeader_t*)sectionsAddress;
    uintptr_t          currentAddress = image->VirtualAddress;
    oserr_t         osStatus;
    MemoryMapHandle_t  mapHandle;
    char               sectionName[PE_SECTION_NAME_LENGTH + 1];
    int                i;

    for (i = 0; i < sectionCount; i++) {
        // Calculate pointers, we need two of them, one that
        // points to data in file, and one that points to where
        // in memory we want to copy data to
        uintptr_t    VirtualDestination = image->VirtualAddress + section->VirtualAddress;
        uint8_t*     FileBuffer         = (uint8_t*)(data + section->RawAddress);
        unsigned int PageFlags          = MEMORY_READ;
        size_t       SectionSize        = MAX(section->RawSize, section->VirtualSize);
        uint8_t*     Destination;

        // Make a local copy of the name, just in case
        // we need to do some debug print
        memcpy(&sectionName[0], &section->Name[0], PE_SECTION_NAME_LENGTH);
        sectionName[8] = 0;

        // Handle page flags for this section
        if (section->Flags & PE_SECTION_EXECUTE) {
            PageFlags |= MEMORY_EXECUTABLE;
        }
        if (section->Flags & PE_SECTION_WRITE) {
            PageFlags |= MEMORY_WRITE;
        }

        // Iterate pages and map them in our memory space
        osStatus = PeImplAcquireImageMapping(image->MemorySpace, &VirtualDestination, SectionSize, PageFlags, &mapHandle);
        if (osStatus != OsOK) {
            ERROR("%ms: Failed to map section %s at 0x%" PRIxIN ": %u",
                  image->Name, &sectionName[0], VirtualDestination, osStatus);
            return osStatus;
        }
        Destination = (uint8_t*)VirtualDestination;

        sectionHandles[i].Handle      = mapHandle;
        sectionHandles[i].BasePointer = Destination;
        sectionHandles[i].RVA         = section->VirtualAddress;
        sectionHandles[i].Size        = SectionSize;

        // Store first code segment we encounter
        if (section->Flags & PE_SECTION_CODE) {
            if (image->CodeBase == 0) {
                image->CodeBase = (uintptr_t)image->VirtualAddress + section->VirtualAddress;
                image->CodeSize = section->VirtualSize;
            }
        }

        // Handle sections specifics, we want to:
        // BSS: Zero out the memory 
        // Code: Copy memory 
        // data: Copy memory
        if (section->RawSize == 0 || (section->Flags & PE_SECTION_BSS)) {
            TRACE("section(%i, %s): clearing %u bytes => 0x%x (0x%x, 0x%x)",
                  i, &sectionName[0], section->VirtualSize, Destination,
                  image->VirtualAddress + section->VirtualAddress, PageFlags);
            memset(Destination, 0, section->VirtualSize);
        }
        else if ((section->Flags & PE_SECTION_CODE) || (section->Flags & PE_SECTION_DATA)) {
            TRACE("section(%i, %s): copying %u bytes 0x%" PRIxIN " => 0x%" PRIxIN " (0x%" PRIxIN ", 0x%x)",
                  i, &sectionName[0], section->RawSize, FileBuffer, Destination,
                  image->VirtualAddress + section->VirtualAddress, PageFlags);
            memcpy(Destination, FileBuffer, section->RawSize);

            // Sanitize this special case, if the virtual size
            // is large, this means there needs to be zeroed space afterwards
            if (section->VirtualSize > section->RawSize) {
                Destination += section->RawSize;
                TRACE("section(%i, %s): clearing %u bytes => 0x%x (0x%x, 0x%x)",
                      i, &sectionName[0], section->VirtualSize - section->RawSize,
                      Destination, image->VirtualAddress + section->VirtualAddress, PageFlags);
                memset(Destination, 0, (section->VirtualSize - section->RawSize));
            }
        }
        currentAddress = (image->VirtualAddress + section->VirtualAddress + SectionSize);
        section++;
    }

    // Return a page-aligned address that points to the
    // next free relocation address
    if (currentAddress % PeImplGetPageSize()) {
        currentAddress += (PeImplGetPageSize() - (currentAddress % PeImplGetPageSize()));
    }

    if (parent != NULL) parent->NextLoadingAddress = currentAddress;
    else image->NextLoadingAddress  = currentAddress;
    return OsOK;
}

static oserr_t
PeResolveImportDescriptor(
    _In_ PeExecutable_t*       ParentImage,
    _In_ PeExecutable_t*       Image,
    _In_ SectionMapping_t*     Section,
    _In_ PeImportDescriptor_t* ImportDescriptor,
    _In_ mstring_t*            ImportDescriptorName)
{
    PeExecutable_t*           ResolvedLibrary;
    PeExportedFunction_t*     Exports;
    int                       NumberOfExports;
    uintptr_t                 AddressOfImportTable;
    PeImportNameDescriptor_t* NameDescriptor;

    TRACE("PeResolveImportDescriptor(%ms, %ms)",
          Image->Name, ImportDescriptorName);

    // Resolve the library from the import chunk
    ResolvedLibrary = PeResolveLibrary(ParentImage, Image, ImportDescriptorName);
    if (ResolvedLibrary == NULL || ResolvedLibrary->ExportedFunctions == NULL) {
        ERROR("(%ms): Failed to resolve library %ms", Image->Name, ImportDescriptorName);
        return OsError;
    }
    Exports              = ResolvedLibrary->ExportedFunctions;
    NumberOfExports      = ResolvedLibrary->NumberOfExportedFunctions;
    AddressOfImportTable = OFFSET_IN_SECTION(Section, ImportDescriptor->ImportAddressTable);

    // Calculate address to IAT
    // These entries are 64 bit in PE32+ and 32 bit in PE32
    if (Image->Architecture == PE_ARCHITECTURE_32) {
        uint32_t* ThunkPointer = (uint32_t*)AddressOfImportTable;
        while (*ThunkPointer) {
            uint32_t              Value    = *ThunkPointer;
            PeExportedFunction_t* Function = NULL;

            // If the upper bit is set, then it's import by ordinal
            if (Value & PE_IMPORT_ORDINAL_32) {
                int Ordinal = (int)(Value & 0xFFFF);
                Function    = GetExportedFunctionByOrdinal(Exports, NumberOfExports, Ordinal);
                if (!Function) {
                    ERROR("Failed to locate function (%i)", Ordinal);
                    return OsError;
                }
            }
            else {
                NameDescriptor = (PeImportNameDescriptor_t*)OFFSET_IN_SECTION(Section, Value & PE_IMPORT_NAMEMASK);
                Function       = GetExportedFunctionByNameDescriptor(Exports, NumberOfExports, NameDescriptor);
                if (!Function) {
                    ERROR("Failed to locate function (%s)", &NameDescriptor->Name[0]);
                    return OsError;
                }
            }

            *ThunkPointer = Function->Address;
            ThunkPointer++;
        }
    }
    else {
        uint64_t* ThunkPointer = (uint64_t*)AddressOfImportTable;
        while (*ThunkPointer) {
            uint64_t              Value    = *ThunkPointer;
            PeExportedFunction_t* Function = NULL;

            if (Value & PE_IMPORT_ORDINAL_64) {
                int Ordinal = (int)(Value & 0xFFFF);
                Function    = GetExportedFunctionByOrdinal(Exports, NumberOfExports, Ordinal);
                if (!Function) {
                    ERROR("Failed to locate function (%i)", Ordinal);
                    return OsError;
                }
            }
            else {
                NameDescriptor = (PeImportNameDescriptor_t*)OFFSET_IN_SECTION(Section, Value & PE_IMPORT_NAMEMASK);
                Function       = GetExportedFunctionByNameDescriptor(Exports, NumberOfExports, NameDescriptor);
                if (!Function) {
                    ERROR("Failed to locate function (%s)", &NameDescriptor->Name[0]);
                    return OsError;
                }
            }
            *ThunkPointer = (uint64_t)Function->Address;
            ThunkPointer++;
        }
    }
    return OsOK;
}

oserr_t
PeHandleRelocations(
    _In_ PeExecutable_t*    parentImage,
    _In_ PeExecutable_t*    image,
    _In_ SectionMapping_t*  sectionMappings,
    _In_ int                sectionCount,
    _In_ uint8_t*           directoryContent,
    _In_ size_t             directorySize)
{
    size_t    bytesLeft         = directorySize;
    uint32_t* relocationPointer = (uint32_t*)directoryContent;
    intptr_t imageDelta         = (intptr_t)image->VirtualAddress - (intptr_t)image->OriginalImageBase;
    uint32_t  i;
    TRACE("PeHandleRelocations(count=%i, size=%" PRIuIN ", delta=0x%" PRIxIN ")",
          sectionCount, directorySize, imageDelta);

    // Sanitize the image delta
    if (imageDelta == 0) {
        return OsOK;
    }

    while (bytesLeft > 0) {
        uint16_t*         relocationTable;
        uint32_t          relocationCount;
        SectionMapping_t* section;
        uintptr_t         sectionOffset;

        // Get the next block
        uint32_t pageRva   = relocationPointer[0];
        uint32_t blockSize = relocationPointer[1];
        if (pageRva == 0 || blockSize == 0) {
            int Index = 0;
            ERROR("%ms: Directory 0x%" PRIxIN ", Size %" PRIuIN,
                  image->Name, directoryContent, directorySize);
            ERROR("bytesLeft %" PRIuIN ", ", bytesLeft);
            ERROR("pageRva is 0, blockSize %u", blockSize);
            for (i = 0; i < 4; i++, Index += 16) {
                WARNING("%x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x", directoryContent[Index],
                        directoryContent[Index + 1], directoryContent[Index + 2], directoryContent[Index + 3],
                        directoryContent[Index + 4], directoryContent[Index + 5], directoryContent[Index + 6],
                        directoryContent[Index + 7], directoryContent[Index + 8], directoryContent[Index + 9],
                        directoryContent[Index + 10], directoryContent[Index + 11], directoryContent[Index + 12],
                        directoryContent[Index + 13], directoryContent[Index + 14], directoryContent[Index + 15]);
            }
            assert(0);
        }

        section       = GetSectionFromRVA(sectionMappings, sectionCount, pageRva);
        sectionOffset = OFFSET_IN_SECTION(section, pageRva);

        if (blockSize > bytesLeft) {
            ERROR("Invalid relocation data: blockSize > bytesLeft, bailing");
            assert(0);
        }

        relocationCount = (blockSize - 8) / sizeof(uint16_t);
        relocationTable = (uint16_t*)&relocationPointer[2];

        for (i = 0; i < relocationCount; i++) {
            uint16_t relocationEntry = relocationTable[i];
            uint16_t type            = (relocationEntry >> 12);
            uint16_t value           = relocationEntry & 0x0FFF;
            
            // 32/64 Bit Relative
            if (type == PE_RELOCATION_HIGHLOW || type == PE_RELOCATION_RELATIVE64) {
                // Create a pointer, the low 12 bits have an offset into the pageRva
                uintptr_t* fixupAddress = (uintptr_t*)(sectionOffset + value);
                intptr_t   fixupValue = (intptr_t)(*fixupAddress) + imageDelta;
#if __BITS == 32
                if ((uintptr_t)fixupValue < image->VirtualAddress || (uintptr_t)fixupValue >= 0x30000000) {
                    ERROR("%ms: Rel %u, value %u (%u/%u)", image->Name, type, value, i, relocationCount);
                    ERROR("pageRva 0x%x of SectionRVA 0x%x. Current blocksize %u", pageRva, section->RVA, blockSize);
                    ERROR("section 0x%x, SectionAddress 0x%x, Address 0x%x, value 0x%x",
                        section->BasePointer, sectionOffset, fixupAddress, *fixupAddress);
                    ERROR("Relocation caused invalid pointer: 0x%x, 0x%x, New Base 0x%x, Old Base 0x%x",
                        fixupValue, imageDelta, image->VirtualAddress, image->OriginalImageBase);
                    assert(0);
                }
#endif
#if __BITS == 64
                if ((uintptr_t)fixupValue < image->VirtualAddress || (uintptr_t)fixupValue >= 0x8100000000ULL) {
                    ERROR("%ms: Rel %u, value %u (%u/%u)", image->Name, type, value, i, relocationCount);
                    ERROR("pageRva 0x%x of SectionRVA 0x%x. Current blocksize %u", pageRva, section->RVA, blockSize);
                    ERROR("section 0x%x, SectionAddress 0x%x, Address 0x%x, value 0x%x",
                          section->BasePointer, sectionOffset, fixupAddress, *fixupAddress);
                    ERROR("Relocation caused invalid pointer: 0x%llx, 0x%llx, New Base 0x%llx, Old Base 0x%llx",
                          fixupValue, imageDelta, image->VirtualAddress, image->OriginalImageBase);
                    assert(0);
                }
#endif
                *fixupAddress = (uintptr_t)fixupValue;
            }
            else if (type == PE_RELOCATION_ALIGN) {
                // End of alignment
                break;
            }
            else {
                ERROR("Implement support for reloc type: %u", type);
                assert(0);
            }
        }
        relocationPointer += (blockSize / sizeof(uint32_t));
        bytesLeft         -= blockSize;
    }
    return OsOK;
}

PACKED_TYPESTRUCT(RuntimeRelocationHeader, {
    uint32_t Magic0;
    uint32_t Magic1;
    uint32_t Version;
});

PACKED_TYPESTRUCT(RuntimeRelocationEntryV1, {
    uint32_t Value;
    uint32_t Offset;
});

PACKED_TYPESTRUCT(RuntimeRelocationEntryV2, {
    uint32_t Symbol;
    uint32_t Offset;
    uint32_t Flags;
});

#define RP_VERSION_1 0
#define RP_VERSION_2 1

static void HandleRelocationsV1(
        _In_ SectionMapping_t* Sections,
        _In_ int               SectionCount,
        _In_ uint8_t*          RelocationEntries,
        _In_ const uint8_t*    RelocationsEnd)
{
    RuntimeRelocationEntryV1_t* entry = (RuntimeRelocationEntryV1_t*)RelocationEntries;
    TRACE("HandleRelocationsV1(start=0x%" PRIxIN ", end=0x%" PRIxIN ")", RelocationEntries, RelocationsEnd);

    while ((uint8_t*)entry < RelocationsEnd) {
        uintptr_t sectionOffset = GetOffsetInSectionFromRVA(Sections, SectionCount, entry->Offset);
        TRACE("HandleRelocationsV1 sectionOffset=0x%" PRIxIN " from entry->Offset=0x%x",
                sectionOffset, entry->Offset);
        TRACE("HandleRelocationsV1 value=0x%" PRIxIN " += 0x%x",
                *((uintptr_t*)sectionOffset), entry->Value);

        *((uintptr_t*)sectionOffset) += entry->Value;
        entry++;
    }
}

static oserr_t HandleRelocationsV2(
        _In_ PeExecutable_t*   Image,
        _In_ SectionMapping_t* Sections,
        _In_ int               SectionCount,
        _In_ uint8_t*          RelocationEntries,
        _In_ const uint8_t*    RelocationsEnd)
{
    RuntimeRelocationEntryV2_t* entry = (RuntimeRelocationEntryV2_t*)RelocationEntries;
    TRACE("HandleRelocationsV2(start=0x%" PRIxIN ", end=0x%" PRIxIN ")", RelocationEntries, RelocationsEnd);

    while ((uint8_t*)entry < RelocationsEnd) {
        uintptr_t symbolSectionAddress = GetOffsetInSectionFromRVA(Sections, SectionCount, entry->Symbol);
        uintptr_t targetSectionAddress = GetOffsetInSectionFromRVA(Sections, SectionCount, entry->Offset);
        intptr_t  symbolValue = *((intptr_t*)symbolSectionAddress);
        uint8_t   relocSize = (uint8_t)(entry->Flags & 0xFF);
        intptr_t  relocData;

        TRACE("HandleRelocationsV2 symbolSectionAddress=0x%" PRIxIN " from entry->Symbol=0x%x",
                symbolSectionAddress, entry->Symbol);
        TRACE("HandleRelocationsV2 targetSectionAddress=0x%" PRIxIN " from entry->Offset=0x%x",
                targetSectionAddress, entry->Offset);
        TRACE("HandleRelocationsV2 symbolValue=0x%" PRIxIN ", relocSize=0x%x",
                symbolValue, relocSize);

        switch (relocSize) {
            case 8: {
                relocData = (intptr_t)*((uint8_t*)targetSectionAddress);
                if (relocData & 0x80) {
                    relocData |= ~((intptr_t) 0xFF);
                }
            } break;
            case 16: {
                relocData = (intptr_t)*((uint16_t*)targetSectionAddress);
                if (relocData & 0x8000) {
                    relocData |= ~((intptr_t) 0xFFFF);
                }
            } break;
            case 32: {
                relocData = (intptr_t)*((uint32_t*)targetSectionAddress);
#if defined(__amd64__)
                if (relocData & 0x80000000) {
                    relocData |= ~((intptr_t) 0xFFFFFFFF);
                }
#endif
            } break;
#if defined(__amd64__)
            case 64: {
                relocData = (intptr_t)*((uint64_t*)targetSectionAddress);
            } break;
#endif
            default: {
                ERROR("HandleRelocationsV2 invalid relocation size %u", relocSize);
                return OsError;
            }
        }

        TRACE("HandleRelocationsV2 relocData=0x%" PRIxIN ", Image->VirtualAddress=0x%" PRIxIN,
                relocData, Image->VirtualAddress);
        relocData -= ((intptr_t)Image->VirtualAddress + entry->Symbol);
        relocData += symbolValue;
        TRACE("HandleRelocationsV2 relocData=0x%" PRIxIN, relocData);

        switch (relocSize) {
            case 8: *((uint8_t*)targetSectionAddress) = (uint8_t)((uintptr_t)relocData & 0xFF); break;
            case 16: *((uint16_t*)targetSectionAddress) = (uint16_t)((uintptr_t)relocData & 0xFFFF); break;
            case 32: *((uint32_t*)targetSectionAddress) = (uint32_t)((uintptr_t)relocData & 0xFFFFFFFF); break;
#if defined(__amd64__)
            case 64: *((uint64_t*)targetSectionAddress) = (uint64_t)relocData; break;
#endif
            default: break;
        }
        entry++;
    }
    return OsOK;
}

oserr_t PeHandleRuntimeRelocations(
        _In_ PeExecutable_t*    ParentImage,
        _In_ PeExecutable_t*    Image,
        _In_ SectionMapping_t*  Sections,
        _In_ int                SectionCount,
        _In_ uint8_t*           DirectoryContent,
        _In_ size_t             DirectorySize)
{
    RuntimeRelocationHeader_t* header = (RuntimeRelocationHeader_t*)DirectoryContent;
    uint8_t*                   endOfEntries = DirectoryContent + DirectorySize;
    TRACE("PeHandleRuntimeRelocations(size=%" PRIuIN ")", DirectorySize);

    if (DirectorySize < 8) {
        return OsInvalidParameters;
    }

    if (DirectorySize >= 12 && header->Magic0 == 0 && header->Magic1 == 0) {
        uint8_t* entries = DirectoryContent + sizeof(RuntimeRelocationHeader_t);
        if (header->Version == RP_VERSION_1) {
            HandleRelocationsV1(Sections, SectionCount, entries, endOfEntries);
        }
        else if (header->Version == RP_VERSION_2) {
            return HandleRelocationsV2(Image, Sections, SectionCount, entries, endOfEntries);
        }
        else {
            return OsInvalidParameters;
        }
    }
    HandleRelocationsV1(Sections, SectionCount, DirectoryContent, endOfEntries);
    return OsOK;
}

oserr_t
PeHandleExports(
    _In_ PeExecutable_t*   ParentImage,
    _In_ PeExecutable_t*   Image,
    _In_ SectionMapping_t* Sections,
    _In_ int               SectionCount,
    _In_ uint8_t*          DirectoryContent,
    _In_ size_t            DirectorySize)
{
    SectionMapping_t*    Section;
    PeExportDirectory_t* ExportTable;
    uint32_t*            FunctionNamesTable;
    uint16_t*            FunctionOrdinalsTable;
    uint32_t*            FunctionAddressTable;
    size_t               FunctionNameLengths;
    int                  OrdinalBase;
    int                  i;

    TRACE("PeHandleExports(%ms, Address 0x%x, Size 0x%x)",
        Image->Name, DirectoryContent, DirectorySize);

    // The following tables are the access we need
    ExportTable = (PeExportDirectory_t*)DirectoryContent;
    if (ExportTable->AddressOfFunctions == 0) {
        int Index = 0;
        WARNING("Export table present, but address of functions table is zero.");
        WARNING("ExportTable(0x%x, %u) => Size of Directory %u", 
            ExportTable->Flags, ExportTable->NumberOfFunctions, DirectorySize);
        for (i = 0; i < 4; i++, Index += 16) {
            WARNING("%x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x", DirectoryContent[Index],
                DirectoryContent[Index + 1], DirectoryContent[Index + 2], DirectoryContent[Index + 3],
                DirectoryContent[Index + 4], DirectoryContent[Index + 5], DirectoryContent[Index + 6],
                DirectoryContent[Index + 7], DirectoryContent[Index + 8], DirectoryContent[Index + 9],
                DirectoryContent[Index + 10], DirectoryContent[Index + 11], DirectoryContent[Index + 12],
                DirectoryContent[Index + 13], DirectoryContent[Index + 14], DirectoryContent[Index + 15]);
        }
        return OsError;
    }

    Section               = GetSectionFromRVA(Sections, SectionCount, ExportTable->AddressOfFunctions);
    FunctionNamesTable    = (uint32_t*)OFFSET_IN_SECTION(Section, ExportTable->AddressOfNames);
    FunctionOrdinalsTable = (uint16_t*)OFFSET_IN_SECTION(Section, ExportTable->AddressOfOrdinals);
    FunctionAddressTable  = (uint32_t*)OFFSET_IN_SECTION(Section, ExportTable->AddressOfFunctions);
    OrdinalBase           = (int)(ExportTable->OrdinalBase);

    // Allocate array for exports
    Image->NumberOfExportedFunctions = (int)ExportTable->NumberOfNames;
    Image->ExportedFunctions         = (PeExportedFunction_t*)malloc(sizeof(PeExportedFunction_t) * Image->NumberOfExportedFunctions);
    memset(Image->ExportedFunctions, 0, sizeof(PeExportedFunction_t) * Image->NumberOfExportedFunctions);

    // Fill the exported list, we export all the function addresses first with ordinals, and get
    // an idea of how much space we need to store names of each function
    TRACE("Number of functions to iterate: %u", Image->NumberOfExportedFunctions);
    FunctionNameLengths = 0;
    for (i = 0; i < Image->NumberOfExportedFunctions; i++) {
        PeExportedFunction_t* ExFunc      = &Image->ExportedFunctions[i];
        uintptr_t             NameAddress = OFFSET_IN_SECTION(Section, FunctionNamesTable[i]);

        // Count up the number of bytes we need for a name buffer
        ExFunc->Ordinal      = FunctionOrdinalsTable[i] - OrdinalBase;
        ExFunc->Address      = (uintptr_t)(Image->VirtualAddress + FunctionAddressTable[ExFunc->Ordinal]);

        // Handle forwarded names
        if (ExFunc->Address >= (uintptr_t)DirectoryContent &&
            ExFunc->Address < ((uintptr_t)DirectoryContent + DirectorySize)) {
            NameAddress = OFFSET_IN_SECTION(Section, FunctionAddressTable[i]);
        }
        else {
            uintptr_t MaxImageValue = (ParentImage == NULL) ? Image->NextLoadingAddress : ParentImage->NextLoadingAddress; 
            if (!ISINRANGE(ExFunc->Address, Image->CodeBase, MaxImageValue)) {
                ERROR("%ms: Address 0x%x (Table RVA value: 0x%x), %i",
                      Image->Name, ExFunc->Address, FunctionAddressTable[ExFunc->Ordinal], i);
                ERROR("The function to export was located outside the image code boundaries (0x%x => 0x%x)",
                    Image->CodeBase, MaxImageValue);
                ERROR("ExportTable->NumberOfFunctions [%u]", ExportTable->NumberOfFunctions);
                ERROR("ExportTable->AddressOfFunctions [0x%x]", ExportTable->AddressOfFunctions);
                assert(0);
            }
        }
        FunctionNameLengths += strlen((const char*)NameAddress) + 1;
    }

    // Allocate name array
    TRACE("Number of names/ordinals to iterate: %u (name table size %u)", ExportTable->NumberOfNames, FunctionNameLengths);
    Image->ExportedFunctionNames = (char*)malloc(FunctionNameLengths);
    FunctionNameLengths          = 0;
    for (i = 0; i < ExportTable->NumberOfNames; i++) {
        PeExportedFunction_t* ExFunc     = &Image->ExportedFunctions[i];
        char*                 NameBuffer = &Image->ExportedFunctionNames[FunctionNameLengths];
        uintptr_t             NameAddress;
        size_t                FunctionLength;

        // Handle the forwarded names
        if (ExFunc->Address >= (uintptr_t)DirectoryContent &&
            ExFunc->Address < ((uintptr_t)DirectoryContent + DirectorySize)) {
            NameAddress = OFFSET_IN_SECTION(Section, FunctionAddressTable[i]);
        }
        else {
            NameAddress = OFFSET_IN_SECTION(Section, FunctionNamesTable[i]);
        }
        FunctionLength = strlen((const char*)NameAddress) + 1;

        memcpy(NameBuffer, (const char*)NameAddress, FunctionLength);
        ExFunc->Name         = NameBuffer;
        FunctionNameLengths += FunctionLength;
    }
    return OsOK;
}

oserr_t
PeHandleImports(
    _In_ PeExecutable_t*    ParentImage,
    _In_ PeExecutable_t*    Image,
    _In_ SectionMapping_t*  Sections,
    _In_ int                SectionCount,
    _In_ uint8_t*           DirectoryContent,
    _In_ size_t             DirectorySize)
{
    PeImportDescriptor_t* ImportDescriptor = (PeImportDescriptor_t*)DirectoryContent;
    while (ImportDescriptor->ImportAddressTable != 0) {
        SectionMapping_t* Section         = GetSectionFromRVA(Sections, SectionCount, ImportDescriptor->ImportAddressTable);
        uintptr_t         HostNameAddress = OFFSET_IN_SECTION(Section, ImportDescriptor->ModuleName);
        mstring_t*        Name            = mstr_new_u8((const char*)HostNameAddress);
        oserr_t        Status          = PeResolveImportDescriptor(ParentImage, Image, Section, ImportDescriptor, Name);
        mstr_delete(Name);

        if (Status != OsOK) {
            return OsError;
        }
        ImportDescriptor++;
    }
    return OsOK;
}

static oserr_t
PeParseAndMapImage(
    _In_ PeExecutable_t*    Parent,
    _In_ PeExecutable_t*    Image,
    _In_ uint8_t*           ImageBuffer,
    _In_ size_t             SizeOfMetaData,
    _In_ uintptr_t          SectionBase,
    _In_ int                SectionCount,
    _In_ PeDataDirectory_t* Directories)
{
    uintptr_t          VirtualAddress = Image->VirtualAddress;
    uint8_t*           DirectoryContents[PE_NUM_DIRECTORIES] = { 0 };
    SectionMapping_t*  SectionMappings;
    MemoryMapHandle_t MapHandle;
    oserr_t        osStatus;
    clock_t           Timing;
    int                i, j;
    WARNING("%ms: loading at 0x%" PRIxIN, Image->Name, Image->VirtualAddress);

    // Copy metadata of image to base address
    osStatus = PeImplAcquireImageMapping(Image->MemorySpace, &VirtualAddress, SizeOfMetaData,
                                         MEMORY_READ | MEMORY_WRITE, &MapHandle);
    if (osStatus != OsOK) {
        ERROR("Failed to map pe's metadata, out of memory?");
        return OsError;
    }
    memcpy((void*)VirtualAddress, ImageBuffer, SizeOfMetaData);
    PeImplReleaseImageMapping(MapHandle);

    // Allocate an array of mappings that we can keep sections in
    SectionMappings = (SectionMapping_t*)malloc(sizeof(SectionMapping_t) * SectionCount);
    memset(SectionMappings, 0, sizeof(SectionMapping_t) * SectionCount);

    // Now we want to handle all the directories and sections in the image
    TRACE("Handling sections and data directory mappings");
    osStatus = PeHandleSections(Parent, Image, ImageBuffer, SectionBase, SectionCount, SectionMappings);
    if (osStatus != OsOK) {
        return OsError;
    }
    
    // Do we have a data directory in this section? Or multiple?
    for (i = 0; i < SectionCount; i++) {
        uintptr_t SectionStart = SectionMappings[i].RVA;
        uintptr_t SectionEnd   = SectionMappings[i].RVA + SectionMappings[i].Size;
        for (j = 0; j < PE_NUM_DIRECTORIES; j++) {
            if (Directories[j].AddressRVA == 0 || Directories[j].Size == 0) {
                continue;
            }
            if (DirectoryContents[j] == NULL) {
                if (Directories[j].AddressRVA >= SectionStart &&
                    (Directories[j].AddressRVA + Directories[j].Size) <= SectionEnd) {
                    // Directory is contained in this section
                    DirectoryContents[j] = SectionMappings[i].BasePointer + (Directories[j].AddressRVA - SectionStart);
                }
            }
        }
    }

    // Add us to parent before handling data-directories
    if (Parent != NULL) {
        list_append(Parent->Libraries, &Image->Header);
    }

    // Handle all the data directories, if they are present
    for (i = 0; i < PE_NUM_DIRECTORIES; i++) {
        int dataDirectoryIndex = DataDirectoryHandlers[i].Index;
        if (dataDirectoryIndex == PE_NUM_DIRECTORIES) {
            break; // End of list of handlers
        }

        // Is there any directory available for the handler?
        if (DirectoryContents[dataDirectoryIndex] != NULL) {
            TRACE("parsing data-directory[%i]", dataDirectoryIndex);
            Timing = PeImplGetTimestampMs();
            osStatus = DataDirectoryHandlers[i].Handler(Parent, Image, SectionMappings, SectionCount,
                                                        DirectoryContents[dataDirectoryIndex],
                                                        Directories[dataDirectoryIndex].Size);
            if (osStatus != OsOK) {
                ERROR("handling of data-directory failed, status %u", osStatus);
            }
            TRACE("directory[%i]: %u ms", dataDirectoryIndex, PeImplGetTimestampMs() - Timing);
        }
    }

    // Free all the section mappings
    for (i = 0; i < SectionCount; i++) {
        if (SectionMappings[i].Handle != NULL) {
            PeImplReleaseImageMapping(SectionMappings[i].Handle);
        }
    }
    free(SectionMappings);
    return osStatus;
}

static oserr_t
__ResolveImagePath(
        _In_  uuid_t          owner,
        _In_  PeExecutable_t* parent,
        _In_  mstring_t*      path,
        _Out_ uint8_t**       bufferOut,
        _Out_ mstring_t**     fullPathOut)
{
    mstring_t* fullPath;
    uint8_t*   buffer;
    size_t     length;
    oserr_t    osStatus;

    osStatus = PeImplResolveFilePath(
            owner,
            (parent ? parent->FullPath : NULL),
            path,
            &fullPath
    );
    if (osStatus != OsOK) {
        ERROR("Failed to resolve path for executable: %ms (%u)", path, osStatus);
        return osStatus;
    }
    
    // Load the file
    osStatus = PeImplLoadFile(fullPath, (void**)&buffer, &length);
    if (osStatus != OsOK) {
        ERROR("Failed to load file for path %ms (%u)", fullPath, osStatus);
        mstr_delete(fullPath);
        return osStatus;
    }

    *bufferOut   = buffer;
    *fullPathOut = fullPath;
    return PeValidateImageBuffer(buffer, length);
}

oserr_t
PeLoadImage(
        _In_  uuid_t           owner,
        _In_  PeExecutable_t*  parent,
        _In_  mstring_t*       path,
        _Out_ PeExecutable_t** imageOut)
{
    MzHeader_t*           dosHeader;
    PeHeader_t*           peHeader;
    PeOptionalHeader_t*   optionalHeader;
    PeOptionalHeader32_t* optionalHeader32;
    PeOptionalHeader64_t* optionalHeader64;

    mstring_t*         fullPath = NULL;
    uintptr_t          sectionAddress;
    uintptr_t          imageBase;
    size_t             sizeOfMetaData;
    PeDataDirectory_t* directoryPtr;
    PeExecutable_t*    image;
    oserr_t            osStatus;
    uint8_t*           buffer;
    int                index;

    TRACE("PeLoadImage(Path %ms, Parent %ms)", path, (parent == NULL) ? NULL : parent->Name);

    osStatus = __ResolveImagePath(owner, parent, path, &buffer, &fullPath);
    if (osStatus != OsOK) {
        if (fullPath != NULL) {
            mstr_delete(fullPath);
        }
        return osStatus;
    }

    dosHeader      = (MzHeader_t*)buffer;
    peHeader       = (PeHeader_t*)(buffer + dosHeader->PeHeaderAddress);
    optionalHeader = (PeOptionalHeader_t*)(buffer + dosHeader->PeHeaderAddress + sizeof(PeHeader_t));
    if (peHeader->Machine != PE_CURRENT_MACHINE) {
        ERROR("The image as built for machine type 0x%x, "
                "which is not the current machine type.",
                peHeader->Machine);
        return OsError;
    }

    // Validate the current architecture,
    // again we don't load 32 bit modules for 64 bit
    if (optionalHeader->Architecture != PE_CURRENT_ARCH) {
        ERROR("The image was built for architecture 0x%x, "
                "and was not supported by the current architecture.",
                optionalHeader->Architecture);
        return OsError;
    }

    // We need to re-cast based on architecture 
    // and handle them differnetly
    if (optionalHeader->Architecture == PE_ARCHITECTURE_32) {
        optionalHeader32 = (PeOptionalHeader32_t*)(buffer
                                                   + dosHeader->PeHeaderAddress + sizeof(PeHeader_t));
        imageBase      = optionalHeader32->BaseAddress;
        sizeOfMetaData = optionalHeader32->SizeOfHeaders;
        sectionAddress = (uintptr_t)(buffer + dosHeader->PeHeaderAddress
                                     + sizeof(PeHeader_t) + sizeof(PeOptionalHeader32_t));
        directoryPtr   = (PeDataDirectory_t*)&optionalHeader32->Directories[0];
    }
    else if (optionalHeader->Architecture == PE_ARCHITECTURE_64) {
        optionalHeader64 = (PeOptionalHeader64_t*)(buffer
                                                   + dosHeader->PeHeaderAddress + sizeof(PeHeader_t));
        imageBase      = (uintptr_t)optionalHeader64->BaseAddress;
        sizeOfMetaData = optionalHeader64->SizeOfHeaders;
        sectionAddress = (uintptr_t)(buffer + dosHeader->PeHeaderAddress
                                     + sizeof(PeHeader_t) + sizeof(PeOptionalHeader64_t));
        directoryPtr   = (PeDataDirectory_t*)&optionalHeader64->Directories[0];
    }
    else {
        ERROR("Unsupported architecture %u", optionalHeader->Architecture);
        return OsError;
    }

    image = (PeExecutable_t*)malloc(sizeof(PeExecutable_t));
    if (!image) {
        return OsOutOfMemory;
    }
    
    memset(image, 0, sizeof(PeExecutable_t));
    ELEMENT_INIT(&image->Header, 0, image);
    index = mstr_rfind_u8(fullPath, "/", -1);
    image->Name              = mstr_substr(fullPath, index + 1, -1);
    image->Owner             = owner;
    image->FullPath          = fullPath;
    image->Architecture      = optionalHeader->Architecture;
    image->VirtualAddress    = (parent == NULL) ? PeImplGetBaseAddress() : parent->NextLoadingAddress;
    image->Libraries         = malloc(sizeof(list_t));
    image->References        = 1;
    image->OriginalImageBase = imageBase;
    list_construct(image->Libraries);
    TRACE("library (%ms) => 0x%" PRIxIN, image->Name, image->VirtualAddress);

    // Set the entry point if there is any
    if (optionalHeader->EntryPoint != 0) {
        image->EntryAddress = image->VirtualAddress + optionalHeader->EntryPoint;
    }

    if (parent == NULL) {
        osStatus = PeImplCreateImageSpace(&image->MemorySpace);
        if (osStatus != OsOK) {
            ERROR("Failed to create pe's memory space");
            mstr_delete(image->Name);
            mstr_delete(image->FullPath);
            free(image->Libraries);
            free(image);
            return OsError;
        }
    }
    else {
        image->MemorySpace = parent->MemorySpace;
    }

    // Parse the headers, directories and handle them.
    osStatus = PeParseAndMapImage(parent, image, buffer, sizeOfMetaData, sectionAddress,
                                  (int)peHeader->NumSections, directoryPtr);
    PeImplUnloadFile((void*)buffer);
    if (osStatus != OsOK) {
        PeUnloadLibrary(parent, image);
        return OsError;
    }
    *imageOut = image;
    return OsOK;
}

oserr_t
PeUnloadImage(
    _In_ PeExecutable_t* image)
{
    if (!image) {
        return OsInvalidParameters;
    }

    TRACE("PeUnloadImage(image=%ms)", image->Name);

    mstr_delete(image->Name);
    mstr_delete(image->FullPath);
    if (image->ExportedFunctions) {
        free(image->ExportedFunctions);
    }

    if (image->Libraries != NULL) {
        element_t* i = list_front(image->Libraries);
        while (i) {
            list_remove(image->Libraries, i);
            PeUnloadImage(i->value);

            i = list_front(image->Libraries);
        }
    }
    free(image);
    return OsOK;
}

oserr_t
PeUnloadLibrary(
    _In_ PeExecutable_t* parent,
    _In_ PeExecutable_t* library)
{
    library->References--;

    // Sanitize the ref count
    // we might have to unload it if there are no more references
    if (library->References <= 0)  {
        if (parent != NULL) {
            foreach(i, parent->Libraries) {
                PeExecutable_t* libraryEntry = i->value;
                if (libraryEntry == library) {
                    list_remove(parent->Libraries, i);
                    break;
                }
            }
        }
        return PeUnloadImage(library);
    }
    return OsOK;
}
