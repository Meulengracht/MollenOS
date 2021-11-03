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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * PE/COFF Image Loader
 *    - Implements support for loading and processing pe/coff image formats
 *      and implemented as a part of libds to share between services and kernel
 */

#define __TRACE

#include <ds/ds.h>
#include <ds/list.h>
#include <ds/mstring.h>
#include <os/mollenos.h>
#include <string.h>
#include <assert.h>
#include "pe.h"

typedef struct SectionMapping {
    MemoryMapHandle_t Handle;
    uint8_t*          BasePointer;
    uintptr_t         RVA;
    size_t            Size;
} SectionMapping_t;

#define OFFSET_IN_SECTION(Section, _RVA) (uintptr_t)((Section)->BasePointer + ((_RVA) - (Section)->RVA))

// Directory handlers
OsStatus_t PeHandleRuntimeRelocations(PeExecutable_t*,PeExecutable_t*, SectionMapping_t*, int, uint8_t*, size_t);
OsStatus_t PeHandleRelocations(PeExecutable_t*,PeExecutable_t*, SectionMapping_t*, int, uint8_t*, size_t);
OsStatus_t PeHandleExports(PeExecutable_t*,PeExecutable_t*, SectionMapping_t*, int, uint8_t*, size_t);
OsStatus_t PeHandleImports(PeExecutable_t*,PeExecutable_t*, SectionMapping_t*, int, uint8_t*, size_t);

