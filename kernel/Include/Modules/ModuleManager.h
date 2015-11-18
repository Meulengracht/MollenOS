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
#include <Arch.h>
#include <crtdefs.h>
#include <stdint.h>

/* Definitions */


/* Enums */
typedef enum _ModuleResult
{
	ModuleOk,
	ModuleFailed
} ModuleResult_t;

/* Structures */
typedef struct _MCoreModule
{
	/* Module Name
	* Also UTF-8 */
	uint8_t ModuleName[64];

	/* Device Type */
	uint32_t DeviceType;

	/* Device SubType */
	uint32_t DeviceSubType;

	/* Module Length */
	uint32_t Length;

} MCoreModule_t;


/* Prototypes */
_CRT_EXTERN void ModuleMgrInit(size_t RamDiskAddr, size_t RamDiskSize);

_CRT_EXTERN MCoreModule_t *ModuleFind(uint32_t DeviceType, uint32_t DeviceSubType);
_CRT_EXTERN ModuleResult_t ModuleLoad(MCoreModule_t *Module, Addr_t *FunctionTable, void *Args);
_CRT_EXTERN void ModuleUnload(MCoreModule_t *Module);

#endif //!_MODULE_MANAGER_H_