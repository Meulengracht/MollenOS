/* MollenOS
*
* Copyright 2011 - 2016, Philip Meulengracht
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

/* Includes */
#include <Modules\ModuleManager.h>
#include <Modules\PeLoader.h>
#include <Vfs/VfsWrappers.h>
#include <Log.h>
#include <Heap.h>

#include <ds/list.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>

/* Keep track of where to load 
 * the next module */
List_t *GlbKernelExports = NULL;
Addr_t GlbModuleLoadAddr = MEMORY_LOCATION_MODULES;

/* Parse Kernel Exports */
void PeLoadKernelExports(Addr_t KernelBase, Addr_t TableOffset)
{
	/* Vars */
	PeExportDirectory_t *ExportTable = NULL;
	uint32_t *FunctionNamesPtr = NULL;
	uint16_t *FunctionOrdinalsPtr = NULL;
	uint32_t *FunctionAddrPtr = NULL;
	DataKey_t Key;
	uint32_t i;

	/* Sanity */
	if (TableOffset == 0)
		return;

	/* Info */
	LogInformation("PELD", "Loading Kernel Exports");

	/* Init list */
	GlbModuleLoadAddr = MEMORY_LOCATION_MODULES;
	GlbKernelExports = ListCreate(KeyInteger, LIST_NORMAL);

	/* Cast */
	ExportTable = (PeExportDirectory_t*)(KernelBase + TableOffset);

	/* Calculate addresses */
	FunctionNamesPtr = (uint32_t*)(KernelBase + ExportTable->AddressOfNames);
	FunctionOrdinalsPtr = (uint16_t*)(KernelBase + ExportTable->AddressOfOrdinals);
	FunctionAddrPtr = (uint32_t*)(KernelBase + ExportTable->AddressOfFunctions); 

	/* Iterate */
	for (i = 0; i < ExportTable->NumberOfFunctions; i++)
	{
		/* Allocate a new entry */
		MCorePeExportFunction_t *ExFunc =
			(MCorePeExportFunction_t*)kmalloc(sizeof(MCorePeExportFunction_t));

		/* Setup */
		ExFunc->Name = (char*)(KernelBase + FunctionNamesPtr[i]);
		ExFunc->Ordinal = FunctionOrdinalsPtr[i];
		ExFunc->Address = (Addr_t)(KernelBase + FunctionAddrPtr[ExFunc->Ordinal]);

		/* Add to list */
		Key.Value = (int)ExFunc->Ordinal;
		ListAppend(GlbKernelExports, ListCreateNode(Key, Key, ExFunc));
	}

	/* Info */
	LogInformation("PELD", "Found %u Functions", GlbKernelExports->Length);
}

/* Validate a buffer containing a PE */
int PeValidate(uint8_t *Buffer)
{
	/* Headers */
	MzHeader_t *DosHeader = NULL;
	PeHeader_t *BaseHeader = NULL;
	PeOptionalHeader_t *OptHeader = NULL;

	/* Let's see */
	DosHeader = (MzHeader_t*)Buffer;

	/* Validate */
	if (DosHeader->Signature != MZ_MAGIC)
	{
		LogFatal("PELD", "Invalid MZ Signature 0x%x", DosHeader->Signature);
		return 0;
	}

	/* Get Pe Header */
	BaseHeader = (PeHeader_t*)(Buffer + DosHeader->PeAddr);

	/* Validate */
	if (BaseHeader->Magic != PE_MAGIC)
	{
		LogFatal("PELD", "Invalid PE File Magic 0x%x", BaseHeader->Magic);
		return 0;
	}

	/* Validate Machine */
	if (BaseHeader->Machine != PE_MACHINE_X32
		&& BaseHeader->Machine != PE_MACHINE_X64)
	{
		LogFatal("PELD", "Unsupported Machine 0x%x", BaseHeader->Machine);
		return 0;
	}

	/* Load Optional Header */
	OptHeader = (PeOptionalHeader_t*)(Buffer + DosHeader->PeAddr + sizeof(PeHeader_t));

	/* Validate Optional */
	if (OptHeader->Architecture != PE_ARCHITECTURE_32
		&& OptHeader->Architecture != PE_ARCHITECTURE_64)
	{
		LogFatal("PELD", "Unsupported Machine 0x%x", BaseHeader->Machine);
		return 0;
	}

	/* Yay! Valid! */
	return 1;
}