typedef OsStatus_t(*DataDirectoryHandler)(PeExecutable_t*, PeExecutable_t*, SectionMapping_t*, int, uint8_t*, size_t);
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
    dserror("GetSectionFromRVA(0x%x) => error segment not found", RVA);
    for (i = 0; i < SectionCount; i++) {
        uintptr_t SectionStart = SectionMappings[i].RVA;
        uintptr_t SectionEnd   = SectionMappings[i].RVA + SectionMappings[i].Size;
        dswarning("section(%i) => 0x%x, 0x%x", i, SectionStart, SectionEnd);
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

static OsStatus_t
PeHandleSections(
    _In_ PeExecutable_t*   Parent,
    _In_ PeExecutable_t*   Image,
    _In_ uint8_t*          Data,
    _In_ uintptr_t         SectionAddress,
    _In_ int               SectionCount,
    _In_ SectionMapping_t* SectionHandles)
{
    PeSectionHeader_t* Section        = (PeSectionHeader_t*)SectionAddress;
    uintptr_t          CurrentAddress = Image->VirtualAddress;
    OsStatus_t         Status;
    MemoryMapHandle_t  MapHandle;
    char               SectionName[PE_SECTION_NAME_LENGTH + 1];
    int                i;

    for (i = 0; i < SectionCount; i++) {
        // Calculate pointers, we need two of them, one that
        // points to data in file, and one that points to where
        // in memory we want to copy data to
        uintptr_t    VirtualDestination = Image->VirtualAddress + Section->VirtualAddress;
        uint8_t*     FileBuffer         = (uint8_t*)(Data + Section->RawAddress);
        unsigned int PageFlags          = MEMORY_READ;
        size_t       SectionSize        = MAX(Section->RawSize, Section->VirtualSize);
        uint8_t*     Destination;

        // Make a local copy of the name, just in case
        // we need to do some debug print
        memcpy(&SectionName[0], &Section->Name[0], 8);
        SectionName[8] = 0;

        // Handle page flags for this section
        if (Section->Flags & PE_SECTION_EXECUTE) {
            PageFlags |= MEMORY_EXECUTABLE;
        }
        if (Section->Flags & PE_SECTION_WRITE) {
            PageFlags |= MEMORY_WRITE;
        }

        // Iterate pages and map them in our memory space
        Status = AcquireImageMapping(Image->MemorySpace, &VirtualDestination, SectionSize, PageFlags, &MapHandle);
        if (Status != OsSuccess) {
            dserror("%s: Failed to map section %s at 0x%" PRIxIN ": %u", 
                MStringRaw(Image->Name), &SectionName[0], VirtualDestination, Status);
            return Status;
        }
        Destination = (uint8_t*)VirtualDestination;

        SectionHandles[i].Handle      = MapHandle;
        SectionHandles[i].BasePointer = Destination;
        SectionHandles[i].RVA         = Section->VirtualAddress;
        SectionHandles[i].Size        = SectionSize;

        // Store first code segment we encounter
        if (Section->Flags & PE_SECTION_CODE) {
            if (Image->CodeBase == 0) {
                Image->CodeBase = (uintptr_t)Image->VirtualAddress + Section->VirtualAddress;
                Image->CodeSize = Section->VirtualSize;
            }
        }

        // Handle sections specifics, we want to:
        // BSS: Zero out the memory 
        // Code: Copy memory 
        // Data: Copy memory
        if (Section->RawSize == 0 || (Section->Flags & PE_SECTION_BSS)) {
            dstrace("section(%i): clearing %u bytes => 0x%x (0x%x, 0x%x)", i, Section->VirtualSize, Destination,
                Image->VirtualAddress + Section->VirtualAddress, PageFlags);
            memset(Destination, 0, Section->VirtualSize);
        }
        else if ((Section->Flags & PE_SECTION_CODE) || (Section->Flags & PE_SECTION_DATA)) {
            dstrace("section(%i): copying %u bytes 0x%" PRIxIN " => 0x%" PRIxIN " (0x%" PRIxIN ", 0x%x)", i, 
                Section->RawSize, FileBuffer, Destination,
                Image->VirtualAddress + Section->VirtualAddress, PageFlags);
            memcpy(Destination, FileBuffer, Section->RawSize);

            // Sanitize this special case, if the virtual size
            // is large, this means there needs to be zeroed space afterwards
            if (Section->VirtualSize > Section->RawSize) {
                Destination += Section->RawSize;
                dstrace("section(%i): clearing %u bytes => 0x%x (0x%x, 0x%x)", i, Section->VirtualSize - Section->RawSize, 
                    Destination, Image->VirtualAddress + Section->VirtualAddress, PageFlags);
                memset(Destination, 0, (Section->VirtualSize - Section->RawSize));
            }
        }
        CurrentAddress = (Image->VirtualAddress + Section->VirtualAddress + SectionSize);
        Section++;
    }

    // Return a page-aligned address that points to the
    // next free relocation address
    if (CurrentAddress % GetPageSize()) {
        CurrentAddress += (GetPageSize() - (CurrentAddress % GetPageSize()));
    }

    if (Parent != NULL) Parent->NextLoadingAddress = CurrentAddress;
    else                Image->NextLoadingAddress  = CurrentAddress;
    return OsSuccess;
}

static OsStatus_t
PeResolveImportDescriptor(
    _In_ PeExecutable_t*       ParentImage,
    _In_ PeExecutable_t*       Image,
    _In_ SectionMapping_t*     Section,
    _In_ PeImportDescriptor_t* ImportDescriptor,
    _In_ MString_t*            ImportDescriptorName)
{
    PeExecutable_t*           ResolvedLibrary;
    PeExportedFunction_t*     Exports;
    int                       NumberOfExports;
    uintptr_t                 AddressOfImportTable;
    PeImportNameDescriptor_t* NameDescriptor;

    dstrace("PeResolveImportDescriptor(%s, %s)", 
        MStringRaw(Image->Name), MStringRaw(ImportDescriptorName));

    // Resolve the library from the import chunk
    ResolvedLibrary = PeResolveLibrary(ParentImage, Image, ImportDescriptorName);
    if (ResolvedLibrary == NULL || ResolvedLibrary->ExportedFunctions == NULL) {
        dserror("(%s): Failed to resolve library %s", MStringRaw(Image->Name), MStringRaw(ImportDescriptorName));
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
                    dserror("Failed to locate function (%i)", Ordinal);
                    return OsError;
                }
            }
            else {
                NameDescriptor = (PeImportNameDescriptor_t*)OFFSET_IN_SECTION(Section, Value & PE_IMPORT_NAMEMASK);
                Function       = GetExportedFunctionByNameDescriptor(Exports, NumberOfExports, NameDescriptor);
                if (!Function) {
                    dserror("Failed to locate function (%s)", &NameDescriptor->Name[0]);
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
                    dserror("Failed to locate function (%i)", Ordinal);
                    return OsError;
                }
            }
            else {
                NameDescriptor = (PeImportNameDescriptor_t*)OFFSET_IN_SECTION(Section, Value & PE_IMPORT_NAMEMASK);
                Function       = GetExportedFunctionByNameDescriptor(Exports, NumberOfExports, NameDescriptor);
                if (!Function) {
                    dserror("Failed to locate function (%s)", &NameDescriptor->Name[0]);
                    return OsError;
                }
            }
            *ThunkPointer = (uint64_t)Function->Address;
            ThunkPointer++;
        }
    }
    return OsSuccess;
}

