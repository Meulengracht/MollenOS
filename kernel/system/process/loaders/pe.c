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

/* Includes 
 * - System */
#include <process/pe.h>
#include <modules/modules.h>
#include <heap.h>
#include <log.h>

/* Includes
 * - C-Library */
#include <ds/list.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>

/* PeCalculateChecksum
 * Perform a checksum calculation of the 
 * given PE file. Use this to validate contents
 * of a PE executable */
uint32_t PeCalculateChecksum(uint8_t *Data, size_t DataLength, size_t PeChkSumOffset)
{
	/* Variables for calculating the 
	 * pe-checksum */
	uint32_t *DataPtr = (uint32_t*)Data;
	uint64_t Limit = 4294967296;
	uint64_t CheckSum = 0;

	for (size_t i = 0; i < (DataLength / 4); i++, DataPtr++) {
		if (i == (PeChkSumOffset / 4)) {
			continue;
		}

		/* Add value to checksum */
		uint32_t Val = *DataPtr;
		CheckSum = (CheckSum & UINT32_MAX) + Val + (CheckSum >> 32);
		if (CheckSum > Limit) {
			CheckSum = (CheckSum & UINT32_MAX) + (CheckSum >> 32);
		}
	}

	CheckSum = (CheckSum & UINT16_MAX) + (CheckSum >> 16);
	CheckSum = (CheckSum) + (CheckSum >> 16);
	CheckSum = CheckSum & UINT16_MAX;

	/* Add length of data and return */
	CheckSum += (uint32_t)DataLength;
	return (uint32_t)(CheckSum & UINT32_MAX);
}

/* PeValidate
 * Validates a file-buffer of the given length,
 * does initial header checks and performs a checksum
 * validation. Returns either PE_INVALID or PE_VALID */
int PeValidate(uint8_t *Buffer, size_t Length)
{
	/* Variables needed for the validation */
	MzHeader_t *DosHeader = NULL;
	PeHeader_t *BaseHeader = NULL;
	PeOptionalHeader_t *OptHeader = NULL;
	size_t HeaderCheckSum = 0, CalculatedCheckSum = 0;
	size_t CheckSumAddress = 0;

	/* Initialize the first header pointer */
	DosHeader = (MzHeader_t*)Buffer;

	/* Validate */
	if (DosHeader->Signature != MZ_MAGIC) {
		LogFatal("PELD", "Invalid MZ Signature 0x%x", DosHeader->Signature);
		return PE_INVALID;
	}

	/* Get Pe Header */
	BaseHeader = (PeHeader_t*)(Buffer + DosHeader->PeAddr);

	/* Validate */
	if (BaseHeader->Magic != PE_MAGIC) {
		LogFatal("PELD", "Invalid PE File Magic 0x%x", BaseHeader->Magic);
		return PE_INVALID;
	}

	/* Validate the current build-target
	 * we don't load arm modules for a x86 */
	if (BaseHeader->Machine != PE_CURRENT_MACHINE) {
		LogFatal("PELD", "The image as built for machine type 0x%x, "
			 "which is not the current machine type.", BaseHeader->Machine);
		return PE_INVALID;
	}

	/* Load Optional Header */
	OptHeader = (PeOptionalHeader_t*)(Buffer + DosHeader->PeAddr + sizeof(PeHeader_t));

	/* Validate the current architecture,
	 * again we don't load 32 bit modules for 64 bit */
	if (OptHeader->Architecture != PE_CURRENT_ARCH) {
		LogFatal("PELD", "The image was built for architecture 0x%x, "
			"and was not supported by the current architecture.", 
			OptHeader->Architecture);
		return PE_INVALID;
	}

	/* Ok, time to validate the contents of the file
	 * by performing a checksum of the PE file */
	/* We need to re-cast based on architecture */
	if (OptHeader->Architecture == PE_ARCHITECTURE_32) {
		PeOptionalHeader32_t *OptHeader32 = 
			(PeOptionalHeader32_t*)(Buffer + DosHeader->PeAddr + sizeof(PeHeader_t));
		CheckSumAddress = (size_t)&(OptHeader32->ImageChecksum);
		HeaderCheckSum = OptHeader32->ImageChecksum;
	}
	else if (OptHeader->Architecture == PE_ARCHITECTURE_64) {
		PeOptionalHeader64_t *OptHeader64 = 
			(PeOptionalHeader64_t*)(Buffer + DosHeader->PeAddr + sizeof(PeHeader_t));
		CheckSumAddress = (size_t)&(OptHeader64->ImageChecksum);
		HeaderCheckSum = OptHeader64->ImageChecksum;
	}

	/* Now do the actual checksum calc */
	CalculatedCheckSum = 
		PeCalculateChecksum(Buffer, Length, CheckSumAddress - ((size_t)Buffer));

	/* Are they equal? x.x */
	if (CalculatedCheckSum != HeaderCheckSum) {
		LogFatal("PELD", "Invalid checksum of file (Header 0x%x, Calculated 0x%x)", 
			HeaderCheckSum, CalculatedCheckSum);
		return PE_INVALID;
	}

	/* Yay! Valid! */
	return PE_VALID;
}

