/* MollenOS
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

#include <ds/collection.h>
#include <os/mollenos.h>
#include <ds/mstring.h>
#include <string.h>
#include <assert.h>
#include "pe.h"

/* PeHandleSections
 * Relocates and initializes all sections in the pe image
 * It also returns the last memory address of the relocations */
OsStatus_t
PeHandleSections(
    _In_  PeExecutable_t* Parent,
    _In_  PeExecutable_t* Image,
    _In_  uint8_t*        Data,
    _In_  uintptr_t       SectionAddress,
    _In_  int             SectionCount)
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
        uintptr_t VirtualDestination = Image->VirtualAddress + Section->VirtualAddress;
        uint8_t*  FileBuffer         = (uint8_t*)(Data + Section->RawAddress);
        Flags_t   PageFlags          = MEMORY_READ;
        size_t    SectionSize        = MAX(Section->RawSize, Section->VirtualSize);
        uint8_t*  Destination;

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
            dserror("Failed to map in PE section at 0x%x", VirtualDestination);
            return Status;
        }
        Destination = (uint8_t*)VirtualDestination;

        // Store first code segment we encounter
        if (Section->Flags & PE_SECTION_CODE) {
            if (Image->CodeBase == 0) {
                Image->CodeBase = (uintptr_t)Destination;
                Image->CodeSize = Section->VirtualSize;
            }
        }

        // Handle sections specifics, we want to:
        // BSS: Zero out the memory 
        // Code: Copy memory 
        // Data: Copy memory
        if (Section->RawSize == 0 || (Section->Flags & PE_SECTION_BSS)) {
            memset(Destination, 0, Section->VirtualSize);
        }
        else if ((Section->Flags & PE_SECTION_CODE) || (Section->Flags & PE_SECTION_DATA)) {
            memcpy(Destination, FileBuffer, Section->RawSize);

            // Sanitize this special case, if the virtual size
            // is large, this means there needs to be zeroed space afterwards
            if (Section->VirtualSize > Section->RawSize) {
                memset((Destination + Section->RawSize), 0, (Section->VirtualSize - Section->RawSize));
            }
        }

        ReleaseImageMapping(MapHandle);
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

/* PeHandleRelocations
 * Initializes and handles the code relocations in the pe image */
OsStatus_t
PeHandleRelocations(
    _In_ PeExecutable_t*    Image,
    _In_ uintptr_t          ImageBase,
    _In_ PeDataDirectory_t* RelocDirectory)
{
    uint32_t  BytesLeft = RelocDirectory->Size;
    uint32_t* RelocationPtr;
    uint16_t* RelocationEntryPtr;
    uint8_t*  AdvancePtr;
    uint32_t  i;

    if (RelocDirectory->AddressRVA == 0 || BytesLeft == 0) {
        return OsDoesNotExist;
    }
    RelocationPtr = (uint32_t*)(Image->VirtualAddress + RelocDirectory->AddressRVA);

    while (BytesLeft > 0) {
        uint32_t PageRVA   = *(RelocationPtr++);
        uint32_t BlockSize = *(RelocationPtr++);
        uint32_t NumRelocs;

        if (BlockSize > BytesLeft) {
            dserror("Invalid relocation data: BlockSize > BytesLeft, bailing");
            return OsError;
        }

        BytesLeft -= BlockSize;
        if (BlockSize == 0) {
            dserror("Invalid relocation data: BlockSize == 0, bailing");
            return OsError;
        }
        NumRelocs          = (BlockSize - 8) / sizeof(uint16_t);
        RelocationEntryPtr = (uint16_t*)RelocationPtr;

        for (i = 0; i < NumRelocs; i++) {
            uint16_t RelocationEntry = *RelocationEntryPtr;
            uint16_t Type            = (RelocationEntry >> 12);
            uint16_t Value           = RelocationEntry & 0x0FFF;
            
            // 32/64 Bit Relative
            if (Type == PE_RELOCATION_HIGHLOW || Type == PE_RELOCATION_RELATIVE64) { 
                // Create a pointer, the low 12 bits have an offset into the PageRVA
                uintptr_t Offset     = (Image->VirtualAddress + PageRVA + Value);
                uintptr_t Translated = Image->VirtualAddress;

                // Handle relocation
                if (Translated >= ImageBase) {
                    uintptr_t Delta       = (uintptr_t)(Translated - ImageBase);
                    *((uintptr_t*)Offset) += Delta;
                }
                else {
                    uintptr_t Delta       = (uintptr_t)(ImageBase - Translated);
                    *((uintptr_t*)Offset) -= Delta;
                }
            }
            else if (Type == PE_RELOCATION_ALIGN) {
                // End of alignment
            }
            else {
                dserror("Implement support for reloc type: %u", Type);
                for (;;);
            }
            RelocationEntryPtr++;
        }

        AdvancePtr    = (uint8_t*)RelocationPtr;
        AdvancePtr    += (BlockSize - 8);
        RelocationPtr = (uint32_t*)AdvancePtr;
    }
    return OsSuccess;
}

