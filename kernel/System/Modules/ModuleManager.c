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
* MollenOS MCore - MollenOS Module Manager
*/

/* Includes */
#include <Modules/ModuleManager.h>
#include <Heap.h>
#include <Log.h>

/* C-Library */
#include <stddef.h>
#include <ds/list.h>

/* Types */
typedef void(*ModuleEntryFunc)(void *Data);

/* Globals */
uint32_t GlbModMgrInitialized = 0;
List_t *GlbModMgrModules = NULL;

/* Loads the RD */
void ModuleMgrInit(MCoreBootDescriptor *BootDescriptor)
{
	/* Get pointers */
	MCoreRamDiskHeader_t *RdHeader = (MCoreRamDiskHeader_t*)BootDescriptor->RamDiskAddress;

	/* Sanity */
	if (BootDescriptor->RamDiskAddress == 0
		|| BootDescriptor->RamDiskSize == 0)
		return;

	/* Info */
	LogInformation("MDMG", "Initializing");

	/* Parse Kernel Exports */
	PeLoadKernelExports(BootDescriptor->KernelAddress, BootDescriptor->ExportsAddress);

	/* Validate Members */
	if (RdHeader->Magic != RAMDISK_MAGIC)
	{
		/* Error! */
		LogFatal("MDMG", "Invalid Magic in Ramdisk - 0x%x", RdHeader->Magic);
		return;
	}

	/* Valid Version? */
	if (RdHeader->Version != RAMDISK_VERSION_1)
	{
		/* Error! */
		LogFatal("MDMG", "Invalid RamDisk Version - 0x%x", RdHeader->Version);
		return;
	}

	/* Allocate list */
	GlbModMgrModules = ListCreate(KeyInteger, LIST_NORMAL);

	/* Save Module-Count */
	uint32_t FileCount = RdHeader->FileCount;
	Addr_t RdPtr = BootDescriptor->RamDiskAddress + sizeof(MCoreRamDiskHeader_t);

	/* Point to first entry */
	MCoreRamDiskFileHeader_t *FilePtr = (MCoreRamDiskFileHeader_t*)RdPtr;

	/* Iterate */
	while (FileCount != 0)
	{
		/* We only care about modules */
		if (FilePtr->Type == RAMDISK_MODULE)
		{
			/* Get a pointer to the module header */
			MCoreRamDiskModuleHeader_t *ModuleHeader = 
				(MCoreRamDiskModuleHeader_t*)(BootDescriptor->RamDiskAddress + FilePtr->DataOffset);
			DataKey_t Key;

			/* Allocate a new module */
			MCoreModule_t *Module = (MCoreModule_t*)kmalloc(sizeof(MCoreModule_t));

			/* Set */
			Module->Name = MStringCreate(ModuleHeader->ModuleName, StrUTF8);
			Module->Header = ModuleHeader;
			Module->Descriptor = NULL;

			/* Add to list */
			Key.Value = 0;
			ListAppend(GlbModMgrModules, ListCreateNode(Key, Key, Module));
		}

		/* Next! */
		FilePtr++;
		FileCount--;
	}

	/* Info */
	LogInformation("MDMG", "Found %i Modules", GlbModMgrModules->Length);

	/* Done! */
	GlbModMgrInitialized = 1;
}

/* Locate a generic module */
MCoreModule_t *ModuleFindGeneric(uint32_t DeviceType, uint32_t DeviceSubType)
{
	foreach(mNode, GlbModMgrModules)
	{
		/* Cast */
		MCoreModule_t *Module = (MCoreModule_t*)mNode->Data;

		/* Sanity */
		if (Module->Header->DeviceType == DeviceType
			&& Module->Header->DeviceSubType == DeviceSubType)
			return Module;
	}

	/* Else return null, not found */
	return NULL;
}

/* Locate a specific module */
MCoreModule_t *ModuleFindSpecific(uint32_t VendorId, uint32_t DeviceId)
{
	foreach(mNode, GlbModMgrModules)
	{
		/* Cast */
		MCoreModule_t *Module = (MCoreModule_t*)mNode->Data;

		/* Sanity */
		if (Module->Header->VendorId == VendorId
			&& Module->Header->DeviceId == DeviceId)
			return Module;
	}

	/* Else return null, not found */
	return NULL;
}

/* Locate a module by string */
MCoreModule_t *ModuleFindStr(MString_t *Module)
{
	foreach(mNode, GlbModMgrModules)
	{
		/* Cast */
		MCoreModule_t *cModule = (MCoreModule_t*)mNode->Data;

		/* Sanity */
		if (MStringCompare(Module, cModule->Name, 0) == MSTRING_FULL_MATCH)
			return cModule;
	}

	/* Else return null, not found */
	return NULL;
}

/* Locates a module by address and returns the diff */
MCoreModule_t *ModuleFindAddress(Addr_t Address)
{
	/* Keep track */
	Addr_t BestMatch = 0xFFFFFFFF;
	MCoreModule_t *BestModule = NULL;

	foreach(mNode, GlbModMgrModules)
	{
		/* Cast */
		MCoreModule_t *Module = (MCoreModule_t*)mNode->Data;

		/* Sanity */
		if (Module->Descriptor != NULL &&
			Module->Descriptor->BaseVirtual <= Address)
		{
			/* Check delta */
			if (Address - Module->Descriptor->BaseVirtual
				< BestMatch)
			{
				BestModule = Module;
				BestMatch = Address - Module->Descriptor->BaseVirtual;
			}
		}
	}

	/* Else return null, not found */
	return BestModule;
}

/* Load a Module */
ModuleResult_t ModuleLoad(MCoreModule_t *Module, void *Args)
{
	/* Sanity */
	if (Module->Descriptor != NULL)
	{
		/* It is already loaded, 
		 * The question is whether or 
		 * not we should call constructor */
		if (Module->Header->Flags & RAMDISK_MODULE_SHARED)
			return ModuleOk;
		else
		{
			/* Information */
			LogInformation("MDMG", "Recycling Module %s", Module->Header->ModuleName);

			/* Call entry point */
			((ModuleEntryFunc)Module->Descriptor->EntryAddr)(Args);

			/* Done! */
			return ModuleOk;
		}
	}

	/* Information */
	LogInformation("MDMG", "Loading Module %s", Module->Header->ModuleName);

	/* Calculate the file data address */
	uint8_t *ModData = 
		(uint8_t*)((Addr_t)Module->Header + sizeof(MCoreRamDiskModuleHeader_t));

	/* Parse & Relocate PE Module */
	Module->Descriptor = PeLoadModule(&Module->Header->ModuleName[0], ModData, Module->Header->Length);

	/* Sanity */
	if (Module->Descriptor == NULL
		|| Module->Descriptor->EntryAddr == 0)
	{
		LogFatal("MDMG", "Failed to load module");
		return ModuleFailed;
	}

	/* Call entry point */
	((ModuleEntryFunc)Module->Descriptor->EntryAddr)(Args);

	/* Done! */
	return ModuleOk;
}