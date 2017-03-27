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
 * MollenOS MCore - MollenOS Module Manager
 */

/* Includes 
 * - System */
#include <process/phoenix.h>
#include <modules/modules.h>
#include <interrupts.h>
#include <heap.h>
#include <log.h>

/* Includes
 * - C-Library */
#include <stddef.h>
#include <ds/list.h>

/* Private definitions that
 * are local to this file */
#define LIST_MODULE			1
#define LIST_SERVER			2

/* Globals */
List_t *GlbModules = NULL;
int GlbModulesInitialized = 0;

/* ModulesInit
 * Loads the ramdisk, iterates all headers and 
 * builds a list of both available servers and 
 * available drivers */
OsStatus_t ModulesInit(MCoreBootDescriptor *BootDescriptor)
{
	/* Variables needed for init of 
	 * ramdisk and iteration */
	MCoreRamDiskHeader_t *Ramdisk = NULL;
	MCoreRamDiskEntry_t *Entry = NULL;
	int Counter = 0;

	/* Sanitize the boot-parameters 
	 * We will consider the possiblity of
	 * 0 values to be there is no ramdisk */
	if (BootDescriptor == NULL
		|| BootDescriptor->RamDiskAddress == 0
		|| BootDescriptor->RamDiskSize == 0) {
		LogDebug("MODS", "No ramdisk detected");
		return OsNoError;
	}
	else {
		LogInformation("MODS", "Loading and parsing ramdisk");
	}

	/* Initialize the address to the pointer */
	Ramdisk = (MCoreRamDiskHeader_t*)BootDescriptor->RamDiskAddress;

	/* Validate the ramdisk magic constant */
	if (Ramdisk->Magic != RAMDISK_MAGIC) {
		LogFatal("MODS", "Invalid magic in ramdisk - 0x%x", Ramdisk->Magic);
		return OsError;
	}

	/* Validate the ramdisk version, we have to
	 * support the version */
	if (Ramdisk->Version != RAMDISK_VERSION_1) {
		LogFatal("MODS", "Invalid ramdisk version - 0x%x", Ramdisk->Version);
		return OsError;
	}

	/* Initialize the list of modules 
	 * and servers so we can add later :-) */
	GlbModules = ListCreate(KeyInteger, LIST_NORMAL);

	/* Store filecount so we can iterate */
	Entry = (MCoreRamDiskEntry_t*)
		(BootDescriptor->RamDiskAddress + sizeof(MCoreRamDiskHeader_t));
	Counter = Ramdisk->FileCount;

	/* Keep iterating untill we reach
	 * the end of counter */
	while (Counter != 0) {
		if (Entry->Type == RAMDISK_MODULE
				|| Entry->Type == RAMDISK_FILE) {
			MCoreRamDiskModuleHeader_t *Header =
				(MCoreRamDiskModuleHeader_t*)(BootDescriptor->RamDiskAddress + Entry->DataOffset); 
			MCoreModule_t *Module = NULL;
			DataKey_t Key;

			/* Allocate a new module header 
			 * And copy some values */
			Module = (MCoreModule_t*)kmalloc(sizeof(MCoreModule_t));
			Module->Name = MStringCreate(Header->ModuleName, StrUTF8);
			Module->Header = Header;

			/* Update key based on the type of module
			 * either its a server or a driver */
			if (Header->Flags & RAMDISK_MODULE_SERVER) {
				Key.Value = LIST_SERVER;
			}
			else {
				Key.Value = LIST_MODULE;
			}

			/* Add to list */
			ListAppend(GlbModules, ListCreateNode(Key, Key, Module));
		}

		/* Next! */
		Entry++;
		Counter--;
	}

	/* Debug information */
	LogInformation("MODS", "Found %i Modules and Servers", 
		ListLength(GlbModules));

	/* Mark as initialized */
	GlbModulesInitialized = 1;

	/* Return success */
	return OsNoError;
}

/* ModulesRunServers
 * Loads all iterated servers in the supplied ramdisk
 * by spawning each one as a new process */