/* PeHandleExports
 * Parses the exporst that the pe image provides and caches the list */
void
PeHandleExports(
    _In_ PeExecutable_t*    Image, 
    _In_ PeDataDirectory_t* ExportDirectory)
{
    PeExportDirectory_t* ExportTable;
    uint32_t*            FunctionNamesTable;
    uint16_t*            FunctionOrdinalsTable;
    uint32_t*            FunctionAddressTable;
    int                  OrdinalBase;
    int                  i;

    dstrace("PeHandleExports(%s, AddressRVA 0x%x, Size 0x%x)",
        MStringRaw(Image->Name), ExportDirectory->AddressRVA, ExportDirectory->Size);
    if (ExportDirectory->AddressRVA == 0 || ExportDirectory->Size == 0) {
        return;
    }

    ExportTable             = (PeExportDirectory_t*)(Image->VirtualAddress + ExportDirectory->AddressRVA);
    FunctionNamesTable      = (uint32_t*)(Image->VirtualAddress + ExportTable->AddressOfNames);
    FunctionOrdinalsTable   = (uint16_t*)(Image->VirtualAddress + ExportTable->AddressOfOrdinals);
    FunctionAddressTable    = (uint32_t*)(Image->VirtualAddress + ExportTable->AddressOfFunctions);

    // Allocate statis array for exports
    Image->ExportedFunctions         = (PeExportedFunction_t*)dsalloc(sizeof(PeExportedFunction_t) * ExportTable->NumberOfOrdinals);
    Image->NumberOfExportedFunctions = (int)ExportTable->NumberOfOrdinals;
    OrdinalBase                      = ExportTable->OrdinalBase;

    // Instantiate the list for exported functions
    dstrace("Number of functions to iterate: %u", ExportTable->NumberOfOrdinals);
    for (i = 0; i < Image->NumberOfExportedFunctions; i++) {
        PeExportedFunction_t* ExFunc = &Image->ExportedFunctions[i];

        // Extract the function information
        ExFunc->Name        = NULL;
        ExFunc->ForwardName = NULL;
        ExFunc->Ordinal     = (int)FunctionOrdinalsTable[i];
        ExFunc->Address     = (uintptr_t)(Image->VirtualAddress + FunctionAddressTable[ExFunc->Ordinal - OrdinalBase]);
        if ((ExFunc->Ordinal - OrdinalBase) <= ExportTable->NumberOfOrdinals) {
            ExFunc->Name    = (char*)(Image->VirtualAddress + FunctionNamesTable[i]);
        }
        if (FunctionAddressTable[ExFunc->Ordinal - OrdinalBase] >= ExportDirectory->AddressRVA &&
            FunctionAddressTable[ExFunc->Ordinal - OrdinalBase] < (ExportDirectory->AddressRVA + ExportDirectory->Size)) {
            ExFunc->ForwardName = (char*)(Image->VirtualAddress + FunctionAddressTable[ExFunc->Ordinal - OrdinalBase]);
            dserror("(%s): Ordinal %i is forwarded as %s, this is not supported yet", 
                MStringRaw(Image->Name), ExFunc->Ordinal, ExFunc->ForwardName);
        }
    }
}

