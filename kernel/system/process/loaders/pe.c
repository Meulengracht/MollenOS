/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS MCore - PE Format Loader
 */
#define __MODULE        "PELD"
//#define __TRACE

/* Includes 
 * - System */
#include <system/addressspace.h>
#include <modules/modules.h>
#include <os/driver/file.h>
#include <process/ash.h>
#include <process/pe.h>
#include <debug.h>
#include <heap.h>
#include <log.h>

/* Includes
 * - Library */
#include <assert.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>

//#define __OSCONFIG_PROCESS_SINGLELOAD
#ifdef __OSCONFIG_PROCESS_SINGLELOAD
__EXTERN CriticalSection_t LoaderLock;
#endif

/* PeCalculateChecksum
 * Perform a checksum calculation of the 
 * given PE file. Use this to validate contents
 * of a PE executable */
uint32_t
PeCalculateChecksum(
    _In_ uint8_t *Data, 
    _In_ size_t DataLength, 
    _In_ size_t PeChkSumOffset)
{
    // Variables
    uint32_t *DataPtr = (uint32_t*)Data;
    uint64_t Limit = 4294967296;
    uint64_t CheckSum = 0;

    for (size_t i = 0; i < (DataLength / 4); i++, DataPtr++) {
        uint32_t Val = *DataPtr;

        // Skip the checksum index
        if (i == (PeChkSumOffset / 4)) {
            continue;
        }
        CheckSum = (CheckSum & UINT32_MAX) + Val + (CheckSum >> 32);
        if (CheckSum > Limit) {
            CheckSum = (CheckSum & UINT32_MAX) + (CheckSum >> 32);
        }
    }

    CheckSum = (CheckSum & UINT16_MAX) + (CheckSum >> 16);
    CheckSum = (CheckSum) + (CheckSum >> 16);
    CheckSum = CheckSum & UINT16_MAX;
    CheckSum += (uint32_t)DataLength;
    return (uint32_t)(CheckSum & UINT32_MAX);
}

/* PeValidate
 * Validates a file-buffer of the given length,
 * does initial header checks and performs a checksum
 * validation. Returns either PE_INVALID or PE_VALID */
int
PeValidate(
    _In_ uint8_t *Buffer, 
    _In_ size_t Length)
{
    // Variables
    MzHeader_t *DosHeader = NULL;
    PeHeader_t *BaseHeader = NULL;
    PeOptionalHeader_t *OptHeader = NULL;
    size_t HeaderCheckSum = 0, CalculatedCheckSum = 0;
    size_t CheckSumAddress = 0;

    // Get pointer to DOS
    DosHeader = (MzHeader_t*)Buffer;

    // Check magic for DOS
    if (DosHeader->Signature != MZ_MAGIC) {
        LogFatal("PELD", "Invalid MZ Signature 0x%x", DosHeader->Signature);
        return PE_INVALID;
    }

    // Get pointer to PE header
    BaseHeader = (PeHeader_t*)(Buffer + DosHeader->PeHeaderAddress);

    // Check magic for PE
    if (BaseHeader->Magic != PE_MAGIC) {
        ERROR("Invalid PE File Magic 0x%x", BaseHeader->Magic);
        return PE_INVALID;
    }

    // Validate the current build-target
    // we don't load arm modules for a x86
    if (BaseHeader->Machine != PE_CURRENT_MACHINE) {
        ERROR("PELD", "The image as built for machine type 0x%x, "
             "which is not the current machine type.", BaseHeader->Machine);
        return PE_INVALID;
    }

    // Initiate pointer to optional header
    OptHeader = (PeOptionalHeader_t*)(Buffer + DosHeader->PeHeaderAddress + sizeof(PeHeader_t));

    // Validate the current architecture,
    // again we don't load 32 bit modules for 64 bit
    if (OptHeader->Architecture != PE_CURRENT_ARCH) {
        ERROR("The image was built for architecture 0x%x, "
              "and was not supported by the current architecture.", 
              OptHeader->Architecture);
        return PE_INVALID;
    }

    // Ok, time to validate the contents of the file
    // by performing a checksum of the PE file
    // We need to re-cast based on architecture
    if (OptHeader->Architecture == PE_ARCHITECTURE_32) {
        PeOptionalHeader32_t *OptHeader32 = 
            (PeOptionalHeader32_t*)(Buffer + DosHeader->PeHeaderAddress + sizeof(PeHeader_t));
        CheckSumAddress = (size_t)&(OptHeader32->ImageChecksum);
        HeaderCheckSum = OptHeader32->ImageChecksum;
    }
    else if (OptHeader->Architecture == PE_ARCHITECTURE_64) {
        PeOptionalHeader64_t *OptHeader64 = 
            (PeOptionalHeader64_t*)(Buffer + DosHeader->PeHeaderAddress + sizeof(PeHeader_t));
        CheckSumAddress = (size_t)&(OptHeader64->ImageChecksum);
        HeaderCheckSum = OptHeader64->ImageChecksum;
    }

    // Now do the actual checksum calc if the checksum
    // of the PE header is not 0
    if (HeaderCheckSum != 0) {
        TRACE("Checksum validation phase");
        CalculatedCheckSum = PeCalculateChecksum(
            Buffer, Length, CheckSumAddress - ((size_t)Buffer));
        if (CalculatedCheckSum != HeaderCheckSum) {
            ERROR("Invalid checksum of file (Header 0x%x, Calculated 0x%x)", 
                HeaderCheckSum, CalculatedCheckSum);
            return PE_INVALID;
        }
    }
    return PE_VALID;
}

