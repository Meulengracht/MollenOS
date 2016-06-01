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
* MollenOS - Shared Object Interface
*/

/* Includes */
#include <os/MollenOS.h>
#include <os/Syscall.h>
#include <stddef.h>

#ifdef LIBC_KERNEL
void __SharedObjectLibCEmpty(void)
{
}
#else

/* Load a shared object given a path
 * path must exists otherwise NULL is returned */
void *SharedObjectLoad(const char *SharedObject)
{
	/* Sanitize the path */
	if (SharedObject == NULL) {
		return NULL;
	}

	/* Just deep call, we have 
	 * all neccessary functionlity and validation already in place */
	return (void*)Syscall1(MOLLENOS_SYSCALL_LOADSO, MOLLENOS_SYSCALL_PARAM(SharedObject));
}

/* Load a function-address given an shared object
 * handle and a function name, function must exist
 * otherwise null is returned */
void *SharedObjectGetFunction(void *Handle, const char *Function)
{
	/* Sanitize the arguments */
	if (Handle == NULL
		|| Function == NULL) {
		return NULL;
	}

	/* Just deep call, we have
	* all neccessary functionlity and validation already in place */
	return (void*)Syscall2(MOLLENOS_SYSCALL_LOADPROC, 
		MOLLENOS_SYSCALL_PARAM(Handle), MOLLENOS_SYSCALL_PARAM(Function));
}

/* Unloads a valid shared object handle
 * returns 0 on success */
void SharedObjectUnload(void *Handle)
{
	/* Sanitize the handle */
	if (Handle == NULL) {
		return;
	}

	/* Just deep call, we have
	* all neccessary functionlity and validation already in place */
	Syscall1(MOLLENOS_SYSCALL_UNLOADSO, MOLLENOS_SYSCALL_PARAM(Handle));
}

#endif