/* Relocate Sections */
Addr_t PeRelocateSections(MCorePeFile_t *PeFile, uint8_t *Data, 
	Addr_t SectionAddr, uint16_t NumSections, int UserSpace)
{
	/* Cast to a pointer */
	PeSectionHeader_t *Section = (PeSectionHeader_t*)SectionAddr;
	Addr_t MemAddr = PeFile->BaseVirtual;
	char TmpName[9];
	uint32_t i, j;

	/* Iterate */
	for (i = 0; i < (uint32_t)NumSections; i++)
	{
		/* Calculate pointers */
		uint8_t *FileBuffer = (uint8_t*)(Data + Section->RawAddr);
		uint8_t *MemBuffer = (uint8_t*)(PeFile->BaseVirtual + Section->VirtualAddr);

		/* Calculate pages needed */
		uint32_t NumPages = DIVUP(MAX(Section->RawSize, Section->VirtualSize), PAGE_SIZE);

		/* Copy Name */
		memcpy(&TmpName[0], &Section->Name[0], 8);
		TmpName[8] = 0;

		/* Is it mapped ? */
		for (j = 0; j < NumPages; j++)
		{
			/* Calculate Address */
			Addr_t pAddr = (Addr_t)MemBuffer + (j * PAGE_SIZE);

			/* Is it mapped? */
			if (!AddressSpaceGetMap(AddressSpaceGetCurrent(), pAddr))
				AddressSpaceMap(AddressSpaceGetCurrent(), pAddr, PAGE_SIZE,
				MEMORY_MASK_DEFAULT, (UserSpace == 1) ? ADDRESS_SPACE_FLAG_USER : 0);
		}

		/* Which kind of section is this */
		if (Section->RawSize == 0
			|| (Section->Flags & PE_SECTION_BSS))
		{
			/* We should zero this */
			memset(MemBuffer, 0, Section->VirtualSize);
		}
		else if ((Section->Flags & PE_SECTION_CODE)
			|| (Section->Flags & PE_SECTION_DATA))
		{
			/* Copy section */
			memcpy(MemBuffer, FileBuffer, Section->RawSize);

			/* Sanity */
			if (Section->VirtualSize > Section->RawSize)
				memset((MemBuffer + Section->RawSize), 0, 
					   (Section->VirtualSize - Section->RawSize));
		}

		/* Increase Pointers */
		MemAddr = (PeFile->BaseVirtual + Section->VirtualAddr 
			+ MAX(Section->RawSize, Section->VirtualSize));

		/* Go to next */
		Section++;
	}

	/* Page Align */
	if (MemAddr % PAGE_SIZE)
		MemAddr += (PAGE_SIZE - (MemAddr % PAGE_SIZE));

	/* Done! */
	return MemAddr;
}

/* Relocate Code */
void PeFixRelocations(MCorePeFile_t *PeFile, PeDataDirectory_t *RelocDirectory, Addr_t ImageBase)
{
	/* Get a pointer to the table */
	uint32_t *RelocPtr = (uint32_t*)(PeFile->BaseVirtual + RelocDirectory->AddressRVA);
	uint16_t *rEntry = NULL;
	uint8_t *AdvPtr = NULL;
	uint32_t Itr = 0;

	/* Iterate untill we run out of space */
	uint32_t BytesLeft = RelocDirectory->Size;

	/* Sanity */
	if (RelocDirectory->AddressRVA == 0
		|| RelocDirectory->Size == 0)
		return;

	while (BytesLeft > 0)
	{
		/* Get Page RVA */
		uint32_t PageRVA = *RelocPtr++;
		uint32_t BlockSize = 0;
		uint32_t NumRelocs = 0;

		/* Get block size */
		BlockSize = *RelocPtr++;

		/* Sanitize the block size */
		if (BlockSize > BytesLeft) {
			LogFatal("PELD", "Invalid relocation data: BlockSize > BytesLeft, bailing");
			break;
		}

		/* Decrease */
		BytesLeft -= BlockSize;

		/* Now, entries come */
		if (BlockSize != 0)
			NumRelocs = (BlockSize - 8) / sizeof(uint16_t);
		else {
			LogFatal("PELD", "Invalid relocation data: BlockSize == 0, bailing");
			break;
		}

		/* Create a pointer */
		rEntry = (uint16_t*)RelocPtr;

		/* Iterate */
		for (Itr = 0; Itr < NumRelocs; Itr++)
		{
			/* Create a delta based on the relocation type */
			uint16_t RelocEntry = *rEntry;
			uint16_t Type = (RelocEntry >> 12);
			uint16_t Value = RelocEntry & 0x0FFF;

			/* 32 Bit Difference */
			if (Type == PE_RELOCATION_HIGHLOW)
			{
				/* Create a pointer, the low 12 bits have 
				 * an offset into the PageRVA */
				Addr_t Offset = (PeFile->BaseVirtual + PageRVA + Value);

				/* Should we add or subtract? */
				if (PeFile->BaseVirtual >= ImageBase) {
					/* Calculate Delta */
					Addr_t Delta = (Addr_t)(PeFile->BaseVirtual - ImageBase);

					/* Update */
					*((Addr_t*)Offset) += Delta;
				}
				else {
					/* Calculate Delta */
					Addr_t Delta = (Addr_t)(ImageBase - PeFile->BaseVirtual);

					/* Update */
					*((Addr_t*)Offset) -= Delta;
				}
			}

			/* Next */
			rEntry++;
		}

		/* Adjust Ptr */
		AdvPtr = (uint8_t*)RelocPtr;
		AdvPtr += (BlockSize - 8);
		RelocPtr = (uint32_t*)AdvPtr;
	}
}