/* PeHandleSections
 * Relocates and initializes all sections in the pe image
 * It also returns the last memory address of the relocations */
Addr_t PeHandleSections(MCorePeFile_t *PeFile, uint8_t *Data, 
	Addr_t SectionAddress, int NumSections, int UserSpace)
{
	/* Initialize pointers and some of the
	 * variables needed for iteration of sections*/
	PeSectionHeader_t *Section = (PeSectionHeader_t*)SectionAddress;
	Addr_t CurrentAddress = PeFile->VirtualAddress;
	char SectionName[PE_SECTION_NAME_LENGTH + 1];
	int i, j;

	/* Iterate sections */
	for (i = 0; i < NumSections; i++)
	{
		/* Calculate pointers, we need two of them, one that
		 * points to data in file, and one that points to where
		 * in memory we want to copy data to */
		uint8_t *FileBuffer = (uint8_t*)(Data + Section->RawAddr);
		uint8_t *Destination = (uint8_t*)(PeFile->VirtualAddress + Section->VirtualAddr);
		int PageCount = DIVUP(MAX(Section->RawSize, Section->VirtualSize), PAGE_SIZE);

		/* Make a local copy of the name, just in case
		 * we need to do some debug print */
		memcpy(&SectionName[0], &Section->Name[0], 8);
		SectionName[8] = 0;

		/* Iterate pages and map them in our memory space */
		for (j = 0; j < PageCount; j++) {
			Addr_t Calculated = (Addr_t)Destination + (j * PAGE_SIZE);
			if (!AddressSpaceGetMap(AddressSpaceGetCurrent(), Calculated)) {
				AddressSpaceMap(AddressSpaceGetCurrent(), Calculated, 
					PAGE_SIZE, MEMORY_MASK_DEFAULT, 
					(UserSpace == 1) ? ADDRESS_SPACE_FLAG_APPLICATION : 0);
			}
		}

		/* Handle sections specifics, we want to:
		 * BSS: Zero out the memory 
		 * Code: Copy memory 
		 * Data: Copy memory */
		if (Section->RawSize == 0
			|| (Section->Flags & PE_SECTION_BSS)) {
			memset(Destination, 0, Section->VirtualSize);
		}
		else if ((Section->Flags & PE_SECTION_CODE)
				|| (Section->Flags & PE_SECTION_DATA)) {
			memcpy(Destination, FileBuffer, Section->RawSize);

			/* Sanitize this special case, if the virtual size
			 * is large, this means there needs to be zeroed space
			 * afterwards */
			if (Section->VirtualSize > Section->RawSize) {
				memset((Destination + Section->RawSize), 0,
					(Section->VirtualSize - Section->RawSize));
			}
		}

		/* Increase Pointers */
		CurrentAddress = (PeFile->VirtualAddress + Section->VirtualAddr
			+ MAX(Section->RawSize, Section->VirtualSize));

		/* Go to next */
		Section++;
	}

	/* Return a page-aligned address that points to the
	 * next free relocation address*/
	if (CurrentAddress % PAGE_SIZE) {
		CurrentAddress += (PAGE_SIZE - (CurrentAddress % PAGE_SIZE));
	}

	/* Done! */
	return CurrentAddress;
}

/* PeHandleRelocations
 * Initializes and handles the code relocations in the pe image */