void ModulesRunServers(void)
{
	// Variables
	IntStatus_t IrqState = 0;

	// Sanitize init status
	if (GlbModulesInitialized != 1) {
		return;
	}

	// Disable interrupts while doing this as
	// we are still the idle thread -> as soon as a new
	// work is spawned we hardly ever return to this
	IrqState = InterruptDisable();

	// Iterate module list and spawn all servers
	// then they will "run" the system for us
	foreach(sNode, GlbModules) {
		if (sNode->Key.Value == LIST_SERVER) {
			MCorePhoenixRequest_t *Request = NULL;
			MString_t *Path = MStringCreate("rd:/", StrUTF8);
			MStringAppendString(Path, ((MCoreModule_t*)sNode->Data)->Name);

			// Allocate a new request, let it cleanup itself
			Request = (MCorePhoenixRequest_t*)kmalloc(sizeof(MCorePhoenixRequest_t));
			Request->Base.Type = AshSpawnServer;
			Request->Base.Cleanup = 1;

			// Set parameters
			Request->Path = Path;
			Request->Arguments.Raw.Data = NULL;
			Request->Arguments.Raw.Length = 0;

			// Send off async requests
			PhoenixCreateRequest(Request);
		}
	}

	// Restore interrupt state
	InterruptRestoreState(IrqState);
}

/* ModulesQueryPath
 * Retrieve a pointer to the file-buffer and its length 
 * based on the given <rd:/> path */
OsStatus_t ModulesQueryPath(MString_t *Path, void **Buffer, size_t *Length)
{
	/* Build the token we are searcing for */
	MString_t *Token = MStringSubString(Path, 
		MStringFindReverse(Path, '/') + 1, -1);
	OsStatus_t Result = OsError;

	/* Sanitizie initialization status */
	if (GlbModulesInitialized != 1) {
		goto Exit;
	}

	/* Iterate and compare */
	foreach(sNode, GlbModules) {
		MCoreModule_t *Mod = (MCoreModule_t*)sNode->Data;
		if (MStringCompare(Token, Mod->Name, 1) != MSTRING_NO_MATCH) {
			*Buffer = (void*)(
				(uintptr_t)Mod->Header + sizeof(MCoreRamDiskModuleHeader_t));
			*Length = Mod->Header->Length;
			Result = OsNoError;
			break;
		}
	}
	
Exit:
	/* Cleanup the token we created
	 * and return the status */
	MStringDestroy(Token);
	return Result;
}

/* ModulesFindGeneric
 * Resolve a 'generic' driver by its device-type and/or
 * its device sub-type, this is generally used if there is no
 * vendor specific driver available for the device. Returns NULL
 * if none is available */
MCoreModule_t *ModulesFindGeneric(DevInfo_t DeviceType, DevInfo_t DeviceSubType)
{
	/* Sanitizie initialization status */
	if (GlbModulesInitialized != 1) {
		return NULL;
	}

	/* Iterate the list of modules */
	foreach(sNode, GlbModules) {
		MCoreModule_t *Mod = (MCoreModule_t*)sNode->Data;
		if (Mod->Header->DeviceType == DeviceType
			&& Mod->Header->DeviceSubType == DeviceSubType) {
			return Mod;
		}
	}

	/* Dayum, return NULL upon no
	 * results of the iteration */
	return NULL;
}

/* ModulesFindSpecific
 * Resolve a specific driver by its vendorid and deviceid 
 * this is to ensure optimal module load. Returns NULL 
 * if none is available */
MCoreModule_t *ModulesFindSpecific(DevInfo_t VendorId, DevInfo_t DeviceId)
{
	/* Sanitizie initialization status */
	if (GlbModulesInitialized != 1) {
		return NULL;
	}

	/* Iterate the list of modules */
	foreach(sNode, GlbModules) {
		MCoreModule_t *Mod = (MCoreModule_t*)sNode->Data;
		if (Mod->Header->VendorId == VendorId
			&& Mod->Header->DeviceId == DeviceId) {
			return Mod;
		}
	}

	/* Dayum, return NULL upon no
	 * results of the iteration */
	return NULL;
}

/* ModulesFindString
 * Resolve a module by its name. Returns NULL if none
 * is available */
MCoreModule_t *ModulesFindString(MString_t *Module)
{
	/* Sanitizie initialization status */
	if (GlbModulesInitialized != 1) {
		return NULL;
	}

	/* Iterate the list of modules */
	foreach(sNode, GlbModules) {
		MCoreModule_t *Mod = (MCoreModule_t*)sNode->Data;
		if (MStringCompare(Module, Mod->Name, 1) == MSTRING_FULL_MATCH) {
			return Mod;
		}
	}

	/* Dayum, return NULL upon no
	 * results of the iteration */
	return NULL;
}