/* Enumerate Exports */
void PeEnumerateExports(MCorePeFile_t *PeFile, PeDataDirectory_t *ExportDirectory)
{
	/* Vars */
	PeExportDirectory_t *ExportTable = NULL;
	uint32_t *FunctionNamesPtr = NULL;
	uint16_t *FunctionOrdinalsPtr = NULL;
	uint32_t *FunctionAddrPtr = NULL;
	DataKey_t Key;
	uint32_t i;

	/* Sanity */
	if (ExportDirectory->AddressRVA == 0
		|| ExportDirectory->Size == 0)
		return;

	/* Cast */
	ExportTable = (PeExportDirectory_t*)(PeFile->BaseVirtual + ExportDirectory->AddressRVA);

	/* Calculate addresses */
	FunctionNamesPtr = (uint32_t*)(PeFile->BaseVirtual + ExportTable->AddressOfNames);
	FunctionOrdinalsPtr = (uint16_t*)(PeFile->BaseVirtual + ExportTable->AddressOfOrdinals);
	FunctionAddrPtr = (uint32_t*)(PeFile->BaseVirtual + ExportTable->AddressOfFunctions);

	/* Create the list */
	PeFile->ExportedFunctions = ListCreate(KeyInteger, LIST_NORMAL);

	/* Iterate */
	for (i = 0; i < ExportTable->NumberOfFunctions; i++)
	{
		/* Allocate a new entry */
		MCorePeExportFunction_t *ExFunc = 
			(MCorePeExportFunction_t*)kmalloc(sizeof(MCorePeExportFunction_t));

		/* Setup */
		ExFunc->Name = (char*)(PeFile->BaseVirtual + FunctionNamesPtr[i]);
		ExFunc->Ordinal = FunctionOrdinalsPtr[i];
		ExFunc->Address = (Addr_t)(PeFile->BaseVirtual + FunctionAddrPtr[ExFunc->Ordinal]);

		/* Add to list */
		Key.Value = (int)ExFunc->Ordinal;
		ListAppend(PeFile->ExportedFunctions, ListCreateNode(Key, Key, ExFunc));
	}
}

