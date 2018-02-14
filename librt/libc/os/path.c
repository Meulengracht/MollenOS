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
#include <os/contracts/filesystem.h>
#include <os/mollenos.h>
#include <os/syscall.h>

/* Includes
 * - Library */
#include <string.h>
#include <stddef.h>

/* SetWorkingDirectory
 * Performs changes to the current working directory by canonicalizing the 
 * given path modifier or absolute path */
OsStatus_t
SetWorkingDirectory(
    _In_ const char *Path)
{
    // Variables
	char TempBuffer[_MAXPATH];

	if (Path == NULL) {
		return OsError;
	}

    // Make sure the path is valid by asking filemanager
	memset(&TempBuffer[0], 0, _MAXPATH);
    if (Syscall_GetWorkingDirectory(UUID_INVALID, &TempBuffer[0], _MAXPATH) != OsSuccess) {
        return OsError;
    }
    strcat(&TempBuffer[0], Path);
	if (PathCanonicalize(&TempBuffer[0], &TempBuffer[0], _MAXPATH) != OsSuccess) {
		return OsError;
	}
	return Syscall_SetWorkingDirectory(&TempBuffer[0]);
}

/* GetWorkingDirectory
 * Queries the current working directory path for the current process (See _MAXPATH) */
OsStatus_t
GetWorkingDirectory(
    _In_ char*  PathBuffer, 
    _In_ size_t MaxLength) {
	if (PathBuffer == NULL || MaxLength == 0) {
		return OsError;
	}
	return Syscall_GetWorkingDirectory(UUID_INVALID, PathBuffer, MaxLength);
}

/* GetWorkingDirectoryOfApplication
 * Queries the current working directory path for the specific process (See _MAXPATH) */
OsStatus_t
GetWorkingDirectoryOfApplication(
    _In_ UUId_t ProcessId,
    _In_ char*  PathBuffer,
    _In_ size_t MaxLength) {
	if (PathBuffer == NULL || MaxLength == 0) {
		return OsError;
	}
	return Syscall_GetWorkingDirectory(ProcessId, PathBuffer, MaxLength);
}

/* GetAssemblyDirectory
 * Queries the application path for the current process (See _MAXPATH) */
OsStatus_t
GetAssemblyDirectory(
    _In_ char*  PathBuffer, 
    _In_ size_t MaxLength) {
	if (PathBuffer == NULL || MaxLength == 0) {
		return OsError;
	}
	return Syscall_GetAssemblyDirectory(PathBuffer, MaxLength);
}

/* GetUserDirectory 
 * Queries the system for the current user data directory. (See _MAXPATH) */
OsStatus_t
GetUserDirectory(
    _In_ char*  PathBuffer, 
    _In_ size_t MaxLength)
{
    // Quick validation before passing on
	if (PathBuffer == NULL || MaxLength == 0) {
		return OsError;
	}
    return PathResolveEnvironment(UserDataDirectory, PathBuffer, MaxLength);
}

/* GetUserCacheDirectory 
 * Queries the system for the current user cache directory. (See _MAXPATH) */
OsStatus_t
GetUserCacheDirectory(
    _In_ char*  PathBuffer, 
    _In_ size_t MaxLength)
{
    // Quick validation before passing on
	if (PathBuffer == NULL || MaxLength == 0) {
		return OsError;
	}
    return PathResolveEnvironment(UserCacheDirectory, PathBuffer, MaxLength);
}

/* GetApplicationDirectory 
 * Queries the system for the current application data directory. (See _MAXPATH) */
OsStatus_t
GetApplicationDirectory(
    _In_ char*  PathBuffer, 
    _In_ size_t MaxLength)
{
    // Quick validation before passing on
	if (PathBuffer == NULL || MaxLength == 0) {
		return OsError;
	}
    return PathResolveEnvironment(ApplicationDataDirectory, PathBuffer, MaxLength);
}

/* GetApplicationTemporaryDirectory 
 * Queries the system for the current application temporary directory. (See _MAXPATH) */
OsStatus_t
GetApplicationTemporaryDirectory(
    _In_ char*  PathBuffer, 
    _In_ size_t MaxLength)
{
    // Quick validation before passing on
	if (PathBuffer == NULL || MaxLength == 0) {
		return OsError;
	}
    return PathResolveEnvironment(ApplicationTemporaryDirectory, PathBuffer, MaxLength);
}
