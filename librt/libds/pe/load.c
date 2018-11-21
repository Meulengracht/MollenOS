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

#include <os/mollenos.h>
#include <ds/ds.h>
#include <assert.h>
#include "pe.h"

/* PeHandleSections
 * Relocates and initializes all sections in the pe image
 * It also returns the last memory address of the relocations */
OsStatus_t
PeHandleSections(
    _In_  PeExecutable_t* Image,
    _In_  uint8_t*        Data,
    _In_  uintptr_t       SectionAddress,
    _In_  int             SectionCount,
    _Out_ uintptr_t*      NextAvailableAddress)
{
    PeSectionHeader_t* Section        = (PeSectionHeader_t*)SectionAddress;
    uintptr_t          CurrentAddress = Image->VirtualAddress;
    OsStatus_t         Status;
    char               SectionName[PE_SECTION_NAME_LENGTH + 1];
    int                i;

    for (i = 0; i < SectionCount; i++)
    {
        // Calculate pointers, we need two of them, one that
        // points to data in file, and one that points to where
        // in memory we want to copy data to
        uintptr_t VirtualDestination = Image->VirtualAddress + Section->VirtualAddress;
        uint8_t*  FileBuffer         = (uint8_t*)(Data + Section->RawAddress);
        uint8_t*  Destination;
        Flags_t   PageFlags          = MEMORY_READ;
        size_t    SectionSize        = MAX(Section->RawSize, Section->VirtualSize);

        // Make a local copy of the name, just in case
        // we need to do some debug print
        memcpy(&SectionName[0], &Section->Name[0], 8);
        SectionName[8] = 0;

        // Handle page flags for this section
        if (Section->Flags & PE_SECTION_WRITE) {
            PageFlags |= MEMORY_WRITE;
        }
        if (Section->Flags & PE_SECTION_EXECUTE) {
            PageFlags |= MEMORY_EXECUTABLE;
        }

        // Iterate pages and map them in our memory space
        Status = CreateSystemMemorySpaceMapping(GetCurrentSystemMemorySpace(), NULL, 
            &VirtualDestination, SectionSize, PageFlags, __MASK);
        if (Status != OsSuccess) {
            ERROR("Failed to map in PE section at 0x%x", Destination);
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
            // is large, this means there needs to be zeroed space
            // afterwards
            if (Section->VirtualSize > Section->RawSize) {
                memset((Destination + Section->RawSize), 0,
                    (Section->VirtualSize - Section->RawSize));
            }
        }

        // Update address and seciton
        CurrentAddress = (Image->VirtualAddress + Section->VirtualAddress + SectionSize);
        Section++;
    }

    // Return a page-aligned address that points to the
    // next free relocation address
    if (CurrentAddress % GetSystemMemoryPageSize()) {
        CurrentAddress += (GetSystemMemoryPageSize() - (CurrentAddress % GetSystemMemoryPageSize()));
    }
    *NextAvailableAddress = CurrentAddress;
    return OsSuccess;
}

/* PeHandleRelocations
 * Initializes and handles the code relocations in the pe image */
OsStatus_t
PeHandleRelocations(
    PeExecutable_t*    Image,
    uintptr_t          ImageBase,
    PeDataDirectory_t* RelocDirectory)
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
            ERROR("Invalid relocation data: BlockSize > BytesLeft, bailing");
            break;
        }

        BytesLeft -= BlockSize;
        if (BlockSize == 0) {
            ERROR("Invalid relocation data: BlockSize == 0, bailing");
            break;
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
                ERROR("Implement support for reloc type: %u", Type);
                for (;;);
            }
            RelocationEntryPtr++;
        }

        AdvancePtr    = (uint8_t*)RelocationPtr;
        AdvancePtr    += (BlockSize - 8);
        RelocationPtr = (uint32_t*)AdvancePtr;
    }
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

    TRACE("PeHandleExports(%s, AddressRVA 0x%x, Size 0x%x)",
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
    TRACE("Number of functions to iterate: %u", ExportTable->NumberOfOrdinals);
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
            ERROR("(%s): Ordinal %i is forwarded as %s, this is not supported yet", 
                MStringRaw(Image->Name), ExFunc->Ordinal, ExFunc->ForwardName);
        }
    }
}