OsStatus_t
PeHandleRelocations(
    _In_ PeExecutable_t*    ParentImage,
    _In_ PeExecutable_t*    Image,
    _In_ SectionMapping_t*  Sections,
    _In_ int                SectionCount,
    _In_ uint8_t*           DirectoryContent,
    _In_ size_t             DirectorySize)
{
    size_t    BytesLeft         = DirectorySize;
    uint32_t* RelocationPointer = (uint32_t*)DirectoryContent;
    uintptr_t ImageDelta        = Image->VirtualAddress - Image->OriginalImageBase;
    uint32_t  i;

    // Sanitize the image delta
    if (ImageDelta == 0) {
        return OsSuccess;
    }

    while (BytesLeft > 0) {
        uint16_t*         RelocationTable;
        uint32_t          NumRelocs;
        SectionMapping_t* Section;
        uintptr_t sectionOffset;

        // Get the next block
        uint32_t PageRVA   = RelocationPointer[0];
        uint32_t BlockSize = RelocationPointer[1];
        if (PageRVA == 0 || BlockSize == 0) {
            int Index = 0;
            dserror("%s: Directory 0x%" PRIxIN ", Size %" PRIuIN, 
                MStringRaw(Image->Name), DirectoryContent, DirectorySize);
            dserror("BytesLeft %" PRIuIN ", ", BytesLeft);
            dserror("PageRVA is 0, BlockSize %u", BlockSize);
            for (i = 0; i < 4; i++, Index += 16) {
                dswarning("%x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x", DirectoryContent[Index],
                    DirectoryContent[Index + 1], DirectoryContent[Index + 2], DirectoryContent[Index + 3],
                    DirectoryContent[Index + 4], DirectoryContent[Index + 5], DirectoryContent[Index + 6],
                    DirectoryContent[Index + 7], DirectoryContent[Index + 8], DirectoryContent[Index + 9],
                    DirectoryContent[Index + 10], DirectoryContent[Index + 11], DirectoryContent[Index + 12],
                    DirectoryContent[Index + 13], DirectoryContent[Index + 14], DirectoryContent[Index + 15]);
            }
            assert(0);
        }

        Section       = GetSectionFromRVA(Sections, SectionCount, PageRVA);
        sectionOffset = OFFSET_IN_SECTION(Section, PageRVA);

        if (BlockSize > BytesLeft) {
            dserror("Invalid relocation data: BlockSize > BytesLeft, bailing");
            assert(0);
        }
        
        NumRelocs       = (BlockSize - 8) / sizeof(uint16_t);
        RelocationTable = (uint16_t*)&RelocationPointer[2];

        for (i = 0; i < NumRelocs; i++) {
            uint16_t RelocationEntry = RelocationTable[i];
            uint16_t Type            = (RelocationEntry >> 12);
            uint16_t Value           = RelocationEntry & 0x0FFF;
            
            // 32/64 Bit Relative
            if (Type == PE_RELOCATION_HIGHLOW || Type == PE_RELOCATION_RELATIVE64) { 
                // Create a pointer, the low 12 bits have an offset into the PageRVA
                volatile uintptr_t* AddressPointer = (volatile uintptr_t*)(sectionOffset + Value);
                uintptr_t           UpdatedAddress = *AddressPointer + ImageDelta;
#if __BITS == 32
                if (UpdatedAddress < Image->VirtualAddress || UpdatedAddress >= 0x30000000) {
                    dserror("%s: Rel %u, Value %u (%u/%u)", MStringRaw(Image->Name), Type, Value, i, NumRelocs);
                    dserror("PageRVA 0x%x of SectionRVA 0x%x. Current blocksize %u", PageRVA, Section->RVA, BlockSize);
                    dserror("Section 0x%x, SectionAddress 0x%x, Address 0x%x, Value 0x%x", 
                        Section->BasePointer, sectionOffset, AddressPointer, *AddressPointer);
                    dserror("Relocation caused invalid pointer: 0x%x, 0x%x, New Base 0x%x, Old Base 0x%x",
                        UpdatedAddress, ImageDelta, Image->VirtualAddress, Image->OriginalImageBase);
                    assert(0);
                }
#endif
#if __BITS == 64
                if (UpdatedAddress < Image->VirtualAddress || UpdatedAddress >= 0x8100000000ULL) {
                    dserror("%s: Rel %u, Value %u (%u/%u)", MStringRaw(Image->Name), Type, Value, i, NumRelocs);
                    dserror("PageRVA 0x%x of SectionRVA 0x%x. Current blocksize %u", PageRVA, Section->RVA, BlockSize);
                    dserror("Section 0x%x, SectionAddress 0x%x, Address 0x%x, Value 0x%x",
                            Section->BasePointer, sectionOffset, AddressPointer, *AddressPointer);
                    dserror("Relocation caused invalid pointer: 0x%llx, 0x%llx, New Base 0x%llx, Old Base 0x%llx",
                        UpdatedAddress, ImageDelta, Image->VirtualAddress, Image->OriginalImageBase);
                    assert(0);
                }
#endif
                *AddressPointer = UpdatedAddress;
            }
            else if (Type == PE_RELOCATION_ALIGN) {
                // End of alignment
                break;
            }
            else {
                dserror("Implement support for reloc type: %u", Type);
                assert(0);
            }
        }
        RelocationPointer += (BlockSize / sizeof(uint32_t));
        BytesLeft         -= BlockSize;
    }
    return OsSuccess;
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
    dstrace("HandleRelocationsV1(start=0x%" PRIxIN ", end=0x%" PRIxIN ")", RelocationEntries, RelocationsEnd);

    while ((uint8_t*)entry < RelocationsEnd) {
        uintptr_t sectionOffset = GetOffsetInSectionFromRVA(Sections, SectionCount, entry->Offset);
        dstrace("HandleRelocationsV1 sectionOffset=0x%" PRIxIN " from entry->Offset=0x%x",
                sectionOffset, entry->Offset);
        dstrace("HandleRelocationsV1 value=0x%" PRIxIN " += 0x%x",
                *((uintptr_t*)sectionOffset), entry->Value);

        *((uintptr_t*)sectionOffset) += entry->Value;
        entry++;
    }
}

