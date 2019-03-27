/* MollenOS
 *
 * Copyright 2019, Philip Meulengracht
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
 * DLL Service Definitions & Structures
 * - This header describes the base library-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __SERVICES_SHAREDOBJECT_H__
#define __SERVICES_SHAREDOBJECT_H__

#include <os/osdefs.h>

_CODE_BEGIN
/* SharedObjectLoad
 * Load a shared object given a path which must exists otherwise NULL is returned */
CRTDECL(Handle_t,
SharedObjectLoad(
	_In_ const char* SharedObject));

/* SharedObjectGetFunction
 * Load a function-address given an shared object
 * handle and a function name, function must exist
 * otherwise null is returned */
CRTDECL(void*,
SharedObjectGetFunction(
	_In_ Handle_t    Handle, 
	_In_ const char* Function));

/* SharedObjectUnload
 * Unloads a valid shared object handle
 * returns OsError on failure */
CRTDECL(OsStatus_t,
SharedObjectUnload(
	_In_ Handle_t Handle));
_CODE_END

#endif //!__SERVICES_SHAREDOBJECT_H__