/* PeHandleSections
 * Relocates and initializes all sections in the pe image
 * It also returns the last memory address of the relocations */
uintptr_t
PeHandleSections(
    _In_ MCorePeFile_t *PeFile,
    _In_ uint8_t *Data,
    _In_ uintptr_t SectionAddress,
    _In_ int SectionCount,
    _In_ int UserSpace)
{
    // Variables
    PeSectionHeader_t *Section = (PeSectionHeader_t*)SectionAddress;
    uintptr_t CurrentAddress = PeFile->VirtualAddress;
    char SectionName[PE_SECTION_NAME_LENGTH + 1];
    int i, j;

    // Debug
    TRACE("PeHandleSections(Library %s, Address 0x%x, AddressOfSections 0x%x, SectionCount %i)",
        MStringRaw(PeFile->Name), PeFile->VirtualAddress, SectionAddress, SectionCount);

    // Iterate all sections
    for (i = 0; i < SectionCount; i++)
    {
        // Calculate pointers, we need two of them, one that
        // points to data in file, and one that points to where
        // in memory we want to copy data to
        uint8_t *FileBuffer = (uint8_t*)(Data + Section->RawAddress);
        uint8_t *Destination = (uint8_t*)(PeFile->VirtualAddress + Section->VirtualAddress);
        int PageCount = DIVUP(MAX(Section->RawSize, Section->VirtualSize), AddressSpaceGetPageSize());

        // Make a local copy of the name, just in case
        // we need to do some debug print
        memcpy(&SectionName[0], &Section->Name[0], 8);
        SectionName[8] = 0;

        // Iterate pages and map them in our memory space
        Flags_t PageFlags = (UserSpace == 1) ? ASPACE_FLAG_APPLICATION : 0;
        PageFlags |= ASPACE_FLAG_SUPPLIEDVIRTUAL;
        for (j = 0; j < PageCount; j++) {
            uintptr_t Calculated = (uintptr_t)Destination + (j * AddressSpaceGetPageSize());
            if (!AddressSpaceGetMapping(AddressSpaceGetCurrent(), Calculated)) {
                AddressSpaceMap(AddressSpaceGetCurrent(), NULL, &Calculated, AddressSpaceGetPageSize(), PageFlags, __MASK);
            }
        }

        // Handle sections specifics, we want to:
        // BSS: Zero out the memory 
        // Code: Copy memory 
        // Data: Copy memory
        if (Section->RawSize == 0
            || (Section->Flags & PE_SECTION_BSS)) {
            memset(Destination, 0, Section->VirtualSize);
        }
        else if ((Section->Flags & PE_SECTION_CODE)
                || (Section->Flags & PE_SECTION_DATA)) {
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
        CurrentAddress = (PeFile->VirtualAddress + Section->VirtualAddress
            + MAX(Section->RawSize, Section->VirtualSize));
        Section++;
    }

    // Return a page-aligned address that points to the
    // next free relocation address
    if (CurrentAddress % AddressSpaceGetPageSize()) {
        CurrentAddress += (AddressSpaceGetPageSize() - (CurrentAddress % AddressSpaceGetPageSize()));
    }
    return CurrentAddress;
}

/* PeHandleRelocations
 * Initializes and handles the code relocations in the pe image */
void PeHandleRelocations(MCorePeFile_t *PeFile, 
    PeDataDirectory_t *RelocDirectory, uintptr_t ImageBase)
{
    /* Get a pointer to the table */
    uint32_t BytesLeft = RelocDirectory->Size;
    uint32_t *RelocationPtr = NULL;
    uint16_t *RelocationEntryPtr = NULL;
    uint8_t *AdvancePtr = NULL;
    uint32_t Itr = 0;

    /* Sanitize the directory */
    if (RelocDirectory->AddressRVA == 0
        || RelocDirectory->Size == 0) {
        return;
    }
    
    /* Initialize the relocation pointer */
    RelocationPtr = (uint32_t*)(PeFile->VirtualAddress + RelocDirectory->AddressRVA);

    /* Iterate as long as we have bytes to parse */
    while (BytesLeft > 0)
    {
        /* Initialize some local variables */
        uint32_t PageRVA = *RelocationPtr++;
        uint32_t BlockSize = 0;
        uint32_t NumRelocs = 0;

        /* Get block size */
        BlockSize = *RelocationPtr++;

        /* Sanitize the block size */
        if (BlockSize > BytesLeft) {
            LogFatal("PELD", "Invalid relocation data: BlockSize > BytesLeft, bailing");
            break;
        }

        /* Decrease the bytes left */
        BytesLeft -= BlockSize;

        /* Now, entries come */
        if (BlockSize != 0) {
            NumRelocs = (BlockSize - 8) / sizeof(uint16_t);
        }
        else {
            LogFatal("PELD", "Invalid relocation data: BlockSize == 0, bailing");
            break;
        }

        /* Initialize the relocation pointer */
        RelocationEntryPtr = (uint16_t*)RelocationPtr;

        /* Iterate relocation entries for this block */
        for (Itr = 0; Itr < NumRelocs; Itr++) {
            uint16_t RelocationEntry = *RelocationEntryPtr;
            uint16_t Type = (RelocationEntry >> 12);
            uint16_t Value = RelocationEntry & 0x0FFF;

            /* 32 Bit Difference */
            if (Type == PE_RELOCATION_HIGHLOW)
            {
                /* Create a pointer, the low 12 bits have 
                 * an offset into the PageRVA */
                uintptr_t Offset = (PeFile->VirtualAddress + PageRVA + Value);
                uintptr_t Translated = PeFile->VirtualAddress;

                /* Should we add or subtract? */
                if (Translated >= ImageBase) {
                    uintptr_t Delta = (uintptr_t)(Translated - ImageBase);
                    *((uintptr_t*)Offset) += Delta;
                }
                else {
                    uintptr_t Delta = (uintptr_t)(ImageBase - Translated);
                    *((uintptr_t*)Offset) -= Delta;
                }
            }
            else if (Type == PE_RELOCATION_ALIGN) {
                /* End of alignment */
            }
            else {
                LogFatal("PEXE", "Implement support for reloc type: %u", Type);
                for (;;);
            }

            /* Next */
            RelocationEntryPtr++;
        }

        /* Adjust the relocation pointer */
        AdvancePtr = (uint8_t*)RelocationPtr;
        AdvancePtr += (BlockSize - 8);
        RelocationPtr = (uint32_t*)AdvancePtr;
    }
}

/* PeHandleExports
 * Parses the exporst that the pe image provides and caches the list */
void
PeHandleExports(
    _In_ MCorePeFile_t *PeFile, 
    _In_ PeDataDirectory_t *ExportDirectory)
{
    // Variables
    PeExportDirectory_t *ExportTable = NULL;
    uint32_t *FunctionNamesTable = NULL;
    uint16_t *FunctionOrdinalsTable = NULL;
    uint32_t *FunctionAddressTable = NULL;
    int i, OrdinalBase;

    // Debug
    TRACE("PeHandleExports(%s, AddressRVA 0x%x, Size 0x%x)",
        MStringRaw(PhoenixGetCurrentAsh()->Name), 
        ExportDirectory->AddressRVA, ExportDirectory->Size);

    // Sanitize the directory first
    if (ExportDirectory->AddressRVA == 0
        || ExportDirectory->Size == 0) {
        return;
    }

    // Initiate pointer to export table
    ExportTable = (PeExportDirectory_t*)(PeFile->VirtualAddress + ExportDirectory->AddressRVA);

    // Calculate the names
    FunctionNamesTable = (uint32_t*)(PeFile->VirtualAddress + ExportTable->AddressOfNames);
    FunctionOrdinalsTable = (uint16_t*)(PeFile->VirtualAddress + ExportTable->AddressOfOrdinals);
    FunctionAddressTable = (uint32_t*)(PeFile->VirtualAddress + ExportTable->AddressOfFunctions);

    // Allocate statis array for exports
    PeFile->ExportedFunctions = (MCorePeExportFunction_t*)
        kmalloc(sizeof(MCorePeExportFunction_t) * ExportTable->NumberOfOrdinals);
    PeFile->NumberOfExportedFunctions = (int)ExportTable->NumberOfOrdinals;
    OrdinalBase = ExportTable->OrdinalBase;

    // Instantiate the list for exported functions
    TRACE("Number of functions to iterate: %u", ExportTable->NumberOfOrdinals);
    for (i = 0; i < PeFile->NumberOfExportedFunctions; i++) {
        // Setup the correct entry
        MCorePeExportFunction_t *ExFunc = &PeFile->ExportedFunctions[i];

        // Extract the function information
        ExFunc->Ordinal = (int)FunctionOrdinalsTable[i];
        ExFunc->Name = (char*)(PeFile->VirtualAddress + FunctionNamesTable[i]);
        if ((ExFunc->Ordinal - OrdinalBase) > ExportTable->NumberOfOrdinals) {
            ERROR("(%s) Found invalid ordinal index: %u (Ordinal %u, Base %u)", 
                MStringRaw(PeFile->Name), (ExFunc->Ordinal - OrdinalBase), ExFunc->Ordinal, OrdinalBase);
            ExFunc->Address = 0;
        }
        else {
            ExFunc->Address = (uintptr_t)(PeFile->VirtualAddress + FunctionAddressTable[ExFunc->Ordinal - OrdinalBase]);
        }
    }
}

/* PeHandleImports
 * Parses and resolves all image imports by parsing the import address table */
OsStatus_t
PeHandleImports(
    _In_ MCorePeFile_t *Parent,
    _In_ MCorePeFile_t *PeFile, 
    _In_ PeDataDirectory_t *ImportDirectory,
    _InOut_ uintptr_t *NextImageBase)
{
    // Variables
    PeImportDescriptor_t *ImportDescriptor = NULL;

    // Sanitize input
    if (ImportDirectory->AddressRVA == 0
        || ImportDirectory->Size == 0) {
        return OsSuccess;
    }

    // Initiate import
    ImportDescriptor = (PeImportDescriptor_t*)
        (PeFile->VirtualAddress + ImportDirectory->AddressRVA);
    while (ImportDescriptor->ImportAddressTable != 0) {
        // Local variables
        MCorePeFile_t *ResolvedLibrary      = NULL;
        MCorePeExportFunction_t *Exports    = NULL;
        MString_t *Name                     = NULL;
        char *NamePtr                       = NULL;
        int NumberOfExports                 = 0;

        // Initialize the string pointer 
        // and create a new mstring instance from it
        NamePtr     = (char*)(PeFile->VirtualAddress + ImportDescriptor->ModuleName);
        Name        = MStringCreate(NamePtr, StrUTF8);

        // Resolve the library from the import chunk
        ResolvedLibrary = PeResolveLibrary(Parent, PeFile, Name, NextImageBase);
        if (ResolvedLibrary == NULL || ResolvedLibrary->ExportedFunctions == NULL) {
            ERROR("(%s): Failed to resolve library %s", 
                MStringRaw(PeFile->Name), MStringRaw(Name));
            return OsError;
        }
        else {
            TRACE("(%s): Library %s resolved, %i functions available", 
                MStringRaw(PeFile->Name), MStringRaw(Name), 
                ResolvedLibrary->NumberOfExportedFunctions);
            Exports = ResolvedLibrary->ExportedFunctions;
            NumberOfExports = ResolvedLibrary->NumberOfExportedFunctions;
        }

        // Calculate address to IAT
        // These entries are 64 bit in PE32+ and 32 bit in PE32 
        if (PeFile->Architecture == PE_ARCHITECTURE_32) {
            uint32_t *Iat = (uint32_t*)
                (PeFile->VirtualAddress + ImportDescriptor->ImportAddressTable);

            /* Iterate Import table for this module */
            while (*Iat) {
                MCorePeExportFunction_t *Function   = NULL;
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
                        (PeFile->VirtualAddress + (Value & PE_IMPORT_NAMEMASK) + 2);
                    for (int i = 0; i < NumberOfExports; i++) {
                        if (!strcmp(Exports[i].Name, FunctionName)) {
                            Function = &Exports[i];
                            break;
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
                (PeFile->VirtualAddress + ImportDescriptor->ImportAddressTable);

            /* Iterate Import table for this module */
            while (*Iat) {
                MCorePeExportFunction_t *Function = NULL;
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
                        (PeFile->VirtualAddress + (uint32_t)(Value & PE_IMPORT_NAMEMASK) + 2);
                    for (int i = 0; i < NumberOfExports; i++) {
                        if (!strcmp(Exports[i].Name, FunctionName)) {
                            Function = &Exports[i];
                            break;
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
    _In_ MCorePeFile_t *Parent,
    _In_ MCorePeFile_t *PeFile,
    _In_ MString_t *LibraryName,
    _InOut_ uintptr_t *LoadAddress)
{
    // Variables
    MCorePeFile_t *ExportParent = Parent;
    MCorePeFile_t *Exports = NULL;

    // Sanitize the parent, because the parent will
    // be null when it's the root module
    if (ExportParent == NULL) {
        ExportParent = PeFile;
    }

    // Trace
    TRACE("PeResolveLibrary(Name %s, Address 0x%x)",
        MStringRaw(LibraryName), *LoadAddress);

    // Before actually loading the file, we want to
    // try to locate the library in the parent first.
    SpinlockAcquire(&ExportParent->LibraryLock);
    foreach(lNode, ExportParent->LoadedLibraries) {
        MCorePeFile_t *Library = (MCorePeFile_t*)lNode->Data;

        // If we find it, then increase the ref count
        // and use its exports
        if (MStringCompare(Library->Name, LibraryName, 1) == MSTRING_FULL_MATCH) {
            TRACE("Library was already resolved, increasing ref count");
            Library->References++;
            Exports = Library;
            break;
        }
    }
    SpinlockRelease(&ExportParent->LibraryLock);

    // Sanitize the exports, if its null we have to resolve the library
    if (Exports == NULL) {
        BufferObject_t *BufferObject = NULL;
        UUId_t fHandle = UUID_INVALID;
        MCorePeFile_t *Library = NULL;
        uint8_t *fBuffer = NULL;
        size_t fSize = 0, fRead = 0, fIndex = 0;

        // Open the file
        // We have a special case here that it might
        // be from the ramdisk we are loading
        if (ExportParent->UsingInitRD) {
            TRACE("Loading from ramdisk (%s)", MStringRaw(LibraryName));
            if (ModulesQueryPath(LibraryName, (void**)&fBuffer, &fSize) != OsSuccess) {
                ERROR("Failed to load library %s", MStringRaw(LibraryName));
                for (;;);
            }
        }
        else {
            // Load the file from hard storage
            FileSystemCode_t Code = 
                OpenFile(MStringRaw(LibraryName),
                __FILE_MUSTEXIST, __FILE_READ_ACCESS, &fHandle);
            
            // Sanitize the open file result
            if (Code != FsOk) {
                ERROR("Failed to load library %s (Code %i)",
                    MStringRaw(LibraryName), Code);
                for (;;);
            }

            /* Allocate a new buffer */
            GetFileSize(fHandle, &fSize, NULL);
            BufferObject = CreateBuffer(fSize);
            fBuffer = (uint8_t*)kmalloc(fSize);

            /* Read */
            ReadFile(fHandle, BufferObject, &fIndex, &fRead);
            ReadBuffer(BufferObject, (__CONST void*)fBuffer, fRead, NULL);

            /* Cleanup */
            DestroyBuffer(BufferObject);
            CloseFile(fHandle);
        }

        // After retrieving the data we can now
        // load the actual image
        TRACE("Parsing pe-image");
        Library = PeLoadImage(ExportParent, 
            LibraryName, fBuffer, fSize, LoadAddress, ExportParent->UsingInitRD);
        Exports = Library;

        // Cleanup buffer, we are done with it now
        if (!ExportParent->UsingInitRD) {
            kfree(fBuffer);
        }

        // Add library to loaded libs
        if (Exports != NULL) {
            DataKey_t Key;
            Key.Value = 0;
            SpinlockAcquire(&ExportParent->LibraryLock);
            CollectionAppend(ExportParent->LoadedLibraries, 
                CollectionCreateNode(Key, Library));
            SpinlockRelease(&ExportParent->LibraryLock);
        }
    }

    // Sanitize exports again, it's only NULL
    // if all our attempts failed!
    if (Exports == NULL) {
        ERROR("Library %s was unable to be resolved", 
            MStringRaw(LibraryName));
    }

    // Exporting done
    return Exports;
}

/* PeResolveFunction
 * Resolves a function by name in the given pe image, the return
 * value is the address of the function. 0 If not found */
uintptr_t
PeResolveFunction(
    _In_ MCorePeFile_t *Library, 
    _In_ __CONST char *Function)
{
    // Variables
    MCorePeExportFunction_t *Exports = Library->ExportedFunctions;
    for (int i = 0; i < Library->NumberOfExportedFunctions; i++) {
        if (!strcmp(Exports[i].Name, Function)) {
            return Exports[i].Address;
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
    _In_ MCorePeFile_t  *Parent,
    _In_ MString_t      *Name,
    _In_ uint8_t        *Buffer,
    _In_ size_t          Length,
    _InOut_ uintptr_t   *BaseAddress,
    _In_ int             UsingInitRD)
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
    int i;

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
    PeInfo                  = (MCorePeFile_t*)kmalloc(sizeof(MCorePeFile_t));
    memset(PeInfo, 0, sizeof(MCorePeFile_t));

    // Fill initial members
    PeInfo->Name            = Name;
    PeInfo->Architecture    = OptHeader->Architecture;
    PeInfo->VirtualAddress  = *BaseAddress;
    PeInfo->LoadedLibraries = CollectionCreate(KeyInteger);
    PeInfo->References      = 1;
    PeInfo->UsingInitRD     = UsingInitRD;
    SpinlockReset(&PeInfo->LibraryLock);

    // Set the entry point if there is any
    if (OptHeader->EntryPoint != 0) {
        PeInfo->EntryAddress = PeInfo->VirtualAddress + OptHeader->EntryPoint;
    }
    else {
        PeInfo->EntryAddress = 0;
    }

    // Copy sections to base address
    TRACE("Copying image meta-data to base of image address");
    for (i = 0; i < DIVUP(SizeOfMetaData, AddressSpaceGetPageSize()); i++) {
        uintptr_t VirtualPage = PeInfo->VirtualAddress + (i * AddressSpaceGetPageSize());
        if (!AddressSpaceGetMapping(AddressSpaceGetCurrent(), VirtualPage)) {
            AddressSpaceMap(AddressSpaceGetCurrent(), NULL, &VirtualPage,
                AddressSpaceGetPageSize(), ASPACE_FLAG_APPLICATION | ASPACE_FLAG_SUPPLIEDVIRTUAL, __MASK);
        }
    }
    memcpy((void*)PeInfo->VirtualAddress, Buffer, SizeOfMetaData);
        
    // Now we want to handle all the directories
    // and sections in the image, start out by handling
    // the sections, then parse all directories
    TRACE("Handling sections, relocations and exports");
    *BaseAddress = PeHandleSections(PeInfo, Buffer, 
        SectionAddress, BaseHeader->NumSections, 1);
    PeHandleRelocations(PeInfo, &DirectoryPtr[PE_SECTION_BASE_RELOCATION], ImageBase);
    PeHandleExports(PeInfo, &DirectoryPtr[PE_SECTION_EXPORT]);

    // Before loading imports, add us to parent list of libraries 
    // so we might be reused, instead of reloaded
    if (Parent != NULL) {
        DataKey_t Key;
        Key.Value = 0;
        SpinlockAcquire(&Parent->LibraryLock);
        CollectionAppend(Parent->LoadedLibraries, CollectionCreateNode(Key, PeInfo));
        SpinlockRelease(&Parent->LibraryLock);
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
        SpinlockAcquire(&Library->LibraryLock);
        foreach(lNode, Parent->LoadedLibraries) {
            MCorePeFile_t *lLib = (MCorePeFile_t*)lNode->Data;
            if (lLib == Library) {
                CollectionRemoveByNode(Parent->LoadedLibraries, lNode);
                kfree(lNode);
                break;
            }
        }
        SpinlockRelease(&Library->LibraryLock);

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
    if (Executable->LoadedLibraries != NULL) {
        foreach(Node, Executable->LoadedLibraries) {
            MCorePeFile_t *Library = (MCorePeFile_t*)Node->Data;
            ModuleList[Index++] = (Handle_t)Library->EntryAddress;
        }
    }
    return OsSuccess;
}