OsStatus_t
PeResolveImportDescriptor(
    _In_    PeExecutable_t*       Parent,
    _In_    PeExecutable_t*       Image,
    _In_    PeImportDescriptor_t* ImportDescriptor,
    _In_    MString_t*            ImportDescriptorName)
{
    PeExecutable_t*       ResolvedLibrary;
    PeExportedFunction_t* Exports;
    int                   NumberOfExports;

    // Resolve the library from the import chunk
    ResolvedLibrary = PeResolveLibrary(Parent, Image, ImportDescriptorName);
    if (ResolvedLibrary == NULL || ResolvedLibrary->ExportedFunctions == NULL) {
        dserror("(%s): Failed to resolve library %s", MStringRaw(Image->Name), MStringRaw(ImportDescriptorName));
        return OsError;
    }
    Exports         = ResolvedLibrary->ExportedFunctions;
    NumberOfExports = ResolvedLibrary->NumberOfExportedFunctions;

    // Calculate address to IAT
    // These entries are 64 bit in PE32+ and 32 bit in PE32 
    if (Image->Architecture == PE_ARCHITECTURE_32) {
        uint32_t* Iat = (uint32_t*)(Image->VirtualAddress + ImportDescriptor->ImportAddressTable);
        while (*Iat) {
            uint32_t              Value        = *Iat;
            PeExportedFunction_t* Function     = NULL;
            char*                 FunctionName;

            if (Value & PE_IMPORT_ORDINAL_32) {
                int Ordinal = (int)(Value & 0xFFFF);
                for (int i = 0; i < NumberOfExports; i++) {
                    if (Exports[i].Ordinal == Ordinal) {
                        Function = &Exports[i];
                        break;
                    }
                }
            }
            else {
                // Nah, pointer to function name, where two first bytes are hint?
                FunctionName = (char*)(Image->VirtualAddress + (Value & PE_IMPORT_NAMEMASK) + 2);
                for (int i = 0; i < NumberOfExports; i++) {
                    if (Exports[i].Name != NULL) {
                        if (!strcmp(Exports[i].Name, FunctionName)) {
                            Function = &Exports[i];
                            break;
                        }
                    }
                }
            }

            if (Function == NULL) {
                dserror("Failed to locate function (%s)", Function->Name);
                return OsError;
            }
            *Iat = Function->Address;
            Iat++;
        }
    }
    else {
        uint64_t* Iat = (uint64_t*)(Image->VirtualAddress + ImportDescriptor->ImportAddressTable);
        while (*Iat) {
            uint64_t              Value        = *Iat;
            PeExportedFunction_t* Function     = NULL;
            char*                 FunctionName;

            if (Value & PE_IMPORT_ORDINAL_64) {
                int Ordinal = (int)(Value & 0xFFFF);
                for (int i = 0; i < NumberOfExports; i++) {
                    if (Exports[i].Ordinal == Ordinal) {
                        Function = &Exports[i];
                        break;
                    }
                }
            }
            else {
                // Nah, pointer to function name, where two first bytes are hint?
                FunctionName = (char*)(Image->VirtualAddress + (uint32_t)(Value & PE_IMPORT_NAMEMASK) + 2);
                for (int i = 0; i < NumberOfExports; i++) {
                    if (Exports[i].Name != NULL) {
                        if (!strcmp(Exports[i].Name, FunctionName)) {
                            Function = &Exports[i];
                            break;
                        }
                    }
                }
            }

            if (Function == NULL) {
                dserror("Failed to locate function (%s)", Function->Name);
                return OsError;
            }
            *Iat = (uint64_t)Function->Address;
            Iat++;
        }
    }
    return OsSuccess;
}

/* PeHandleImports
 * Parses and resolves all image imports by parsing the import address table */
OsStatus_t
PeHandleImports(
    _In_    PeExecutable_t*    Parent,
    _In_    PeExecutable_t*    Image, 
    _In_    PeDataDirectory_t* ImportDirectory)
{
    PeImportDescriptor_t* ImportDescriptor;
    
    if (ImportDirectory->AddressRVA == 0 || ImportDirectory->Size == 0) {
        return OsSuccess;
    }

    ImportDescriptor = (PeImportDescriptor_t*)(Image->VirtualAddress + ImportDirectory->AddressRVA);
    while (ImportDescriptor->ImportAddressTable != 0) {
        char*      NamePtr = (char*)(Image->VirtualAddress + ImportDescriptor->ModuleName);
        MString_t* Name    = MStringCreate(NamePtr, StrUTF8);
        OsStatus_t Status  = PeResolveImportDescriptor(Parent, Image, ImportDescriptor, Name);
        MStringDestroy(Name);

        if (Status != OsSuccess) {
            return OsError;
        }
        ImportDescriptor++;
    }
    return OsSuccess;
}

/* PeParseAndMapImage
 * Parses sections, data directories and performs neccessary translations and mappings. */
OsStatus_t
PeParseAndMapImage(
    _In_ PeExecutable_t*    Parent,
    _In_ PeExecutable_t*    Image,
    _In_ uint8_t*           ImageBuffer,
    _In_ uintptr_t          ImageBase,
    _In_ size_t             SizeOfMetaData,
    _In_ uintptr_t          SectionBase,
    _In_ size_t             SectionCount,
    _In_ PeDataDirectory_t* DirectoryPointer)
{
    uintptr_t         VirtualAddress = Image->VirtualAddress;
    MemoryMapHandle_t MapHandle;
    OsStatus_t        Status;

    // Copy metadata of image to base address
    Status = AcquireImageMapping(Image->MemorySpace, &VirtualAddress, SizeOfMetaData,
        MEMORY_READ | MEMORY_WRITE, &MapHandle);
    if (Status != OsSuccess) {
        dserror("Failed to map pe's metadata, out of memory?");
        return OsError;
    }
    memcpy((void*)VirtualAddress, ImageBuffer, SizeOfMetaData);
    ReleaseImageMapping(MapHandle);

    // Now we want to handle all the directories
    // and sections in the image, start out by handling
    // the sections, then parse all directories
    dstrace("Handling sections, relocations and exports");
    Status = PeHandleSections(Parent, Image, ImageBuffer, SectionBase, SectionCount);
    if (Status != OsSuccess) {
        return OsError;
    } 
    PeHandleRelocations(Image, ImageBase, &DirectoryPointer[PE_SECTION_BASE_RELOCATION]);
    PeHandleExports(Image, &DirectoryPointer[PE_SECTION_EXPORT]);

    // Before loading imports, add us to parent list of libraries 
    // so we might be reused, instead of reloaded
    if (Parent != NULL) {
        DataKey_t Key = { 0 };
        CollectionAppend(Parent->Libraries, CollectionCreateNode(Key, Image));
    }
    Status = PeHandleImports(Parent, Image, &DirectoryPointer[PE_SECTION_IMPORT]);
    return Status;
}

