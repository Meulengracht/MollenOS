/**
 * MollenOS
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Path Definitions & Structures
 * - This header describes the path-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

//#define __TRACE

#include <ddk/utils.h>
#include <internal/_ipc.h>
#include <internal/_utils.h>
#include <os/mollenos.h>
#include <os/process.h>
#include <stdio.h>
#include <string.h>

char* PathJoin(
        _In_ const char* path1,
        _In_ const char* path2)
{
    char*  combined;
    int    status;
    size_t path1Length;
    size_t path2Length;

    if (path1 == NULL && path2 == NULL) {
        return NULL;
    }

    if (path1 == NULL) {
        return strdup(path2);
    } else if (path2 == NULL) {
        return strdup(path1);
    }

    path1Length = strlen(path1);
    path2Length = strlen(path2);

    if (path1Length == 0) {
        return strdup(path2);
    } else if (path2Length == 0) {
        return strdup(path1);
    }

    if (path2[0] == '/') {
        path2++;
        path2Length--;
    };

    combined = malloc(path1Length + path2Length + 2);
    if (combined == NULL) {
        return NULL;
    }

    if (path1[path1Length - 1] != '/') {
        status = sprintf(combined, "%s/%s", path1, path2);
    } else {
        status = sprintf(combined, "%s%s", path1, path2);
    }

    if (status < 0) {
        free(combined);
        return NULL;
    }
    return combined;
}

bool
PathIsAbsolute(
        _In_ const char* path)
{
    // Check against a root path
    if (strchr(path, ':') == NULL && strchr(path, '$') == NULL) {
        return true;
    }
    return false;
}

OsStatus_t
PathResolveEnvironment(
    _In_ EnvironmentPath_t environment,
    _In_ char*             buffer,
    _In_ size_t            maxLength)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetFileService());
	OsStatus_t               status;
	
	if (!buffer) {
	    return OsInvalidParameters;
	}
	
	sys_path_resolve(GetGrachtClient(), &msg.base, (enum sys_system_paths)environment);
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
	sys_path_resolve_result(GetGrachtClient(), &msg.base, &status, buffer, maxLength);
	return status;
}

OsStatus_t
GetFullPath(
        _In_ const char* path,
        _In_ int         followLinks,
        _In_ char*       buffer,
        _In_ size_t      maxLength)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetFileService());
    OsStatus_t               status;

    if (path == NULL || buffer == NULL || maxLength == 0) {
        return OsInvalidParameters;
    }

    if (!PathIsAbsolute(path)) {
        char* fullPath = NULL;
        char* token    = getenv("PATH");
        for (char* i = strtok( token, ";"); i; i = strtok(NULL, ";")) {
            OsFileDescriptor_t descriptor;
            char*              combined = PathJoin(i, path);
            if (GetFileInformationFromPath(combined, 0, &descriptor) == OsSuccess) {
                fullPath = combined;
                break;
            }
            free(combined);
        }

        // path was invalid, we can early exit here
        if (fullPath == NULL) {
            return OsDoesNotExist;
        }

        // we were asked to follow symlinks, this means we want to resolve the real path of the symlink, which
        // we need vfs assistance for.
        sys_path_realpath(GetGrachtClient(), &msg.base, fullPath, followLinks);
        gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
        sys_path_realpath_result(GetGrachtClient(), &msg.base, &status, buffer, maxLength);
        free(fullPath);
    } else {
        sys_path_realpath(GetGrachtClient(), &msg.base, path, followLinks);
        gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
        sys_path_realpath_result(GetGrachtClient(), &msg.base, &status, buffer, maxLength);
    }
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
	
	sys_path_canonicalize(GetGrachtClient(), &msg.base, path);
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
	sys_path_canonicalize_result(GetGrachtClient(), &msg.base, &status, buffer, maxLength);
	return status;
}

OsStatus_t
GetWorkingDirectory(
    _In_ char*  PathBuffer, 
    _In_ size_t MaxLength)
{
    // Use the process version instead of the path version
    return ProcessGetWorkingDirectory(UUID_INVALID, PathBuffer, MaxLength);
}

OsStatus_t
ChangeWorkingDirectory(
    _In_ const char* path)
{
    OsFileDescriptor_t fileInfo;
	char               canonBuffer[_MAXPATH];
    char               outBuffer[_MAXPATH];
    OsStatus_t         osStatus;
    TRACE("ChangeWorkingDirectory(path=%s)", path);

	if (path == NULL || strlen(path) == 0) {
		return OsInvalidParameters;
	}

	memset(&canonBuffer[0], 0, _MAXPATH);
    if (PathIsAbsolute(path)) {
        TRACE("ChangeWorkingDirectory absolute path detected");
        memcpy(&canonBuffer[0], path, strnlen(path, _MAXPATH - 1));
    } else {
        TRACE("ChangeWorkingDirectory relative path detected");
        osStatus = GetWorkingDirectory(&canonBuffer[0], _MAXPATH);
        if (osStatus != OsSuccess) {
            return osStatus;
        }
        strncat(&canonBuffer[0], path, _MAXPATH);
    }

    TRACE("ChangeWorkingDirectory canonicalizing %s", &canonBuffer[0]);
    memset(&outBuffer[0], 0, _MAXPATH);
    osStatus = PathCanonicalize(&canonBuffer[0], &outBuffer[0], _MAXPATH);
    if (osStatus == OsSuccess) {
        TRACE("ChangeWorkingDirectory canonicalized path=%s", &outBuffer[0]);

        osStatus = GetFileInformationFromPath(&outBuffer[0], 1, &fileInfo);
        if (osStatus != OsSuccess) {
            return osStatus;
        }

        if (fileInfo.Flags & FILE_FLAG_DIRECTORY) {
            size_t currentLength;

            TRACE("ChangeWorkingDirectory path exists and is a directory");
            currentLength = strlen(&outBuffer[0]);
            if (outBuffer[currentLength - 1] != '/') {
                outBuffer[currentLength] = '/';
            }

            // Handle this differently based on a module or application
            return ProcessSetWorkingDirectory(&outBuffer[0]);
        } else {
            ERROR("ChangeWorkingDirectory path was not a directory: %u", fileInfo.Flags);
            return OsPathIsNotDirectory;
        }
    } else {
        ERROR("ChangeWorkingDirectory failed to canonicalize path=%s", &canonBuffer[0]);
    }
    return osStatus;
}

OsStatus_t
GetAssemblyDirectory(
    _In_ char*  PathBuffer,
    _In_ size_t MaxLength)
{
    return ProcessGetAssemblyDirectory(UUID_INVALID, PathBuffer, MaxLength);
}

OsStatus_t
GetUserDirectory(
    _In_ char*  PathBuffer, 
    _In_ size_t MaxLength)
{
	if (PathBuffer == NULL || MaxLength == 0) {
		return OsError;
	}
    return PathResolveEnvironment(__crt_is_phoenix() ?
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
    return PathResolveEnvironment(__crt_is_phoenix() ?
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
    return PathResolveEnvironment(__crt_is_phoenix() ?
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
    return PathResolveEnvironment(__crt_is_phoenix() ?
                                  PathSystemDirectory : ApplicationTemporaryDirectory, PathBuffer, MaxLength);
}
