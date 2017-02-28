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
 * MollenOS MCore - Shared Objects Definitions & Structures
 * - This header describes the shared object structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _SHARED_OBJECTS_INTERFACE_H_
#define _SHARED_OBJECTS_INTERFACE_H_

/* Includes
 * - Library */
#include <os/osdefs.h>

 /* Start one of these before function prototypes */
_CODE_BEGIN

/* SharedObjectLoad
 * Load a shared object given a path
 * path must exists otherwise NULL is returned */
MOSAPI 
Handle_t 
SharedObjectLoad(
	_In_ __CONST char *SharedObject);

/* SharedObjectGetFunction
 * Load a function-address given an shared object
 * handle and a function name, function must exist
 * otherwise null is returned */
MOSAPI 
void *
SharedObjectGetFunction(
	_In_ Handle_t Handle, 
	_In_ __CONST char *Function);

/* SharedObjectUnload
 * Unloads a valid shared object handle
 * returns OsError on failure */
MOSAPI 
OsStatus_t 
SharedObjectUnload(
	_In_ Handle_t Handle);

_CODE_END

#endif //!_SHARED_OBJECTS_INTERFACE_H_