void PeHandleRelocations(MCorePeFile_t *PeFile, 
	PeDataDirectory_t *RelocDirectory, Addr_t ImageBase)
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
				Addr_t Offset = (PeFile->VirtualAddress + PageRVA + Value);
				Addr_t Translated = AddressSpaceTranslate(AddressSpaceGetCurrent(), 
					PeFile->VirtualAddress);

				/* Should we add or subtract? */
				if (Translated >= ImageBase) {
					Addr_t Delta = (Addr_t)(Translated - ImageBase);
					*((Addr_t*)Offset) += Delta;
				}
				else {
					Addr_t Delta = (Addr_t)(ImageBase - Translated);
					*((Addr_t*)Offset) -= Delta;
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
void PeHandleExports(MCorePeFile_t *PeFile, PeDataDirectory_t *ExportDirectory)
{
	/* Variables needed for enumerating the exports*/
	PeExportDirectory_t *ExportTable = NULL;
	uint32_t *FunctionNamesPtr = NULL;
	uint16_t *FunctionOrdinalsPtr = NULL;
	uint32_t *FunctionAddrPtr = NULL;
	DataKey_t Key;
	int i;

	/* Sanitize the directory first */
	if (ExportDirectory->AddressRVA == 0
		|| ExportDirectory->Size == 0) {
		return;
	}

	/* Cast */
	ExportTable = (PeExportDirectory_t*)(PeFile->VirtualAddress + ExportDirectory->AddressRVA);

	/* Calculate addresses */
	FunctionNamesPtr = (uint32_t*)(PeFile->VirtualAddress + ExportTable->AddressOfNames);
	FunctionOrdinalsPtr = (uint16_t*)(PeFile->VirtualAddress + ExportTable->AddressOfOrdinals);
	FunctionAddrPtr = (uint32_t*)(PeFile->VirtualAddress + ExportTable->AddressOfFunctions);

	/* Create the list */
	PeFile->ExportedFunctions = ListCreate(KeyInteger, LIST_NORMAL);

	/* Iterate */
	for (i = 0; i < (int)ExportTable->NumberOfFunctions; i++) {
		MCorePeExportFunction_t *ExFunc = 
			(MCorePeExportFunction_t*)kmalloc(sizeof(MCorePeExportFunction_t));

		/* Setup */
		ExFunc->Name = (char*)(PeFile->VirtualAddress + FunctionNamesPtr[i]);
		ExFunc->Ordinal = FunctionOrdinalsPtr[i];
		ExFunc->Address = AddressSpaceTranslate(AddressSpaceGetCurrent(), 
			(Addr_t)(PeFile->VirtualAddress + FunctionAddrPtr[ExFunc->Ordinal]));

		/* Add to list */
		Key.Value = (int)ExFunc->Ordinal;
		ListAppend(PeFile->ExportedFunctions, ListCreateNode(Key, Key, ExFunc));
	}
}

/* PeHandleImports
 * Parses and resolves all image imports by parsing the import address table */
void PeHandleImports(MCorePeFile_t *Parent, MCorePeFile_t *PeFile, 
	PeDataDirectory_t *ImportDirectory, Addr_t *NextImageBase)
{
	/* Variables for import */
	PeImportDescriptor_t *ImportDescriptor = NULL;

	/* Sanitize the directory */
	if (ImportDirectory->AddressRVA == 0
		|| ImportDirectory->Size == 0) {
		return;
	}

	/* Cast */
	ImportDescriptor = (PeImportDescriptor_t*)
		(PeFile->VirtualAddress + ImportDirectory->AddressRVA);

	/* Iterate untill we hit the null-descriptor 
	 * which marks the end of import-table */
	while (ImportDescriptor->ImportAddressTable != 0)
	{
		/* Variables for resolving the library */
		MCorePeFile_t *ResolvedLibrary = NULL;
		List_t *Exports = NULL;
		MString_t *Name = NULL;
		char *NamePtr = NULL;

		/* Initialize the string pointer 
		 * and create a new mstring instance from it */
		NamePtr = (char*)(PeFile->VirtualAddress + ImportDescriptor->ModuleName);
		Name = MStringCreate(NamePtr, StrUTF8);

		/* Try to resolve the library we import from */
		ResolvedLibrary = PeResolveLibrary(Parent, PeFile, Name, NextImageBase);

		if (ResolvedLibrary == NULL
			|| ResolvedLibrary->ExportedFunctions == NULL) {
			return;
		}
		else {
			Exports = ResolvedLibrary->ExportedFunctions;
		}

		/* Calculate address to IAT
		 * These entries are 64 bit in PE32+
		 * and 32 bit in PE32 */
		if (PeFile->Architecture == PE_ARCHITECTURE_32) {
			uint32_t *Iat = (uint32_t*)
				(PeFile->VirtualAddress + ImportDescriptor->ImportAddressTable);

			/* Iterate Import table for this module */
			while (*Iat) {
				MCorePeExportFunction_t *Function = NULL;
				char *FunctionName = NULL;
				uint32_t Value = *Iat;

				/* Is it an ordinal or a function name? */
				if (Value & PE_IMPORT_ORDINAL_32) {
					DataKey_t oKey;
					oKey.Value = (int)(Value & 0xFFFF);
					Function = (MCorePeExportFunction_t*)
						ListGetDataByKey(Exports, oKey, 0);
				}
				else {
					/* Nah, pointer to function name, 
					 * where two first bytes are hint? */
					FunctionName = (char*)
						(PeFile->VirtualAddress + (Value & PE_IMPORT_NAMEMASK) + 2);

					/* A little bit more tricky, we now have to
					 * locate the function by name in exported functions */
					foreach(FuncNode, Exports) {
						MCorePeExportFunction_t *pFunc =
							(MCorePeExportFunction_t*)FuncNode->Data;
						if (!strcmp(pFunc->Name, FunctionName)) {
							Function = pFunc;
							break;
						}
					}
				}

				/* Sanitize whether or not we were able to
				 * locate the function */
				if (Function == NULL) {
					LogFatal("PELD", "Failed to locate function (%s)", 
						FunctionName);
					return;
				}

				/* Now, overwrite the IAT Entry with the actual address */
				*Iat = Function->Address;

				/* Go to next */
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
					DataKey_t oKey;
					oKey.Value = (int)(Value & 0xFFFF);
					Function = (MCorePeExportFunction_t*)
						ListGetDataByKey(Exports, oKey, 0);
				}
				else {
					/* Nah, pointer to function name, 
					 * where two first bytes are hint? */
					char *FuncName = (char*)
						(PeFile->VirtualAddress + (uint32_t)(Value & PE_IMPORT_NAMEMASK) + 2);

					/* A little bit more tricky, we now have to
					 * locate the function by name in exported functions */
					foreach(FuncNode, Exports) {
						MCorePeExportFunction_t *pFunc =
							(MCorePeExportFunction_t*)FuncNode->Data;
						if (!strcmp(pFunc->Name, FuncName)) {
							Function = pFunc;
							break;
						}
					}
				}

				/* Sanitize whether or not we were able to
				 * locate the function */
				if (Function == NULL) {
					LogFatal("PELD", "Failed to locate function");
					return;
				}


				/* Now, overwrite the IAT Entry with the actual address */
				*Iat = (uint64_t)Function->Address;

				/* Go to next */
				Iat++;
			}
		}

		/* Next ! */
		ImportDescriptor++;
	}
}