static OsStatus_t HandleRelocationsV2(
        _In_ PeExecutable_t*   Image,
        _In_ SectionMapping_t* Sections,
        _In_ int               SectionCount,
        _In_ uint8_t*          RelocationEntries,
        _In_ const uint8_t*    RelocationsEnd)
{
    RuntimeRelocationEntryV2_t* entry = (RuntimeRelocationEntryV2_t*)RelocationEntries;
    dstrace("HandleRelocationsV2(start=0x%" PRIxIN ", end=0x%" PRIxIN ")", RelocationEntries, RelocationsEnd);

    while ((uint8_t*)entry < RelocationsEnd) {
        uintptr_t symbolSectionAddress = GetOffsetInSectionFromRVA(Sections, SectionCount, entry->Symbol);
        uintptr_t targetSectionAddress = GetOffsetInSectionFromRVA(Sections, SectionCount, entry->Offset);
        intptr_t  symbolValue = *((intptr_t*)symbolSectionAddress);
        uint8_t   relocSize = (uint8_t)(entry->Flags & 0xFF);
        intptr_t  relocData;

        dstrace("HandleRelocationsV2 symbolSectionAddress=0x%" PRIxIN " from entry->Symbol=0x%x",
                symbolSectionAddress, entry->Symbol);
        dstrace("HandleRelocationsV2 targetSectionAddress=0x%" PRIxIN " from entry->Offset=0x%x",
                targetSectionAddress, entry->Offset);
        dstrace("HandleRelocationsV2 symbolValue=0x%" PRIxIN ", relocSize=0x%x",
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
                dserror("HandleRelocationsV2 invalid relocation size %u", relocSize);
                return OsError;
            }
        }

        dstrace("HandleRelocationsV2 relocData=0x%" PRIxIN ", Image->VirtualAddress=0x%" PRIxIN,
                relocData, Image->VirtualAddress);
        relocData -= ((intptr_t)Image->VirtualAddress + entry->Symbol);
        relocData += symbolValue;
        dstrace("HandleRelocationsV2 relocData=0x%" PRIxIN, relocData);

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
    return OsSuccess;
}

