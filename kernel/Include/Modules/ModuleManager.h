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
* MollenOS MCore - MollenOS Module Manager
*/
#ifndef _MODULE_MANAGER_H_
#define _MODULE_MANAGER_H_

/* Includes */
#include <MollenOS.h>
#include <Arch.h>
#include <MString.h>
#include <crtdefs.h>
#include <stdint.h>

/* Subsystems */
#include <Modules/PeLoader.h>
#include <Modules/RamDisk.h>

/* Definitions */
#define MODULE_FILESYSTEM	0x01010101

/* Enums */
typedef enum _ModuleResult
{
	ModuleOk,
	ModuleFailed
} ModuleResult_t;

/* Structures */
typedef struct _MCoreModule
{
	/* Name */
	MString_t *Name;

	/* The Descriptor */
	MCoreRamDiskModuleHeader_t *Header;

	/* File Information */
	MCorePeFile_t *Descriptor;

} MCoreModule_t;

/* Prototypes */
_CRT_EXTERN void ModuleMgrInit(MCoreBootDescriptor *BootDescriptor);

_CRT_EXTERN MCoreModule_t *ModuleFind(uint32_t DeviceType, uint32_t DeviceSubType);
_CRT_EXTERN MCoreModule_t *ModuleFindStr(MString_t *Module);
_CRT_EXTERN ModuleResult_t ModuleLoad(MCoreModule_t *Module, void *Args);
_CRT_EXTERN void ModuleUnload(MCoreModule_t *Module);

/* Debugging */
_CRT_EXTERN MCoreModule_t *ModuleFindAddress(Addr_t Address);

#endif //!_MODULE_MANAGER_H_