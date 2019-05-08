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
 * Path Definitions & Structures
 * - This header describes the path-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */
#define __TRACE

#include <ddk/contracts/filesystem.h>
#include <ddk/utils.h>
#include <internal/_syscalls.h>
#include <internal/_utils.h>
#include <os/services/path.h>
#include <os/services/process.h>
#include <string.h>

OsStatus_t
SetWorkingDirectory(
    _In_ const char* Path)
{
    OsFileDescriptor_t FileInfo;
	char               TempBuffer[_MAXPATH];
    TRACE("SetWorkingDirectory(%s)", Path);

	if (Path == NULL || strlen(Path) == 0) {
		return OsError;
	}
	memset(&TempBuffer[0], 0, _MAXPATH);

    if (strstr(Path, ":/") != NULL || strstr(Path, ":\\") != NULL) {
        memcpy(&TempBuffer[0], Path, strlen(Path));
    }
    else {
        if (GetWorkingDirectory(&TempBuffer[0], _MAXPATH) != OsSuccess) {
            return OsError;
        }
        strcat(&TempBuffer[0], Path);
    }
    
    if (PathCanonicalize(&TempBuffer[0], &TempBuffer[0], _MAXPATH) == OsSuccess) {
        FileSystemCode_t FsStatus = GetFileInformationFromPath(&TempBuffer[0], &FileInfo);
        if (FsStatus == FsOk && (FileInfo.Flags & FILE_FLAG_DIRECTORY)) {
            size_t CurrentLength = strlen(&TempBuffer[0]);
            if (TempBuffer[CurrentLength - 1] != '/') {
                TempBuffer[CurrentLength] = '/';
            }

            // Handle this differently based on a module or application
            if (IsProcessModule()) {
                return Syscall_SetWorkingDirectory(&TempBuffer[0]);
            }
            else {
                TRACE("...proc_set_cwd %s", &TempBuffer[0]);
                return ProcessSetWorkingDirectory(&TempBuffer[0]);
            }
        }
        else {
            ERROR("GetFileInformationFromPath(%s) Result: %u, %u",
                &TempBuffer[0], FsStatus, FileInfo.Flags);
        }
    }
    else {
        ERROR("Failed to canonicalize path for current path: %s", &TempBuffer[0]);
    }
    return OsError;
}

OsStatus_t
GetWorkingDirectory(
    _In_ char*  PathBuffer, 
    _In_ size_t MaxLength)
{
    OsStatus_t Status;

	if (PathBuffer == NULL || MaxLength == 0) {
		return OsError;
	}

    if (IsProcessModule()) {
        Status = Syscall_GetWorkingDirectory(PathBuffer, MaxLength);
    }
	else {
        Status = ProcessGetWorkingDirectory(UUID_INVALID, PathBuffer, MaxLength);
        TRACE("GetWorkingDirectory => %s", PathBuffer);
    }
    return Status;
}

OsStatus_t
GetAssemblyDirectory(
    _In_ char*  PathBuffer, 
    _In_ size_t MaxLength)
{
	if (PathBuffer == NULL || MaxLength == 0) {
		return OsError;
	}

    if (IsProcessModule()) {
        return Syscall_GetAssemblyDirectory(PathBuffer, MaxLength);
    }
	else {
        return ProcessGetAssemblyDirectory(UUID_INVALID, PathBuffer, MaxLength);
    }
}

OsStatus_t
GetUserDirectory(
    _In_ char*  PathBuffer, 
    _In_ size_t MaxLength)
{
	if (PathBuffer == NULL || MaxLength == 0) {
		return OsError;
	}
    return PathResolveEnvironment(IsProcessModule() ? 
        PathSystemDirectory : UserDataDirectory, PathBuffer, MaxLength);
}

OsStatus_t
GetUserCacheDirectory(
    _In_ char*  PathBuffer, 
    _In_ size_t MaxLength)
{
	if (PathBuffer == NULL || MaxLength == 0) {
		return OsError;
	}
    return PathResolveEnvironment(IsProcessModule() ? 
        PathSystemDirectory : UserCacheDirectory, PathBuffer, MaxLength);
}

OsStatus_t
GetApplicationDirectory(
    _In_ char*  PathBuffer, 
    _In_ size_t MaxLength)
{
	if (PathBuffer == NULL || MaxLength == 0) {
		return OsError;
	}
    return PathResolveEnvironment(IsProcessModule() ? 
        PathSystemDirectory : ApplicationDataDirectory, PathBuffer, MaxLength);
}

OsStatus_t
GetApplicationTemporaryDirectory(
    _In_ char*  PathBuffer, 
    _In_ size_t MaxLength)
{
	if (PathBuffer == NULL || MaxLength == 0) {
		return OsError;
	}
    return PathResolveEnvironment(IsProcessModule() ? 
        PathSystemDirectory : ApplicationTemporaryDirectory, PathBuffer, MaxLength);
}
