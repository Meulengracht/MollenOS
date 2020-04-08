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

#include <ddk/utils.h>
#include <internal/_ipc.h>
#include <internal/_syscalls.h>
#include <internal/_utils.h>
#include <os/mollenos.h>
#include <string.h>

OsStatus_t
PathResolveEnvironment(
    _In_ enum svc_path_environment_path environment,
    _In_ char*                          buffer,
    _In_ size_t                         maxLength)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetFileService());
	OsStatus_t               status;
	
	if (!buffer) {
	    return OsInvalidParameters;
	}
	
	svc_path_resolve_sync(GetGrachtClient(), &msg, environment, &status, &buffer[0], maxLength);
	gracht_vali_message_finish(&msg);
	return status;
}

OsStatus_t
PathCanonicalize(
    _In_ const char* path,
    _In_ char*       buffer,
    _In_ size_t      maxLength)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetFileService());
	OsStatus_t               status;
	
	if (!path || !buffer) {
	    return OsInvalidParameters;
	}
	
	svc_path_canonicalize_sync(GetGrachtClient(), &msg, path, &status, &buffer[0], maxLength);
	gracht_vali_message_finish(&msg);
	return status;
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
	    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetProcessService());
        svc_process_get_working_directory_sync(GetGrachtClient(), &msg, UUID_INVALID,
            &Status, PathBuffer, MaxLength);
        gracht_vali_message_finish(&msg);
        TRACE("GetWorkingDirectory => %s", PathBuffer);
    }
    return Status;
}

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
        OsStatus_t Status = GetFileInformationFromPath(&TempBuffer[0], &FileInfo);
        if (Status == OsSuccess && (FileInfo.Flags & FILE_FLAG_DIRECTORY)) {
            size_t CurrentLength = strlen(&TempBuffer[0]);
            if (TempBuffer[CurrentLength - 1] != '/') {
                TempBuffer[CurrentLength] = '/';
            }

            // Handle this differently based on a module or application
            if (IsProcessModule()) {
                return Syscall_SetWorkingDirectory(&TempBuffer[0]);
            }
            else {
	            struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetProcessService());
                TRACE("...proc_set_cwd %s", &TempBuffer[0]);
                svc_process_set_working_directory_sync(GetGrachtClient(), &msg, *GetInternalProcessId(),
                    &TempBuffer[0], &Status);
                gracht_vali_message_finish(&msg);
                return Status;
            }
        }
        else {
            ERROR("GetFileInformationFromPath(%s) Result: %u, %u",
                &TempBuffer[0], Status, FileInfo.Flags);
        }
    }
    else {
        ERROR("Failed to canonicalize path for current path: %s", &TempBuffer[0]);
    }
    return OsError;
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
	    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetProcessService());
	    OsStatus_t               status;
	    
        svc_process_get_assembly_directory_sync(GetGrachtClient(), &msg,
            UUID_INVALID, &status, PathBuffer, MaxLength);
        gracht_vali_message_finish(&msg);
        return status;
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
        path_system : path_user_data, PathBuffer, MaxLength);
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
        path_system : path_user_cache, PathBuffer, MaxLength);
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
        path_system : path_app_data, PathBuffer, MaxLength);
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
        path_system : path_app_temp, PathBuffer, MaxLength);
}