PeExportedFunction_t*
PeResolveImportDescriptor(
    _In_ PeExecutable_t*       Image,
    _In_ PeImportDescriptor_t* ImportDescriptor)
{
    // Calculate address to IAT
    // These entries are 64 bit in PE32+ and 32 bit in PE32 
    if (Image->Architecture == PE_ARCHITECTURE_32) {
        uint32_t *Iat = (uint32_t*)
            (Image->VirtualAddress + ImportDescriptor->ImportAddressTable);

        /* Iterate Import table for this module */
        while (*Iat) {
            PeExportedFunction_t *Function   = NULL;
            char *FunctionName                  = NULL;
            uint32_t Value                      = *Iat;

            /* Is it an ordinal or a function name? */
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
                /* Nah, pointer to function name, 
                    * where two first bytes are hint? */
                FunctionName = (char*)
                    (Image->VirtualAddress + (Value & PE_IMPORT_NAMEMASK) + 2);
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
                ERROR("Failed to locate function (%s)", Function->Name);
                return OsError;
            }

            // Update import address and go to next
            *Iat = Function->Address;
            Iat++;
        }
    }
    else {
        uint64_t *Iat = (uint64_t*)
            (Image->VirtualAddress + ImportDescriptor->ImportAddressTable);

        /* Iterate Import table for this module */
        while (*Iat) {
            PeExportedFunction_t *Function = NULL;
            uint64_t Value = *Iat;

            /* Is it an ordinal or a function name? */
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
                /* Nah, pointer to function name, 
                    * where two first bytes are hint? */
                char *FunctionName = (char*)
                    (Image->VirtualAddress + (uint32_t)(Value & PE_IMPORT_NAMEMASK) + 2);
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
                ERROR("Failed to locate function (%s)", Function->Name);
                return OsError;
            }

            // Update import address and go to next
            *Iat = (uint64_t)Function->Address;
            Iat++;
        }
    }
}

/* PeHandleImports
 * Parses and resolves all image imports by parsing the import address table */
OsStatus_t
PeHandleImports(
    _In_    PeExecutable_t*    Parent,
    _In_    PeExecutable_t*    Image, 
    _In_    PeDataDirectory_t* ImportDirectory,
    _InOut_ uintptr_t*         NextImageBase)
{
    PeImportDescriptor_t* ImportDescriptor;
    
    if (ImportDirectory->AddressRVA == 0 || ImportDirectory->Size == 0) {
        return OsSuccess;
    }

    ImportDescriptor = (PeImportDescriptor_t*)(Image->VirtualAddress + ImportDirectory->AddressRVA);
    while (ImportDescriptor->ImportAddressTable != 0) {
        PeExecutable_t*       ResolvedLibrary = NULL;
        PeExportedFunction_t* Exports         = NULL;
        int                   NumberOfExports = 0;
        MString_t*            Name;
        char*                 NamePtr;

        NamePtr = (char*)(Image->VirtualAddress + ImportDescriptor->ModuleName);
        Name    = MStringCreate(NamePtr, StrUTF8);

        // Resolve the library from the import chunk
        ResolvedLibrary = PeResolveLibrary(Parent, Image, Name, NextImageBase);
        if (ResolvedLibrary == NULL || ResolvedLibrary->ExportedFunctions == NULL) {
            ERROR("(%s): Failed to resolve library %s", 
                MStringRaw(Image->Name), MStringRaw(Name));
            return OsError;
        }
        else {
            TRACE("(%s): Library %s resolved, %i functions available", 
                MStringRaw(Image->Name), MStringRaw(Name), 
                ResolvedLibrary->NumberOfExportedFunctions);
            Exports = ResolvedLibrary->ExportedFunctions;
            NumberOfExports = ResolvedLibrary->NumberOfExportedFunctions;
        }

        
        ImportDescriptor++;
    }
    return OsSuccess;
}