OsStatus_t PeHandleRuntimeRelocations(
        _In_ PeExecutable_t*    ParentImage,
        _In_ PeExecutable_t*    Image,
        _In_ SectionMapping_t*  Sections,
        _In_ int                SectionCount,
        _In_ uint8_t*           DirectoryContent,
        _In_ size_t             DirectorySize)
{
    RuntimeRelocationHeader_t* header = (RuntimeRelocationHeader_t*)DirectoryContent;
    uint8_t*                   endOfEntries = DirectoryContent + DirectorySize;
    dstrace("PeHandleRuntimeRelocations(size=%" PRIuIN ")", DirectorySize);

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
    return OsSuccess;
}

OsStatus_t
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

    dstrace("PeHandleExports(%s, Address 0x%x, Size 0x%x)",
        MStringRaw(Image->Name), DirectoryContent, DirectorySize);

    // The following tables are the access we need
    ExportTable = (PeExportDirectory_t*)DirectoryContent;
    if (ExportTable->AddressOfFunctions == 0) {
        int Index = 0;
        dswarning("Export table present, but address of functions table is zero.");
        dswarning("ExportTable(0x%x, %u) => Size of Directory %u", 
            ExportTable->Flags, ExportTable->NumberOfFunctions, DirectorySize);
        for (i = 0; i < 4; i++, Index += 16) {
            dswarning("%x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x", DirectoryContent[Index],
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
    Image->ExportedFunctions         = (PeExportedFunction_t*)dsalloc(sizeof(PeExportedFunction_t) * Image->NumberOfExportedFunctions);
    memset(Image->ExportedFunctions, 0, sizeof(PeExportedFunction_t) * Image->NumberOfExportedFunctions);

    // Fill the exported list, we export all the function addresses first with ordinals, and get
    // an idea of how much space we need to store names of each function
    dstrace("Number of functions to iterate: %u", Image->NumberOfExportedFunctions);
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
                dserror("%s: Address 0x%x (Table RVA value: 0x%x), %i", 
                    MStringRaw(Image->Name), ExFunc->Address, FunctionAddressTable[ExFunc->Ordinal], i);
                dserror("The function to export was located outside the image code boundaries (0x%x => 0x%x)",
                    Image->CodeBase, MaxImageValue);
                dserror("ExportTable->NumberOfFunctions [%u]", ExportTable->NumberOfFunctions);
                dserror("ExportTable->AddressOfFunctions [0x%x]", ExportTable->AddressOfFunctions);
                assert(0);
            }
        }
        FunctionNameLengths += strlen((const char*)NameAddress) + 1;
    }

    // Allocate name array
    dstrace("Number of names/ordinals to iterate: %u (name table size %u)", ExportTable->NumberOfNames, FunctionNameLengths);
    Image->ExportedFunctionNames = (char*)dsalloc(FunctionNameLengths);
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
    return OsSuccess;
}

OsStatus_t
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
        MString_t*        Name            = MStringCreate((const char*)HostNameAddress, StrUTF8);
        OsStatus_t        Status          = PeResolveImportDescriptor(ParentImage, Image, Section, ImportDescriptor, Name);
        MStringDestroy(Name);

        if (Status != OsSuccess) {
            return OsError;
        }
        ImportDescriptor++;
    }
    return OsSuccess;
}

static OsStatus_t
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
    OsStatus_t        osStatus;
    clock_t           Timing;
    int                i, j;
    dswarning("%s: loading at 0x%" PRIxIN, MStringRaw(Image->Name), Image->VirtualAddress);

    // Copy metadata of image to base address
    osStatus = AcquireImageMapping(Image->MemorySpace, &VirtualAddress, SizeOfMetaData,
        MEMORY_READ | MEMORY_WRITE, &MapHandle);
    if (osStatus != OsSuccess) {
        dserror("Failed to map pe's metadata, out of memory?");
        return OsError;
    }
    memcpy((void*)VirtualAddress, ImageBuffer, SizeOfMetaData);
    ReleaseImageMapping(MapHandle);

    // Allocate an array of mappings that we can keep sections in
    SectionMappings = (SectionMapping_t*)dsalloc(sizeof(SectionMapping_t) * SectionCount);
    memset(SectionMappings, 0, sizeof(SectionMapping_t) * SectionCount);

    // Now we want to handle all the directories and sections in the image
    dstrace("Handling sections and data directory mappings");
    osStatus = PeHandleSections(Parent, Image, ImageBuffer, SectionBase, SectionCount, SectionMappings);
    if (osStatus != OsSuccess) {
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
        ELEMENT_INIT(&Image->Header, 0, Image);
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
            dstrace("parsing data-directory[%i]", dataDirectoryIndex);
            Timing = GetTimestamp();
            osStatus = DataDirectoryHandlers[i].Handler(Parent, Image, SectionMappings, SectionCount,
                                                        DirectoryContents[dataDirectoryIndex],
                                                        Directories[dataDirectoryIndex].Size);
            if (osStatus != OsSuccess) {
                dserror("handling of data-directory failed, status %u", osStatus);
            }
            dstrace("directory[%i]: %u ms", dataDirectoryIndex, GetTimestamp() - Timing);
        }
    }

    // Free all the section mappings
    for (i = 0; i < SectionCount; i++) {
        if (SectionMappings[i].Handle != NULL) {
            ReleaseImageMapping(SectionMappings[i].Handle);
        }
    }
    dsfree(SectionMappings);
    return osStatus;
}