/* PeLoadImage
 * Loads the given file-buffer as a pe image into the current address space 
 * at the given Base-Address, which is updated after load to reflect where
 * the next address is available for load */
OsStatus_t
PeLoadImage(
    _In_    PeExecutable_t*  Parent,
    _In_    MString_t*       Name,
    _In_    MString_t*       FullPath,
    _In_    uint8_t*         Buffer,
    _In_    size_t           Length,
    _Out_   PeExecutable_t** ImageOut)
{
    MzHeader_t*           DosHeader;
    PeHeader_t*           BaseHeader;
    PeOptionalHeader_t*   OptHeader;
    PeOptionalHeader32_t* OptHeader32;
    PeOptionalHeader64_t* OptHeader64;

    uintptr_t          SectionAddress;
    uintptr_t          ImageBase;
    size_t             SizeOfMetaData;
    PeDataDirectory_t* DirectoryPtr;
    PeExecutable_t*    Image;
    OsStatus_t         Status;
    
    dstrace("PeLoadImage(Path %s, Parent %s, Address 0x%x)",
        MStringRaw(Name), (Parent == NULL) ? "None" : MStringRaw(Parent->Name), 
        GetBaseAddress());
    
    if (PeValidateImageBuffer(Buffer, Length) != OsSuccess) {
        return OsError;
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
    memset(Image, 0, sizeof(PeExecutable_t));

    Image->Name            = MStringCreate((void*)MStringRaw(Name), StrUTF8);
    Image->FullPath        = MStringCreate((void*)MStringRaw(FullPath), StrUTF8);
    Image->Architecture    = OptHeader->Architecture;
    Image->VirtualAddress  = (Parent == NULL) ? GetBaseAddress() : Parent->NextLoadingAddress;
    Image->Libraries       = CollectionCreate(KeyInteger);
    Image->References      = 1;

    // Set the entry point if there is any
    if (OptHeader->EntryPoint != 0) {
        Image->EntryAddress = Image->VirtualAddress + OptHeader->EntryPoint;
    }

    Status = CreateImageSpace(&Image->MemorySpace);
    if (Status != OsSuccess) {
        dserror("Failed to create pe's memory space");
        CollectionDestroy(Image->Libraries);
        MStringDestroy(Image->Name);
        MStringDestroy(Image->FullPath);
        dsfree(Image);
        return OsError;
    }

    Status = PeParseAndMapImage(Parent, Image, Buffer, ImageBase, SizeOfMetaData, SectionAddress, 
        BaseHeader->NumSections, DirectoryPtr);
    if (Status != OsSuccess) {
        PeUnloadLibrary(Parent, Image);
        return OsError;
    }
    *ImageOut = Image;
    return OsSuccess;
}

/* PeUnloadImage
 * Unload executables, all it's dependancies and free it's resources */
OsStatus_t
PeUnloadImage(
    _In_ PeExecutable_t* Image)
{
    CollectionItem_t* Node;
    if (Image != NULL) {
        MStringDestroy(Image->Name);
        MStringDestroy(Image->FullPath);
        if (Image->ExportedFunctions != NULL) {
            dsfree(Image->ExportedFunctions);
        }
        if (Image->Libraries != NULL) {
            _foreach(Node, Image->Libraries) {
                PeUnloadImage((PeExecutable_t*)Node->Data);
            }
        }
        dsfree(Image);
        return OsSuccess;
    }
    return OsError;
}

/* PeUnloadLibrary
 * Unload dynamically loaded library 
 * This only cleans up in the case there are no more references */
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
            foreach(lNode, Parent->Libraries) {
                PeExecutable_t* lLib = (PeExecutable_t*)lNode->Data;
                if (lLib == Library) {
                    CollectionRemoveByNode(Parent->Libraries, lNode);
                    dsfree(lNode);
                    break;
                }
            }
        }
        return PeUnloadImage(Library);
    }
    return OsSuccess;
}