/* PeResolveLibrary
 * Resolves a dependancy or a given module path, a load address must be provided
 * together with a pe-file header to fill out and the parent that wants to resolve
 * the library */
MCorePeFile_t*
PeResolveLibrary(
    _In_    MCorePeFile_t*  Parent,
    _In_    MCorePeFile_t*  PeFile,
    _In_    MString_t*      LibraryName,
    _InOut_ uintptr_t*      LoadAddress)
{
    // Variables
    MCorePeFile_t *ExportParent = Parent;
    MCorePeFile_t *Exports      = NULL;
    OsStatus_t Status;

    // Sanitize the parent, because the parent will
    // be null when it's the root module
    if (ExportParent == NULL) {
        ExportParent = PeFile;
    }

    // Trace
    TRACE("PeResolveLibrary(Name %s, Address 0x%x)", MStringRaw(LibraryName), *LoadAddress);

    // Before actually loading the file, we want to
    // try to locate the library in the parent first.
    foreach(lNode, ExportParent->LoadedLibraries) {
        MCorePeFile_t *Library = (MCorePeFile_t*)lNode->Data;

        // If we find it, then increase the ref count
        // and use its exports
        if (MStringCompare(Library->Name, LibraryName, 1) == MSTRING_FULL_MATCH) {
            TRACE("Library %s was already resolved, increasing ref count", MStringRaw(Library->Name));
            Library->References++;
            Exports = Library;
            break;
        }
    }

    // Sanitize the exports, if its null we have to resolve the library
    if (Exports == NULL) {
        MCorePeFile_t *Library;
        uint8_t *fBuffer;
        size_t fSize;

        // Open the file
        // We have a special case here that it might
        // be from the ramdisk we are loading
        if (ExportParent->UsingInitRD) {
            TRACE("Loading from ramdisk (%s)", MStringRaw(LibraryName));
            Status = ModulesQueryPath(LibraryName, (void**)&fBuffer, &fSize);
        }
        else {
            TRACE("Loading from filesystem (%s)", MStringRaw(LibraryName));
            Status = LoadFile(MStringRaw(LibraryName), NULL, (void**)&fBuffer, &fSize);
        }

        if (Status != OsSuccess && fBuffer != NULL && fSize != 0) {
            ERROR("Failed to load library %s", MStringRaw(LibraryName));
            for (;;);
        }

        // After retrieving the data we can now
        // load the actual image
        TRACE("Parsing pe-image");
        Library = PeLoadImage(ExportParent, LibraryName, fBuffer, fSize, LoadAddress, ExportParent->UsingInitRD);
        Exports = Library;

        // Cleanup buffer, we are done with it now
        if (!ExportParent->UsingInitRD) {
            kfree(fBuffer);
        }
    }

    // Sanitize exports again, it's only NULL
    // if all our attempts failed!
    if (Exports == NULL) {
        ERROR("Library %s was unable to be resolved", MStringRaw(LibraryName));
    }
    return Exports;
}

/* PeResolveFunction
 * Resolves a function by name in the given pe image, the return
 * value is the address of the function. 0 If not found */
uintptr_t
PeResolveFunction(
    _In_ MCorePeFile_t* Library, 
    _In_ const char*    Function)
{
    MCorePeExportFunction_t* Exports = Library->ExportedFunctions;
    if (Exports != NULL) {
        for (int i = 0; i < Library->NumberOfExportedFunctions; i++) {
            if (Exports[i].Name != NULL && !strcmp(Exports[i].Name, Function)) {
                return Exports[i].Address;
            }
        }
    }
    return 0;
}

/* PeLoadImage
 * Loads the given file-buffer as a pe image into the current address space 
 * at the given Base-Address, which is updated after load to reflect where
 * the next address is available for load */