/* Load Imports for Kernel Module */
void PeLoadModuleImports(MCorePeFile_t *PeFile, PeDataDirectory_t *ImportDirectory)
{
	/* Vars */
	PeImportDescriptor_t *ImportDescriptor = NULL;

	/* Sanity */
	if (ImportDirectory->AddressRVA == 0
		|| ImportDirectory->Size == 0)
		return;

	/* Cast */
	ImportDescriptor = (PeImportDescriptor_t*)(PeFile->BaseVirtual + ImportDirectory->AddressRVA);

	/* Iterate untill
	 * we hit the null-descriptor */
	while (ImportDescriptor->ImportAddressTable != 0)
	{
		/* Get name of module */
		List_t *Exports = NULL;
		char *NamePtr = (char*)(PeFile->BaseVirtual + ImportDescriptor->ModuleName);

		/* Is it the kernel module ? */
		if (!strcmp(NamePtr, PE_KERNEL_MODULE))
		{
			/* Yes, use the kernel */
			Exports = GlbKernelExports;
		}
		else
		{
			MString_t *Name = MStringCreate(NamePtr, StrUTF8);

			/* Find Module */
			MCoreModule_t *Module = ModuleFindStr(Name);

			/* Sanity */
			if (Module == NULL)
			{
				LogFatal("PELD", "Failed to locate module %s", Name->Data);
				return;
			}

			/* Bind Module if it's not already */
			if (Module->Descriptor == NULL)
				ModuleLoad(Module, NULL);
			
			/* Sanity */
			if (Module->Descriptor->ExportedFunctions == NULL)
			{
				LogFatal("PELD", "Module %s does not export anything", Name->Data);
				return;
			}

			/* Cleanup */
			MStringDestroy(Name);

			/* Set */
			Exports = Module->Descriptor->ExportedFunctions;
		}

		/* Calculate address to IAT 
		 * These entries are 64 bit in PE32+ 
		 * and 32 bit in PE32 */
		if (PeFile->Architecture == PE_ARCHITECTURE_32)
		{
			uint32_t *Iat = (uint32_t*)(PeFile->BaseVirtual + ImportDescriptor->ImportAddressTable);

			/* Iterate Import table for this module */
			while (*Iat)
			{
				/* Get value */
				uint32_t Value = *Iat;

				/* We store the bound func */
				MCorePeExportFunction_t *Func = NULL;

				/* Is it an ordinal or a function name? */
				if (Value & PE_IMPORT_ORDINAL_32)
				{
					/* Yes, ordinal */
					DataKey_t Key;
					Key.Value = (int)(Value & 0xFFFF);

					/* Locate Ordinal in loaded image */
					Func = (MCorePeExportFunction_t*)ListGetDataByKey(Exports, Key, 0);
				}
				else
				{
					/* Nah, pointer to function name, where two first bytes are hint? */
					char *FuncName = (char*)(PeFile->BaseVirtual + (Value & PE_IMPORT_NAMEMASK) + 2);

					/* A little bit more tricky */
					foreach(FuncNode, Exports)
					{
						/* Cast */
						MCorePeExportFunction_t *pFunc =
							(MCorePeExportFunction_t*)FuncNode->Data;

						/* Compare */
						if (!strcmp(pFunc->Name, FuncName))
						{
							/* Found it */
							Func = pFunc;
							break;
						}
					}
				}

				/* Sanity */
				if (Func == NULL)
				{
					LogFatal("PELD", "Failed to locate function");
					return;
				}

				/* Now, overwrite the IAT Entry with the actual address */
				*Iat = Func->Address;

				/* Go to next */
				Iat++;
			}
		}
		else
		{
			uint64_t *Iat = (uint64_t*)(PeFile->BaseVirtual + ImportDescriptor->ImportAddressTable);

			/* Iterate Import table for this module */
			while (*Iat)
			{
				/* Get value */
				uint64_t Value = *Iat;

				/* We store the bound func */
				MCorePeExportFunction_t *Func = NULL;

				/* Is it an ordinal or a function name? */
				if (Value & PE_IMPORT_ORDINAL_64)
				{
					/* Yes, ordinal */
					DataKey_t oKey;
					oKey.Value = (int)(Value & 0xFFFF);

					/* Locate Ordinal in loaded image */
					Func = (MCorePeExportFunction_t*)ListGetDataByKey(Exports, oKey, 0);
				}
				else
				{
					/* Nah, pointer to function name, where two first bytes are hint? */
					char *FuncName = (char*)(PeFile->BaseVirtual + (uint32_t)(Value & PE_IMPORT_NAMEMASK) + 2);

					/* A little bit more tricky */
					foreach(FuncNode, Exports)
					{
						/* Cast */
						MCorePeExportFunction_t *pFunc =
							(MCorePeExportFunction_t*)FuncNode->Data;

						/* Compare */
						if (!strcmp(pFunc->Name, FuncName))
						{
							/* Found it */
							Func = pFunc;
							break;
						}
					}
				}

				/* Sanity */
				if (Func == NULL)
				{
					LogFatal("PELD", "Failed to locate function");
					return;
				}

				/* Now, overwrite the IAT Entry with the actual address */
				*Iat = (uint64_t)Func->Address;

				/* Go to next */
				Iat++;
			}
		}

		/* Next ! */
		ImportDescriptor++;
	}
}