static OsStatus_t
ResolvePeImagePath(
    _In_  UUId_t           Owner,
    _In_  MString_t*       Path,
    _Out_ uint8_t**        BufferOut,
    _Out_ MString_t**      FullPathOut)
{
    MString_t* FullPath;
    uint8_t*   Buffer;
    size_t     Length;
    OsStatus_t Status;

    // Resolve the path first
    Status = ResolveFilePath(Owner, Path, &FullPath);
    if (Status != OsSuccess) {
        dserror("Failed to resolve path for executable: %s (%u)", MStringRaw(Path), Status);
        return Status;
    }
    
    // Load the file
    Status = LoadFile(FullPath, (void**)&Buffer, &Length);
    if (Status != OsSuccess) {
        dserror("Failed to load file for path %s (%u)", MStringRaw(FullPath), Status);
        MStringDestroy(FullPath);
        return Status;
    }

    *BufferOut   = Buffer;
    *FullPathOut = FullPath;
    return PeValidateImageBuffer(Buffer, Length);
}

OsStatus_t
PeLoadImage(
    _In_  UUId_t           Owner,
    _In_  PeExecutable_t*  Parent,
    _In_  MString_t*       Path,
    _Out_ PeExecutable_t** ImageOut)
{
    MzHeader_t*           DosHeader;
    PeHeader_t*           BaseHeader;
    PeOptionalHeader_t*   OptHeader;
    PeOptionalHeader32_t* OptHeader32;
    PeOptionalHeader64_t* OptHeader64;

    MString_t*         FullPath = NULL;
    uintptr_t          SectionAddress;
    uintptr_t          ImageBase;
    size_t             SizeOfMetaData;
    PeDataDirectory_t* DirectoryPtr;
    PeExecutable_t*    Image;
    OsStatus_t         Status;
    uint8_t*           Buffer;
    int                Index;

    dstrace("PeLoadImage(Path %s, Parent %s)",
        MStringRaw(Path), (Parent == NULL) ? "None" : MStringRaw(Parent->Name));
    
    Status = ResolvePeImagePath(Owner, Path, &Buffer, &FullPath);
    if (Status != OsSuccess) {
        if (FullPath != NULL) {
            MStringDestroy(FullPath);
        }
        return Status;
    }

    DosHeader  = (MzHeader_t*)Buffer;
    BaseHeader = (PeHeader_t*)(Buffer + DosHeader->PeHeaderAddress);
    OptHeader  = (PeOptionalHeader_t*)(Buffer + DosHeader->PeHeaderAddress + sizeof(PeHeader_t));
    if (BaseHeader->Machine != PE_CURRENT_MACHINE) {
        dserror("The image as built for machine type 0x%x, "
                "which is not the current machine type.", 
                BaseHeader->Machine);
        return OsError;
    }

    // Validate the current architecture,
    // again we don't load 32 bit modules for 64 bit
    if (OptHeader->Architecture != PE_CURRENT_ARCH) {
        dserror("The image was built for architecture 0x%x, "
                "and was not supported by the current architecture.", 
                OptHeader->Architecture);
        return OsError;
    }

    // We need to re-cast based on architecture 
    // and handle them differnetly
    if (OptHeader->Architecture == PE_ARCHITECTURE_32) {
        OptHeader32     = (PeOptionalHeader32_t*)(Buffer 
            + DosHeader->PeHeaderAddress + sizeof(PeHeader_t));
        ImageBase       = OptHeader32->BaseAddress;
        SizeOfMetaData  = OptHeader32->SizeOfHeaders;
        SectionAddress  = (uintptr_t)(Buffer + DosHeader->PeHeaderAddress 
            + sizeof(PeHeader_t) + sizeof(PeOptionalHeader32_t));
        DirectoryPtr    = (PeDataDirectory_t*)&OptHeader32->Directories[0];
    }
    else if (OptHeader->Architecture == PE_ARCHITECTURE_64) {
        OptHeader64     = (PeOptionalHeader64_t*)(Buffer 
            + DosHeader->PeHeaderAddress + sizeof(PeHeader_t));
        ImageBase       = (uintptr_t)OptHeader64->BaseAddress;
        SizeOfMetaData  = OptHeader64->SizeOfHeaders;
        SectionAddress  = (uintptr_t)(Buffer + DosHeader->PeHeaderAddress 
            + sizeof(PeHeader_t) + sizeof(PeOptionalHeader64_t));
        DirectoryPtr    = (PeDataDirectory_t*)&OptHeader64->Directories[0];
    }
    else {
        dserror("Unsupported architecture %u", OptHeader->Architecture);
        return OsError;
    }

    Image = (PeExecutable_t*)dsalloc(sizeof(PeExecutable_t));
    if (!Image) {
        return OsOutOfMemory;
    }
    
    memset(Image, 0, sizeof(PeExecutable_t));
    Index                    = MStringFindReverse(FullPath, '/', 0);
    Image->Name              = MStringSubString(FullPath, Index + 1, -1);
    Image->Owner             = Owner;
    Image->FullPath          = FullPath;
    Image->Architecture      = OptHeader->Architecture;
    Image->VirtualAddress    = (Parent == NULL) ? GetBaseAddress() : Parent->NextLoadingAddress;
    Image->Libraries         = dsalloc(sizeof(list_t));
    Image->References        = 1;
    Image->OriginalImageBase = ImageBase;
    list_construct(Image->Libraries);
    dstrace("library (%s) => 0x%x", MStringRaw(Image->Name), Image->VirtualAddress);

    // Set the entry point if there is any
    if (OptHeader->EntryPoint != 0) {
        Image->EntryAddress = Image->VirtualAddress + OptHeader->EntryPoint;
    }

    if (Parent == NULL) {
        Status = CreateImageSpace(&Image->MemorySpace);
        if (Status != OsSuccess) {
            dserror("Failed to create pe's memory space");
            MStringDestroy(Image->Name);
            MStringDestroy(Image->FullPath);
            dsfree(Image->Libraries);
            dsfree(Image);
            return OsError;
        }
    }
    else {
        Image->MemorySpace = Parent->MemorySpace;
    }

    // Parse the headers, directories and handle them.
    Status = PeParseAndMapImage(Parent, Image, Buffer, SizeOfMetaData, SectionAddress, 
        (int)BaseHeader->NumSections, DirectoryPtr);
    UnloadFile(FullPath, (void*)Buffer);
    if (Status != OsSuccess) {
        PeUnloadLibrary(Parent, Image);
        return OsError;
    }
    *ImageOut = Image;
    return OsSuccess;
}

OsStatus_t
PeUnloadImage(
    _In_ PeExecutable_t* Image)
{
    element_t* Element;
    if (Image != NULL) {
        MStringDestroy(Image->Name);
        MStringDestroy(Image->FullPath);
        if (Image->ExportedFunctions != NULL) {
            dsfree(Image->ExportedFunctions);
        }
        if (Image->Libraries != NULL) {
            _foreach(Element, Image->Libraries) {
                PeUnloadImage(Element->value);
            }
        }
        dsfree(Image);
        return OsSuccess;
    }
    return OsError;
}

OsStatus_t
PeUnloadLibrary(
    _In_ PeExecutable_t* Parent, 
    _In_ PeExecutable_t* Library)
{
    Library->References--;

    // Sanitize the ref count
    // we might have to unload it if there are no more references
    if (Library->References <= 0)  {
        if (Parent != NULL) {
            foreach(i, Parent->Libraries) {
                PeExecutable_t* lLib = i->value;
                if (lLib == Library) {
                    list_remove(Parent->Libraries, i);
                    break;
                }
            }
        }
        return PeUnloadImage(Library);
    }
    return OsSuccess;
}