MCorePeFile_t*
PeLoadImage(
    _In_    MCorePeFile_t*  Parent,
    _In_    MString_t*      Name,
    _In_    uint8_t*        Buffer,
    _In_    size_t          Length,
    _InOut_ uintptr_t*      BaseAddress,
    _In_    int             UsingInitRD)
{
    // Variables
    MzHeader_t *DosHeader               = NULL;
    PeHeader_t *BaseHeader              = NULL;
    PeOptionalHeader_t *OptHeader       = NULL;

    // Optional headers for both bit-widths
    PeOptionalHeader32_t *OptHeader32   = NULL;
    PeOptionalHeader64_t *OptHeader64   = NULL;

    // More variables
    uintptr_t SectionAddress            = 0;
    uintptr_t ImageBase                 = 0;
    size_t SizeOfMetaData               = 0;
    PeDataDirectory_t *DirectoryPtr     = NULL;
    MCorePeFile_t *PeInfo               = NULL;

#ifdef __OSCONFIG_PROCESS_SINGLELOAD
    CriticalSectionEnter(&LoaderLock);
#endif

    // Debug
    TRACE("PeLoadImage(Path %s, Parent %s, Address 0x%x)",
        MStringRaw(Name), (Parent == NULL) ? "None" : MStringRaw(Parent->Name), 
        *BaseAddress);

    // Start out by validating the file buffer
    // so we don't load any garbage
    if (!PeValidate(Buffer, Length)) {
        return NULL;
    }
    
    // Start out by initializing our header pointers
    DosHeader       = (MzHeader_t*)Buffer;
    BaseHeader      = (PeHeader_t*)(Buffer + DosHeader->PeHeaderAddress);
    OptHeader       = (PeOptionalHeader_t*)
        (Buffer + DosHeader->PeHeaderAddress + sizeof(PeHeader_t));

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
        // Cleanup, return null
        ERROR("Unsupported architecture %u", OptHeader->Architecture);
        return NULL;
    }

    // Allocate a new pe image file structure
    PeInfo = (MCorePeFile_t*)dsalloc(sizeof(MCorePeFile_t));
    memset(PeInfo, 0, sizeof(MCorePeFile_t));

    // Fill initial members
    PeInfo->Name            = Name;
    PeInfo->Architecture    = OptHeader->Architecture;
    PeInfo->VirtualAddress  = *BaseAddress;
    PeInfo->LoadedLibraries = CollectionCreate(KeyInteger);
    PeInfo->References      = 1;
    PeInfo->UsingInitRD     = UsingInitRD;

    // Set the entry point if there is any
    if (OptHeader->EntryPoint != 0) {
        PeInfo->EntryAddress = PeInfo->VirtualAddress + OptHeader->EntryPoint;
    }
    else {
        PeInfo->EntryAddress = 0;
    }

    // Copy sections to base address
    if (CreateSystemMemorySpaceMapping(GetCurrentSystemMemorySpace(), NULL, &PeInfo->VirtualAddress,
        SizeOfMetaData, MAPPING_USERSPACE | MAPPING_FIXED, __MASK) != OsSuccess) {
        // Whatthe
        FATAL(FATAL_SCOPE_KERNEL, "Failed to map pe's metadata, out of memory?");
    }
    memcpy((void*)PeInfo->VirtualAddress, Buffer, SizeOfMetaData);
    
    // Now we want to handle all the directories
    // and sections in the image, start out by handling
    // the sections, then parse all directories
    TRACE("Handling sections, relocations and exports");
    *BaseAddress = PeHandleSections(PeInfo, Buffer, SectionAddress, BaseHeader->NumSections, 1);
    PeHandleRelocations(PeInfo, ImageBase, &DirectoryPtr[PE_SECTION_BASE_RELOCATION]);
    PeHandleExports(PeInfo, &DirectoryPtr[PE_SECTION_EXPORT]);

    // Before loading imports, add us to parent list of libraries 
    // so we might be reused, instead of reloaded
    if (Parent != NULL) {
        DataKey_t Key = { 0 };
        CollectionAppend(Parent->LoadedLibraries, CollectionCreateNode(Key, PeInfo));
    }

    // Handle imports
    if (PeHandleImports(Parent, PeInfo, &DirectoryPtr[PE_SECTION_IMPORT], BaseAddress) != OsSuccess) {
        FATAL(FATAL_SCOPE_KERNEL, "Failed to load library");
    }
    TRACE("Library(%s) has been loaded", MStringRaw(Name));