/* Load Module into memory */
MCorePeFile_t *PeLoadModule(uint8_t *Name, uint8_t *Buffer, size_t Length)
{
	/* Headers */
	MzHeader_t *DosHeader = NULL;
	PeHeader_t *BaseHeader = NULL;
	PeOptionalHeader_t *OptHeader = NULL;

	/* Depends on Arch */
	PeOptionalHeader32_t *OptHeader32 = NULL;
	PeOptionalHeader64_t *OptHeader64 = NULL;

	/* Vars */
	Addr_t SectionAddr = 0;
	Addr_t ImageBase = 0;
	PeDataDirectory_t *DirectoryPtr = NULL;
	MCorePeFile_t *PeInfo = NULL;

	/* Validate */
	if (!PeValidate(Buffer))
		return NULL;

	_CRT_UNUSED(Length);

	/* Let's see */
	DosHeader = (MzHeader_t*)Buffer;

	/* Get Pe Header */
	BaseHeader = (PeHeader_t*)(Buffer + DosHeader->PeAddr);

	/* Load Optional Header */
	OptHeader = (PeOptionalHeader_t*)(Buffer + DosHeader->PeAddr + sizeof(PeHeader_t));

	/* We need to re-cast based on architecture */
	if (OptHeader->Architecture == PE_ARCHITECTURE_32)
	{
		/* This is an 32 bit */
		OptHeader32 = (PeOptionalHeader32_t*)(Buffer + DosHeader->PeAddr + sizeof(PeHeader_t));
		ImageBase = OptHeader32->BaseAddress;

		/* Calc address of first section */
		SectionAddr = (Addr_t)(Buffer + DosHeader->PeAddr + sizeof(PeHeader_t) + sizeof(PeOptionalHeader32_t));

		/* Set directory pointer */
		DirectoryPtr = (PeDataDirectory_t*)&OptHeader32->Directories[0];
	}
	else if (OptHeader->Architecture == PE_ARCHITECTURE_64)
	{
		/* 64 Bit! */
		OptHeader64 = (PeOptionalHeader64_t*)(Buffer + DosHeader->PeAddr + sizeof(PeHeader_t));
		ImageBase = (Addr_t)OptHeader64->BaseAddress;

		/* Calc address of first section */
		SectionAddr = (Addr_t)(Buffer + DosHeader->PeAddr + sizeof(PeHeader_t) + sizeof(PeOptionalHeader64_t));

		/* Set directory pointer */
		DirectoryPtr = (PeDataDirectory_t*)&OptHeader64->Directories[0];
	}

	/* Allocate data */
	PeInfo = (MCorePeFile_t*)kmalloc(sizeof(MCorePeFile_t));
	memset(PeInfo, 0, sizeof(MCorePeFile_t));
	
	/* Set base information */
	PeInfo->Name = MStringCreate(Name, StrUTF8);
	PeInfo->Architecture = OptHeader->Architecture;
	PeInfo->BaseVirtual = GlbModuleLoadAddr;
	PeInfo->LoadedLibraries = ListCreate(KeyInteger, LIST_NORMAL);
	PeInfo->References = 1;

	/* Step 1. Relocate Sections */
	GlbModuleLoadAddr = PeRelocateSections(PeInfo, Buffer, 
		SectionAddr, BaseHeader->NumSections, 0);

	/* Step 2. Fix Relocations */
	PeFixRelocations(PeInfo, &DirectoryPtr[PE_SECTION_BASE_RELOCATION], ImageBase);

	/* Step 2. Enumerate Exports */
	PeEnumerateExports(PeInfo, &DirectoryPtr[PE_SECTION_EXPORT]);

	/* Step 3. Load Imports */
	PeLoadModuleImports(PeInfo, &DirectoryPtr[PE_SECTION_IMPORT]);

	/* Step 4. Proxy ModuleInit to Entry Point */
	if (PeInfo->ExportedFunctions != NULL)
	{
		/* Look for ModuleInit */
		foreach(fNode, PeInfo->ExportedFunctions)
		{
			/* Cast */
			MCorePeExportFunction_t *ExFunc =
				(MCorePeExportFunction_t*)fNode->Data;

			/* Is it ? */
			if (!strcmp(ExFunc->Name, "ModuleInit"))
			{
				PeInfo->EntryAddr = ExFunc->Address;
				break;
			}
		}
	}

	/* Done */
	return PeInfo;
}

