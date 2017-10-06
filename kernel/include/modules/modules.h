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
 * MollenOS MCore - MollenOS Modules
 */
#ifndef _MCORE_MODULES_H_
#define _MCORE_MODULES_H_

/* Includes 
 * - Systems */
#include <mollenos.h>
#include <modules/ramdisk.h>

/* Includes
 * - C-Library */
#include <os/osdefs.h>
#include <ds/mstring.h>

/* Definitions 
 * These are some software types for modules
 * and not hardware modules, so we specify them
 * as hardcoded values and they must be conformed to by drivers */
#define MODULE_FILESYSTEM	0x01010101
#define MODULE_BUS			0x02020202

/* The module definition, it currently just
 * consists of the a ramdisk entry header and
 * a name/path */
typedef struct _MCoreModule {
	MString_t *Name;
	MCoreRamDiskModuleHeader_t *Header;
} MCoreModule_t;

/* ModulesInitialize
 * Loads the ramdisk, iterates all headers and 
 * builds a list of both available servers and 
 * available drivers */
__EXTERN
OsStatus_t
ModulesInitialize(
    _In_ MCoreBootDescriptor *BootDescriptor);

/* ModulesRunServers
 * Loads all iterated servers in the supplied ramdisk
 * by spawning each one as a new process */
__EXTERN
void
ModulesRunServers(void);

/* ModulesQueryPath
 * Retrieve a pointer to the file-buffer and its length 
 * based on the given <rd:/> path */
__EXTERN
OsStatus_t
ModulesQueryPath(
    _In_ MString_t *Path, 
    _Out_ void **Buffer, 
    _Out_ size_t *Length);

/* ModulesFindGeneric
 * Resolve a 'generic' driver by its device-type and/or
 * its device sub-type, this is generally used if there is no
 * vendor specific driver available for the device. Returns NULL
 * if none is available */
__EXTERN
MCoreModule_t*
ModulesFindGeneric(
    _In_ DevInfo_t DeviceType, 
    _In_ DevInfo_t DeviceSubType);

/* ModulesFindSpecific
 * Resolve a specific driver by its vendorid and deviceid 
 * this is to ensure optimal module load. Returns NULL 
 * if none is available */
__EXTERN
MCoreModule_t*
ModulesFindSpecific(
    _In_ DevInfo_t VendorId, 
    _In_ DevInfo_t DeviceId);

/* ModulesFindString
 * Resolve a module by its name. Returns NULL if none
 * is available */
__EXTERN
MCoreModule_t*
ModulesFindString(
    _In_ MString_t *Module);

#endif //!_MCORE_MODULES_H_
