/* MollenOS
*
* Copyright 2011 - 2014, Philip Meulengracht
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
#include <Modules\PeLoader.h>
#include <Log.h>
#include <Heap.h>

#include <stddef.h>
#include <string.h>

/* Keep track of where to load 
 * the next module */
Addr_t GlbModuleLoadAddr = MEMORY_LOCATION_MODULES;

/* Relocate Sections */
Addr_t PeRelocateSections(MCorePeFile_t *PeFile, uint8_t *Data, 
	Addr_t SectionAddr, uint16_t NumSections)
{
	/* Cast to a pointer */
	PeSectionHeader_t *Section = (PeSectionHeader_t*)SectionAddr;
	Addr_t MemAddr = PeFile->BaseVirtual;
	int32_t i;

	/* Iterate */
	for (i = 0; i < (int32_t)NumSections; i++)
	{
		/* Calculate pointers */
		uint8_t *FileBuffer = (uint8_t*)(Data + Section->RawAddr);
		uint8_t *MemBuffer = (uint8_t*)(PeFile->BaseVirtual + Section->VirtualAddr);

		/* Which kind of section is this */
		if (Section->RawSize == 0
			|| Section->Flags & PE_SECTION_BSS)
		{
			/* We should zero this */
			memset(MemBuffer, 0, Section->VirtualSize);
		}
		else if (Section->Flags & PE_SECTION_CODE
			|| Section->Flags & PE_SECTION_DATA)
		{
			/* Copy section */
			memcpy(MemBuffer, FileBuffer, Section->RawSize);

			/* Sanity */
			if (Section->VirtualSize > Section->RawSize)
				memset((MemBuffer + Section->RawSize), 0, 
				(Section->VirtualSize - Section->RawSize));
		}

		/* Increase Pointers */
		MemAddr = (PeFile->BaseVirtual + Section->VirtualAddr + Section->VirtualSize);

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
		uint32_t PageRVA = *RelocPtr;
		uint32_t BlockSize = 0;
		uint32_t NumRelocs = 0;
		RelocPtr++;

		/* Get block size */
		BlockSize = *RelocPtr;
		RelocPtr++;

		/* Decrease */
		BytesLeft -= BlockSize;

		/* Now, entries come */
		NumRelocs = (BlockSize - 8) / sizeof(uint16_t);

		/* Create a pointer */
		rEntry = (uint16_t*)RelocPtr;

		/* Iterate */
		for (Itr = 0; Itr < NumRelocs; Itr++)
		{
			/* Create a delta based on the relocation type */
			uint16_t RelocEntry = *rEntry;
			uint16_t Type = (RelocEntry >> 12);
			uint16_t Value = RelocEntry & 0x0FFF;

			/* Check Type */
			if (Type == PE_RELOCATION_ALIGN)
				continue;

			/* 32 Bit Difference */
			if (Type == PE_RELOCATION_HIGHLOW)
			{
				/* Create a pointer, the low 12 bits have 
				 * an offset into the PageRVA */
				Addr_t Offset = (PeFile->BaseVirtual + PageRVA + Value);

				/* Calculate Delta */
				SAddr_t Delta = (SAddr_t)(PeFile->BaseVirtual - ImageBase);

				/* Update */
				*((SAddr_t*)Offset) += Delta;
			}
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
	PeFile->ExportedFunctions = list_create(LIST_NORMAL);

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
		list_append(PeFile->ExportedFunctions, list_create_node(ExFunc->Ordinal, ExFunc));
	}
}

/* Load Imports */
void PeLoadImports(MCorePeFile_t *PeFile, PeDataDirectory_t *ImportDirectory)
{
	/* Vars */
	PeImportDirectory_t *ImportTable = NULL;

	/* Sanity */
	if (ImportDirectory->AddressRVA == 0
		|| ImportDirectory->Size == 0)
		return;

	/* Cast */
	ImportTable = (PeImportDirectory_t*)(PeFile->BaseVirtual + ImportDirectory->AddressRVA);


}

/* Load Module into memory */
MCorePeFile_t *PeLoadModule(uint8_t *Buffer)
{
	/* Headers */
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

	/* Let's see */
	BaseHeader = (PeHeader_t*)Buffer;

	/* Validate */
	if (BaseHeader->Magic != PE_MAGIC)
	{
		LogFatal("PELD", "Invalid PE File Magic 0x%x", BaseHeader->Magic);
		return NULL;
	}

	/* Validate Machine */
	if (BaseHeader->Machine != PE_MACHINE_X32
		&& BaseHeader->Machine != PE_MACHINE_X64)
	{
		LogFatal("PELD", "Unsupported Machine 0x%x", BaseHeader->Machine);
		return NULL;
	}

	/* Load Optional Header */
	OptHeader = (PeOptionalHeader_t*)(Buffer + sizeof(PeHeader_t));

	/* We need to re-cast based on architecture */
	if (OptHeader->Architecture == PE_ARCHITECTURE_32)
	{
		/* This is an 32 bit */
		OptHeader32 = (PeOptionalHeader32_t*)(Buffer + sizeof(PeHeader_t));
		ImageBase = OptHeader32->BaseAddress;

		/* Calc address of first section */
		SectionAddr = (Addr_t)(Buffer + sizeof(PeHeader_t) + sizeof(PeOptionalHeader64_t));

		/* Set directory pointer */
		DirectoryPtr = (PeDataDirectory_t*)&OptHeader32->Directories[0];
	}
	else if (OptHeader->Architecture == PE_ARCHITECTURE_64)
	{
		/* 64 Bit! */
		OptHeader64 = (PeOptionalHeader64_t*)(Buffer + sizeof(PeHeader_t));
		ImageBase = (Addr_t)OptHeader64->BaseAddress;

		/* Calc address of first section */
		SectionAddr = (Addr_t)(Buffer + sizeof(PeHeader_t) + sizeof(PeOptionalHeader64_t));

		/* Set directory pointer */
		DirectoryPtr = (PeDataDirectory_t*)&OptHeader64->Directories[0];
	}
	else
	{
		LogFatal("PELD", "Unsupported Architecture in Optional 0x%x", OptHeader->Architecture);
		return NULL;
	}

	/* Allocate data */
	PeInfo = (MCorePeFile_t*)kmalloc(sizeof(MCorePeFile_t));
	
	/* Set base information */
	PeInfo->BaseVirtual = GlbModuleLoadAddr;
	PeInfo->EntryAddr = GlbModuleLoadAddr + OptHeader->EntryPoint;

	/* Step 1. Relocate Sections */
	GlbModuleLoadAddr = 
		PeRelocateSections(PeInfo, Buffer, SectionAddr, BaseHeader->NumSections);

	/* Step 2. Fix Relocations */
	PeFixRelocations(PeInfo, &DirectoryPtr[PE_SECTION_BASE_RELOCATION], ImageBase);

	/* Step 2. Enumerate Exports */
	PeEnumerateExports(PeInfo, &DirectoryPtr[PE_SECTION_EXPORT]);

	/* Step 3. Load Imports */
	PeLoadImports(PeInfo, &DirectoryPtr[PE_SECTION_IMPORT]);

	/* Done */
	return PeInfo;
}