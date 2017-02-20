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
 * MollenOS MCore - Path Definitions & Structures
 * - This header describes the path-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

/* Includes
 * - System */
#include <os/driver/contracts/filesystem.h>
#include <os/mollenos.h>
#include <os/syscall.h>

/* Includes
 * - Library */
#include <string.h>
#include <stddef.h>

/* PathQueryWorkingDirectory
 * Queries the current working directory path
 * for the current process (See _MAXPATH) */
OsStatus_t
PathQueryWorkingDirectory(
	_Out_ char *Buffer,
	_In_ size_t MaxLength)
{
	/* Do some quick validation */
	if (Buffer == NULL || MaxLength == 0) {
		return OsError;
	}

	/* Redirect to os-sublayer */
	return (OsStatus_t)Syscall2(SYSCALL_QUERYCWD,
		SYSCALL_PARAM(Buffer), SYSCALL_PARAM(MaxLength));
}

/* PathChangeWorkingDirectory
 * Performs changes to the current working directory
 * by canonicalizing the given path modifier or absolute
 * path */
OsStatus_t
PathChangeWorkingDirectory(
	_In_ __CONST char *Path)
{
	/* Variables */
	char TempBuffer[_MAXPATH];

	/* Do some quick validation */
	if (Path == NULL) {
		return OsError;
	}

	/* Reset the buffer */
	memset(TempBuffer, 0, _MAXPATH);

	/* Have our filemanager validate the path 
	 * changes before updating */
	if (PathCanonicalize(PathCurrentWorkingDirectory, 
		Path, &TempBuffer[0], _MAXPATH) != OsNoError) {
		return OsError;
	}

	/* Redirect to os-sublayer */
	return (OsStatus_t)Syscall1(SYSCALL_CHANGECWD, SYSCALL_PARAM(&TempBuffer[0]));
}

/* PathQueryApplication
 * Queries the application path for
 * the current process (See _MAXPATH) */
OsStatus_t
PathQueryApplication(
	_Out_ char *Buffer,
	_In_ size_t MaxLength)
{
	/* Do some quick validation */
	if (Buffer == NULL || MaxLength == 0) {
		return OsError;
	}

	/* Redirect to os-sublayer */
	return (OsStatus_t)Syscall2(SYSCALL_QUERYCAD,
		SYSCALL_PARAM(Buffer), SYSCALL_PARAM(MaxLength));
}