/* Resolve Library Dependency */
MCorePeFile_t *PeResolveLibrary(MCorePeFile_t *Parent, MCorePeFile_t *PeFile, MString_t *LibraryName, Addr_t *NextLoadAddress)
{
	/* Result */
	MCorePeFile_t *ExportParent = Parent;
	MCorePeFile_t *Exports = NULL;

	/* Sanity */
	if (ExportParent == NULL)
		ExportParent = PeFile;

	/* Find File */
	foreach(lNode, ExportParent->LoadedLibraries)
	{
		/* Cast */
		MCorePeFile_t *Library = (MCorePeFile_t*)lNode->Data;

		/* Did we find it ? */
		if (MStringCompare(Library->Name, LibraryName, 1) == MSTRING_FULL_MATCH)
		{
			/* Increase Ref Count */
			Library->References++;

			/* Yay */
			Exports = Library;

			/* Done */
			break;
		}
	}

	/* Sanity */
	if (Exports == NULL)
	{
		/* Resolve Library */
		MCoreFileInstance_t *lFile = VfsWrapperOpen(LibraryName->Data, Read);
		uint8_t *fBuffer = (uint8_t*)kmalloc((size_t)lFile->File->Size);
		MCorePeFile_t *Library = NULL;

		/* Sanity */
		if (lFile->Code != VfsOk) {
			LogDebug("PELD", "Failed to load library %s", LibraryName->Data);
		}

		/* Read all data */
		VfsWrapperRead(lFile, fBuffer, (size_t)lFile->File->Size);

		/* Cleanup */
		VfsWrapperClose(lFile);

		/* Load */
		Library = PeLoadImage(ExportParent, LibraryName, fBuffer, NextLoadAddress);

		/* Cleanup Buffer */
		kfree(fBuffer);

		/* Import */
		Exports = Library;

		/* Add library to loaded libs */
		if (Exports != NULL) {
			DataKey_t Key;
			Key.Value = 0;
			ListAppend(ExportParent->LoadedLibraries, ListCreateNode(Key, Key, Library));
		}
	}

	/* Sanity Again */
	if (Exports == NULL)
		LogFatal("PELD", "Library %s was unable to be resolved", LibraryName->Data);

	/* Done */
	return Exports;
}

/* Resolve Function in Library */
Addr_t PeResolveFunctionAddress(MCorePeFile_t *Library, const char *Function)
{
	/* Get exports */
	List_t *Exports = Library->ExportedFunctions;

	/* Find File */
	foreach(lNode, Exports)
	{
		/* Cast */
		MCorePeExportFunction_t *exFunc = (MCorePeExportFunction_t*)lNode->Data;

		/* Did we find it ? */
		if (!strcmp(exFunc->Name, Function)) {
			return exFunc->Address;
		}
	}

	/* Damn.. */
	return 0;
}

