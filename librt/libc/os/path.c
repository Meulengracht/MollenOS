/* MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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

#include <os/contracts/filesystem.h>
#include <os/mollenos.h>
#include <os/syscall.h>
#include <string.h>

/* SetWorkingDirectory
 * Performs changes to the current working directory by canonicalizing the 
 * given path modifier or absolute path */
OsStatus_t
SetWorkingDirectory(
    _In_ const char *Path)
{
    OsFileDescriptor_t  FileInfo;
	char                TempBuffer[_MAXPATH];

	if (Path == NULL || strlen(Path) == 0) {
		return OsError;
	}
	memset(&TempBuffer[0], 0, _MAXPATH);

    if (strstr(Path, ":/") != NULL || strstr(Path, ":\\") != NULL) {
        memcpy(&TempBuffer[0], Path, strlen(Path));
    }
    else {
        if (Syscall_GetWorkingDirectory(UUID_INVALID, &TempBuffer[0], _MAXPATH) != OsSuccess) {
            return OsError;
        }
        strcat(&TempBuffer[0], Path);
    }
    
    if (PathCanonicalize(&TempBuffer[0], &TempBuffer[0], _MAXPATH) == OsSuccess) {
        if (GetFileInformationFromPath(&TempBuffer[0], &FileInfo) == OsSuccess &&
            FileInfo.Flags & FILE_FLAG_DIRECTORY) {
            size_t CurrentLength = strlen(&TempBuffer[0]);
            if (TempBuffer[CurrentLength - 1] != '/') {
                TempBuffer[CurrentLength] = '/';
            }
            return Syscall_SetWorkingDirectory(&TempBuffer[0]);
        }
    }
    return OsError;
}

/* GetWorkingDirectory
 * Queries the current working directory path for the current process (See _MAXPATH) */
OsStatus_t
GetWorkingDirectory(
    _In_ char*  PathBuffer, 
    _In_ size_t MaxLength)
{
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
    _In_ size_t MaxLength)
{
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
    _In_ size_t MaxLength)
{
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
	if (PathBuffer == NULL || MaxLength == 0) {
		return OsError;
	}
    return PathResolveEnvironment(ApplicationTemporaryDirectory, PathBuffer, MaxLength);
}