/* PeResolveLibrary
 * Resolves a dependancy or a given module path, a load address must be provided
 * together with a pe-file header to fill out and the parent that wants to resolve
 * the library */
MCorePeFile_t *PeResolveLibrary(MCorePeFile_t *Parent, 
	MCorePeFile_t *PeFile, MString_t *LibraryName, Addr_t *LoadAddress)
{
	/* Variables needed */
	MCorePeFile_t *ExportParent = Parent;
	MCorePeFile_t *Exports = NULL;

	/* Sanitize the parent, because the parent will
	 * be null when it's the root module */
	if (ExportParent == NULL) {
		ExportParent = PeFile;
	}

	/* Before actually loading the file, we want to
	 * try to locate the library in the parent first. */
	foreach(lNode, ExportParent->LoadedLibraries) {
		MCorePeFile_t *Library = (MCorePeFile_t*)lNode->Data;

		/* If we find it, then increase the ref count
		 * and use its exports */
		if (MStringCompare(Library->Name, LibraryName, 1) == MSTRING_FULL_MATCH) {
			Library->References++;
			Exports = Library;
			break;
		}
	}

	/* Sanitize the exports, if its null
	 * we have to resolve the library */
	if (Exports == NULL) {
		MCoreFileInstance_t *lFile = NULL;
		MCorePeFile_t *Library = NULL;
		uint8_t *fBuffer = NULL;
		size_t fSize = 0;

		/* Open the file
		 * We have a special case here that it might
		 * be from the ramdisk we are loading */
		if (ExportParent->UsingInitRD) {
			if (ModulesQueryPath(LibraryName, &fBuffer, &fSize) != OsNoError) {
				LogDebug("PELD", "Failed to load library %s (Code %i)",
					MStringRaw(LibraryName), lFile->Code);
				for (;;);
			}
		}
		else {
			/* Load the file */
			lFile = VfsWrapperOpen(MStringRaw(LibraryName), Read);

			/* Sanity */
			if (lFile == NULL || lFile->Code != VfsOk || lFile->File == NULL) {
				LogDebug("PELD", "Failed to load library %s (Code %i)",
					MStringRaw(LibraryName), lFile->Code);
				for (;;);
			}

			/* Allocate a new buffer */
			fSize = (size_t)lFile->File->Size;
			fBuffer = (uint8_t*)kmalloc(fSize);

			/* Read all data */
			VfsWrapperRead(lFile, fBuffer, fSize);

			/* Cleanup */
			VfsWrapperClose(lFile);
		}

		/* After retrieving the data we can now
		 * load the actual image */
		Library = PeLoadImage(ExportParent, 
			LibraryName, fBuffer, fSize, LoadAddress, ExportParent->UsingInitRD);
		Exports = Library;

		/* Cleanup buffer, we are done with it now */
		if (!ExportParent->UsingInitRD) {
			kfree(fBuffer);
		}

		/* Add library to loaded libs */
		if (Exports != NULL) {
			DataKey_t Key;
			Key.Value = 0;
			ListAppend(ExportParent->LoadedLibraries, 
				ListCreateNode(Key, Key, Library));
		}
	}

	/* Sanitize exports again, it's only NULL
	 * if all our attempts failed! */
	if (Exports == NULL) {
		LogFatal("PELD", "Library %s was unable to be resolved", 
			MStringRaw(LibraryName));
	}

	/* Done */
	return Exports;
}