/* Load Imports for an Image */
void PeLoadImageImports(MCorePeFile_t *Parent, MCorePeFile_t *PeFile, PeDataDirectory_t *ImportDirectory, Addr_t *NextImageBase)
{
	/* Vars */
	PeImportDescriptor_t *ImportDescriptor = NULL;

	/* Sanity */
	if (ImportDirectory->AddressRVA == 0
		|| ImportDirectory->Size == 0)
		return;

	/* Cast */
	ImportDescriptor = (PeImportDescriptor_t*)(PeFile->BaseVirtual + ImportDirectory->AddressRVA);

	/* Iterate untill
	* we hit the null-descriptor */
	while (ImportDescriptor->ImportAddressTable != 0)
	{
		/* Get name of module */
		MCorePeFile_t *ResolvedLib = NULL;
		List_t *Exports = NULL;
		char *NamePtr = (char*)(PeFile->BaseVirtual + ImportDescriptor->ModuleName);

		/* Convert -> Don't release this */
		MString_t *Name = MStringCreate(NamePtr, StrUTF8);
		
		/* Resolve Library */
		ResolvedLib = PeResolveLibrary(Parent, PeFile, Name, NextImageBase);

		if (ResolvedLib == NULL
			|| ResolvedLib->ExportedFunctions == NULL)
			return;

		/* Set list */
		Exports = ResolvedLib->ExportedFunctions;

		/* Calculate address to IAT
		* These entries are 64 bit in PE32+
		* and 32 bit in PE32 */
		if (PeFile->Architecture == PE_ARCHITECTURE_32)
		{
			uint32_t *Iat = (uint32_t*)(PeFile->BaseVirtual + ImportDescriptor->ImportAddressTable);

			/* Iterate Import table for this module */
			while (*Iat)
			{
				/* Get value */
				uint32_t Value = *Iat;

				/* We store the bound func */
				MCorePeExportFunction_t *Func = NULL;
				char *FuncName = NULL;

				/* Is it an ordinal or a function name? */
				if (Value & PE_IMPORT_ORDINAL_32)
				{
					/* Yes, ordinal */
					DataKey_t oKey;
					oKey.Value = (int)(Value & 0xFFFF);

					/* Locate Ordinal in loaded image */
					Func = (MCorePeExportFunction_t*)ListGetDataByKey(Exports, oKey, 0);
				}
				else
				{
					/* Nah, pointer to function name, where two first bytes are hint? */
					FuncName = (char*)(PeFile->BaseVirtual + (Value & PE_IMPORT_NAMEMASK) + 2);

					/* A little bit more tricky */
					foreach(FuncNode, Exports)
					{
						/* Cast */
						MCorePeExportFunction_t *pFunc =
							(MCorePeExportFunction_t*)FuncNode->Data;

						/* Compare */
						if (!strcmp(pFunc->Name, FuncName))
						{
							/* Found it */
							Func = pFunc;
							break;
						}
					}
				}

				/* Sanity */
				if (Func == NULL)
				{
					LogFatal("PELD", "Failed to locate function (%s)", FuncName);
					return;
				}

				/* Now, overwrite the IAT Entry with the actual address */
				*Iat = Func->Address;

				/* Go to next */
				Iat++;
			}
		}
		else
		{
			uint64_t *Iat = (uint64_t*)(PeFile->BaseVirtual + ImportDescriptor->ImportAddressTable);

			/* Iterate Import table for this module */
			while (*Iat)
			{
				/* Get value */
				uint64_t Value = *Iat;

				/* We store the bound func */
				MCorePeExportFunction_t *Func = NULL;

				/* Is it an ordinal or a function name? */
				if (Value & PE_IMPORT_ORDINAL_64)
				{
					/* Yes, ordinal */
					DataKey_t oKey;
					oKey.Value = (int)(Value & 0xFFFF);

					/* Locate Ordinal in loaded image */
					Func = (MCorePeExportFunction_t*)ListGetDataByKey(Exports, oKey, 0);
				}
				else
				{
					/* Nah, pointer to function name, where two first bytes are hint? */
					char *FuncName = (char*)(PeFile->BaseVirtual + (uint32_t)(Value & PE_IMPORT_NAMEMASK) + 2);

					/* A little bit more tricky */
					foreach(FuncNode, Exports)
					{
						/* Cast */
						MCorePeExportFunction_t *pFunc =
							(MCorePeExportFunction_t*)FuncNode->Data;

						/* Compare */
						if (!strcmp(pFunc->Name, FuncName))
						{
							/* Found it */
							Func = pFunc;
							break;
						}
					}
				}

				/* Sanity */
				if (Func == NULL)
				{
					LogFatal("PELD", "Failed to locate function");
					return;
				}

				/* Now, overwrite the IAT Entry with the actual address */
				*Iat = (uint64_t)Func->Address;

				/* Go to next */
				Iat++;
			}
		}

		/* Next ! */
		ImportDescriptor++;
	}
}