#ifdef __OSCONFIG_PROCESS_SINGLELOAD
    CriticalSectionLeave(&LoaderLock);
#endif
    return PeInfo;
}

/* PeUnloadLibrary
 * Unload dynamically loaded library 
 * This only cleans up in the case there are no more references */
OsStatus_t
PeUnloadLibrary(
    _In_ MCorePeFile_t *Parent, 
    _In_ MCorePeFile_t *Library)
{
    // Decrease the ref count
    Library->References--;

    // Sanitize the ref count
    // we might have to unload it if there are
    // no more references
    if (Library->References <= 0)  {
        foreach(lNode, Parent->LoadedLibraries) {
            MCorePeFile_t *lLib = (MCorePeFile_t*)lNode->Data;
            if (lLib == Library) {
                CollectionRemoveByNode(Parent->LoadedLibraries, lNode);
                kfree(lNode);
                break;
            }
        }

        // Actually unload image
        return PeUnloadImage(Library);
    }
    return OsSuccess;
}

/* PeUnloadImage
 * Unload executables, all it's dependancies and free it's resources */
OsStatus_t
PeUnloadImage(
    _In_ MCorePeFile_t *Executable)
{
    // Variables
    CollectionItem_t *Node = NULL;

    // Sanitize parameter
    if (Executable == NULL) {
        return OsError;
    }

    // Cleanup resources
    MStringDestroy(Executable->Name);

    // Cleanup exports
    if (Executable->ExportedFunctions != NULL) {
        kfree(Executable->ExportedFunctions);
    }

    // Unload libraries
    if (Executable->LoadedLibraries != NULL) {
        _foreach(Node, Executable->LoadedLibraries) {
            MCorePeFile_t *Library = (MCorePeFile_t*)Node->Data;
            PeUnloadImage(Library);
        }
    }

    // Last step, free base
    kfree(Executable);
    return OsSuccess;
}

/* PeGetModuleHandles
 * Retrieves a list of loaded module handles currently loaded for the process. */
OsStatus_t
PeGetModuleHandles(
    _In_ MCorePeFile_t *Executable,
    _Out_ Handle_t ModuleList[PROCESS_MAXMODULES])
{
    // Variables
    int Index = 0;

    // Sanitize input
    if (Executable == NULL || ModuleList == NULL) {
        return OsError;
    }

    // Reset data
    memset(&ModuleList[0], 0, sizeof(Handle_t) * PROCESS_MAXMODULES);

    // Copy base over
    ModuleList[Index++] = (Handle_t)Executable->VirtualAddress;
    if (Executable->LoadedLibraries != NULL) {
        foreach(Node, Executable->LoadedLibraries) {
            MCorePeFile_t *Library = (MCorePeFile_t*)Node->Data;
            ModuleList[Index++] = (Handle_t)Library->VirtualAddress;
        }
    }
    return OsSuccess;
}

/* PeGetModuleEntryPoints
 * Retrieves a list of loaded module entry points currently loaded for the process. */
OsStatus_t
PeGetModuleEntryPoints(
    _In_  MCorePeFile_t*    Executable,
    _Out_ Handle_t          ModuleList[PROCESS_MAXMODULES])
{
    // Variables
    int Index = 0;

    // Sanitize input
    if (Executable == NULL || ModuleList == NULL) {
        return OsError;
    }

    // Reset data
    memset(&ModuleList[0], 0, sizeof(Handle_t) * PROCESS_MAXMODULES);

    // Copy base over
    if (Executable->LoadedLibraries != NULL) {
        foreach(Node, Executable->LoadedLibraries) {
            MCorePeFile_t *Library = (MCorePeFile_t*)Node->Data;
            if (Library->EntryAddress != 0) {
                ModuleList[Index++] = (Handle_t)Library->EntryAddress;
            }
        }
    }
    return OsSuccess;
}