/* PeResolveFunction
 * Resolves a function by name in the given pe image, the return
 * value is the address of the function. 0 If not found */
Addr_t PeResolveFunction(MCorePeFile_t *Library, const char *Function)
{
	/* Variables for finding the function */
	List_t *Exports = Library->ExportedFunctions;

	/* Ok, so we iterate exported function and try to
	 * locate the function by its name */
	foreach(lNode, Exports) {
		MCorePeExportFunction_t *ExFunc = 
			(MCorePeExportFunction_t*)lNode->Data;
		if (!strcmp(ExFunc->Name, Function)) {
			return ExFunc->Address;
		}
	}

	/* Damn.. */
	return 0;
}

/* PeLoadImage
 * Loads the given file-buffer as a pe image into the current address space 
 * at the given Base-Address, which is updated after load to reflect where
 * the next address is available for load */
MCorePeFile_t *PeLoadImage(MCorePeFile_t *Parent, MString_t *Name, 
	uint8_t *Buffer, size_t Length, Addr_t *BaseAddress, int UsingInitRD)
{
	/* Base Headers */
	MzHeader_t *DosHeader = NULL;
	PeHeader_t *BaseHeader = NULL;
	PeOptionalHeader_t *OptHeader = NULL;

	/* This depends on the architecture */
	PeOptionalHeader32_t *OptHeader32 = NULL;
	PeOptionalHeader64_t *OptHeader64 = NULL;

	/* Variables for loading the image */
	Addr_t SectionAddress = 0;
	Addr_t ImageBase = 0;
	PeDataDirectory_t *DirectoryPtr = NULL;
	MCorePeFile_t *PeInfo = NULL;

	/* Start out by validating the file buffer
	 * so we don't load any garbage */
	if (!PeValidate(Buffer, Length)) {
		return NULL;
	}
	
	/* Start out by initializing our header pointers */
	DosHeader = (MzHeader_t*)Buffer;
	BaseHeader = (PeHeader_t*)(Buffer + DosHeader->PeAddr);
	OptHeader = (PeOptionalHeader_t*)
		(Buffer + DosHeader->PeAddr + sizeof(PeHeader_t));

	/* We need to re-cast based on architecture 
	 * and handle them differnetly */
	if (OptHeader->Architecture == PE_ARCHITECTURE_32) {
		OptHeader32 = (PeOptionalHeader32_t*)(Buffer 
			+ DosHeader->PeAddr + sizeof(PeHeader_t));
		ImageBase = OptHeader32->BaseAddress;
		SectionAddress = (Addr_t)(Buffer + DosHeader->PeAddr 
			+ sizeof(PeHeader_t) + sizeof(PeOptionalHeader32_t));
		DirectoryPtr = (PeDataDirectory_t*)&OptHeader32->Directories[0];
	}
	else if (OptHeader->Architecture == PE_ARCHITECTURE_64) {
		OptHeader64 = (PeOptionalHeader64_t*)(Buffer 
			+ DosHeader->PeAddr + sizeof(PeHeader_t));
		ImageBase = (Addr_t)OptHeader64->BaseAddress;
		SectionAddress = (Addr_t)(Buffer + DosHeader->PeAddr 
			+ sizeof(PeHeader_t) + sizeof(PeOptionalHeader64_t));
		DirectoryPtr = (PeDataDirectory_t*)&OptHeader64->Directories[0];
	}

	/* Allocate a new pe image file structure */
	PeInfo = (MCorePeFile_t*)kmalloc(sizeof(MCorePeFile_t));
	memset(PeInfo, 0, sizeof(MCorePeFile_t));

	/* Set base information */
	PeInfo->Name = Name;
	PeInfo->Architecture = OptHeader->Architecture;
	PeInfo->VirtualAddress = *BaseAddress;
	PeInfo->LoadedLibraries = ListCreate(KeyInteger, LIST_NORMAL);
	PeInfo->References = 1;
	PeInfo->UsingInitRD = UsingInitRD;

	/* Set the entry point if there is any */
	if (OptHeader->EntryPoint != 0) {
		PeInfo->EntryAddress = AddressSpaceTranslate(AddressSpaceGetCurrent(), 
			PeInfo->VirtualAddress + OptHeader->EntryPoint);
	}
	else {
		PeInfo->EntryAddress = 0;
	}
		
	/* Now we want to handle all the directories
	 * and sections in the image, start out by handling
	 * the sections, then parse all directories */
	*BaseAddress = PeHandleSections(PeInfo, Buffer, 
		SectionAddress, BaseHeader->NumSections, 1);
	PeHandleRelocations(PeInfo, &DirectoryPtr[PE_SECTION_BASE_RELOCATION], ImageBase);
	PeHandleExports(PeInfo, &DirectoryPtr[PE_SECTION_EXPORT]);

	/* Before loading imports, add us to parent list of libraries 
	 * so we might be reused, instead of reloaded */
	if (Parent != NULL) {
		DataKey_t Key;
		Key.Value = 0;
		ListAppend(Parent->LoadedLibraries, ListCreateNode(Key, Key, PeInfo));
	}

	/* Now we can handle the imports */
	PeHandleImports(Parent, PeInfo, &DirectoryPtr[PE_SECTION_IMPORT], BaseAddress);

	/* Done */
	return PeInfo;
}