/* Load Executable into memory */
MCorePeFile_t *PeLoadImage(MCorePeFile_t *Parent, MString_t *Name, uint8_t *Buffer, Addr_t *BaseAddress)
{
	/* Headers */
	MzHeader_t *DosHeader = NULL;
	PeHeader_t *BaseHeader = NULL;
	PeOptionalHeader_t *OptHeader = NULL;

	/* Depends on Arch */
	PeOptionalHeader32_t *OptHeader32 = NULL;
	PeOptionalHeader64_t *OptHeader64 = NULL;

	/* Vars */
	Addr_t SectionAddr = 0;
	Addr_t ImageBase = 0;
	PeDataDirectory_t *DirectoryPtr = NULL;
	MCorePeFile_t *PeInfo = NULL;

	/* Validate */
	if (!PeValidate(Buffer))
		return NULL;

	/* Let's see */
	DosHeader = (MzHeader_t*)Buffer;

	/* Get Pe Header */
	BaseHeader = (PeHeader_t*)(Buffer + DosHeader->PeAddr);

	/* Load Optional Header */
	OptHeader = (PeOptionalHeader_t*)(Buffer + DosHeader->PeAddr + sizeof(PeHeader_t));

	/* We need to re-cast based on architecture */
	if (OptHeader->Architecture == PE_ARCHITECTURE_32)
	{
		/* This is an 32 bit */
		OptHeader32 = (PeOptionalHeader32_t*)(Buffer + DosHeader->PeAddr + sizeof(PeHeader_t));
		ImageBase = OptHeader32->BaseAddress;

		/* Calc address of first section */
		SectionAddr = (Addr_t)(Buffer + DosHeader->PeAddr + sizeof(PeHeader_t) + sizeof(PeOptionalHeader32_t));

		/* Set directory pointer */
		DirectoryPtr = (PeDataDirectory_t*)&OptHeader32->Directories[0];
	}
	else if (OptHeader->Architecture == PE_ARCHITECTURE_64)
	{
		/* 64 Bit! */
		OptHeader64 = (PeOptionalHeader64_t*)(Buffer + DosHeader->PeAddr + sizeof(PeHeader_t));
		ImageBase = (Addr_t)OptHeader64->BaseAddress;

		/* Calc address of first section */
		SectionAddr = (Addr_t)(Buffer + DosHeader->PeAddr + sizeof(PeHeader_t) + sizeof(PeOptionalHeader64_t));

		/* Set directory pointer */
		DirectoryPtr = (PeDataDirectory_t*)&OptHeader64->Directories[0];
	}

	/* Allocate data */
	PeInfo = (MCorePeFile_t*)kmalloc(sizeof(MCorePeFile_t));
	memset(PeInfo, 0, sizeof(MCorePeFile_t));

	/* Set base information */
	PeInfo->Name = Name;
	PeInfo->Architecture = OptHeader->Architecture;
	PeInfo->BaseVirtual = *BaseAddress;
	PeInfo->LoadedLibraries = ListCreate(KeyInteger, LIST_NORMAL);
	PeInfo->References = 1;

	/* Set Entry Point */
	if (OptHeader->EntryPoint != 0)
		PeInfo->EntryAddr = PeInfo->BaseVirtual + OptHeader->EntryPoint;
	else
		PeInfo->EntryAddr = 0;

	/* Step 1. Relocate Sections */
	*BaseAddress = PeRelocateSections(PeInfo, Buffer, 
		SectionAddr, BaseHeader->NumSections, 1);

	/* Step 2. Fix Relocations */
	PeFixRelocations(PeInfo, &DirectoryPtr[PE_SECTION_BASE_RELOCATION], ImageBase);

	/* Step 2. Enumerate Exports */
	PeEnumerateExports(PeInfo, &DirectoryPtr[PE_SECTION_EXPORT]);

	/* Before loading imports, add us to parent list of libraries 
	 * so we might be reused, instead of reloaded */
	if (Parent != NULL) {
		DataKey_t Key;
		Key.Value = 0;
		ListAppend(Parent->LoadedLibraries, ListCreateNode(Key, Key, PeInfo));
	}

	/* Step 3. Load Imports */
	PeLoadImageImports(Parent, PeInfo, &DirectoryPtr[PE_SECTION_IMPORT], BaseAddress);

	/* Step 4. Call entry points of loaded libraries (!?!?!?!?) */

	/* Done */
	return PeInfo;
}

/* Unload dynamically loaded library 
 * This only cleans up in the case there are no 
 * more references */
void PeUnloadLibrary(MCorePeFile_t *Parent, MCorePeFile_t *Library)
{
	/* Decrease reference count */
	Library->References--;

	/* Sanity */
	if (Library->References <= 0) 
	{
		/* Remove it from list */
		foreach(lNode, Parent->LoadedLibraries)
		{
			/* Cast */
			MCorePeFile_t *lLib = (MCorePeFile_t*)lNode->Data;

			/* Did we find it ? */
			if (lLib == Library)
			{
				/* Remove */
				ListRemoveByNode(Parent->LoadedLibraries, lNode);

				/* Free node */
				kfree(lNode);

				/* Done */
				break;
			}
		}

		/* Unload it */
		PeUnload(Library);
	}
}

/* Unload executables, all it's dependancies and free it's resources */
void PeUnload(MCorePeFile_t *Executable)
{
	/* Vars */
	ListNode_t *Node;

	/* Sanity */
	if (Executable == NULL)
		return;

	/* Free Strings */
	kfree(Executable->Name);

	/* Cleanup exported functions */
	if (Executable->ExportedFunctions != NULL)
	{
		/* Iterate */
		_foreach(Node, Executable->ExportedFunctions)
		{
			/* Cast data */
			MCorePeExportFunction_t *ExFunc = (MCorePeExportFunction_t*)Node->Data;

			/* Free struct */
			kfree(ExFunc);
		}

		/* Destroy list */
		ListDestroy(Executable->ExportedFunctions);
	}

	/* Cleanup libraries */
	if (Executable->LoadedLibraries != NULL)
	{
		/* Iterate */
		_foreach(Node, Executable->LoadedLibraries)
		{
			/* Cast data */
			MCorePeFile_t *Library = (MCorePeFile_t*)Node->Data;

			/* Cleanup that aswell */
			PeUnload(Library);
		}

		/* Destroy list */
		ListDestroy(Executable->LoadedLibraries);
	}

	/* Free structure */
	kfree(Executable);
}
