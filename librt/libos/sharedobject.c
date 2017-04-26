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
 * MollenOS MCore - Shared Object Support Definitions & Structures
 * - This header describes the base sharedobject-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

/* Includes 
 * - System */
#include <os/sharedobject.h>
#include <os/syscall.h>

/* Includes
 * - Library */
#include <stddef.h>

/* SharedObjectLoad
 * Load a shared object given a path
 * path must exists otherwise NULL is returned */
Handle_t 
SharedObjectLoad(
	_In_ __CONST char *SharedObject)
{
	// Sanitize parameters
	if (SharedObject == NULL) {
		return HANDLE_INVALID;
	}

	// Just deep call, we have 
	// all neccessary functionlity and validation already in place
	return (Handle_t*)Syscall1(SYSCALL_LOADSO, SYSCALL_PARAM(SharedObject));
}

/* SharedObjectGetFunction
 * Load a function-address given an shared object
 * handle and a function name, function must exist
 * otherwise null is returned */
void *
SharedObjectGetFunction(
	_In_ Handle_t Handle, 
	_In_ __CONST char *Function)
{
	/* Sanitize the arguments */
	if (Handle == HANDLE_INVALID
		|| Function == NULL) {
		return NULL;
	}

	/* Just deep call, we have
	* all neccessary functionlity and validation already in place */
	return (void*)Syscall2(SYSCALL_LOADPROC, 
		SYSCALL_PARAM(Handle), SYSCALL_PARAM(Function));
}

/* SharedObjectUnload
 * Unloads a valid shared object handle
 * returns OsError on failure */
OsStatus_t 
SharedObjectUnload(
	_In_ Handle_t Handle)
{
	/* Sanitize the handle */
	if (Handle == HANDLE_INVALID) {
		return OsError;
	}

	/* Just deep call, we have
	* all neccessary functionlity and validation already in place */
	return (OsStatus_t)Syscall1(SYSCALL_UNLOADSO, SYSCALL_PARAM(Handle));
}