/* PeUnloadLibrary
 * Unload dynamically loaded library 
 * This only cleans up in the case there are no more references */
void PeUnloadLibrary(MCorePeFile_t *Parent, MCorePeFile_t *Library)
{
	/* Decrease reference count */
	Library->References--;

	/* Sanitize the ref count
	 * we might have to unload it if there are
	 * no more references */
	if (Library->References <= 0) 
	{
		/* Remove it from list */
		foreach(lNode, Parent->LoadedLibraries) {
			MCorePeFile_t *lLib = (MCorePeFile_t*)lNode->Data;
			if (lLib == Library) {
				ListRemoveByNode(Parent->LoadedLibraries, lNode);
				kfree(lNode);
				break;
			}
		}

		/* Unload it */
		PeUnloadImage(Library);
	}
}

/* PeUnloadImage
 * Unload executables, all it's dependancies and free it's resources */
void PeUnloadImage(MCorePeFile_t *Executable)
{
	/* Variables */
	ListNode_t *Node;

	/* Sanitize the parameter */
	if (Executable == NULL) {
		return;
	}

	/* Free Strings */
	kfree(Executable->Name);

	/* Cleanup exported functions */
	if (Executable->ExportedFunctions != NULL) {
		_foreach(Node, Executable->ExportedFunctions) {
			MCorePeExportFunction_t *ExFunc = 
				(MCorePeExportFunction_t*)Node->Data;
			kfree(ExFunc);
		}

		/* Destroy list */
		ListDestroy(Executable->ExportedFunctions);
	}

	/* Cleanup libraries */
	if (Executable->LoadedLibraries != NULL) {
		_foreach(Node, Executable->LoadedLibraries) {
			MCorePeFile_t *Library = (MCorePeFile_t*)Node->Data;
			PeUnloadImage(Library);
		}

		/* Destroy list */
		ListDestroy(Executable->LoadedLibraries);
	}

	/* Free structure */
	kfree(Executable);
}
