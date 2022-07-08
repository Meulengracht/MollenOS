/**
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
 */

//#define __TRACE

#include <ddk/utils.h>
#include <internal/_ipc.h>
#include <internal/_utils.h>
#include <os/mollenos.h>
#include <os/process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>

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
    if (path != NULL && (path[0] == '/' || strchr(path, ':') != NULL)) {
        return true;
    }
    return false;
}

oserr_t
GetFullPath(
        _In_ const char* path,
        _In_ int         followLinks,
        _In_ char*       buffer,
        _In_ size_t      maxLength)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetFileService());
    oserr_t               status;

    if (path == NULL || buffer == NULL || maxLength == 0) {
        return OsInvalidParameters;
    }

    if (!PathIsAbsolute(path)) {
        OsFileDescriptor_t descriptor;
        char*              fullPath = NULL;

        // Test current working directory, we are a bit out of line here but we reuse
        // the provided buffer :-)
        GetWorkingDirectory(&buffer[0], maxLength);
        fullPath = PathJoin(&buffer[0], path);
        if (GetFileInformationFromPath(fullPath, 0, &descriptor) != OsOK) {
            free(fullPath);
            fullPath = NULL;
        }

        // Now we test all paths in PATH
        if (fullPath == NULL) {
            char* token = getenv("PATH");
            for (char* i = strtok( token, ";"); i; i = strtok(NULL, ";")) {
                char* combined = PathJoin(i, path);
                if (GetFileInformationFromPath(combined, 0, &descriptor) == OsOK) {
                    fullPath = combined;
                    break;
                }
                free(combined);
            }
        }

        // path was invalid, we can early exit here
        if (fullPath == NULL) {
            return OsNotExists;
        }

        sys_file_realpath(GetGrachtClient(), &msg.base, fullPath, followLinks);
        gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
        sys_file_realpath_result(GetGrachtClient(), &msg.base, &status, buffer, maxLength);
        free(fullPath);
    } else {
        sys_file_realpath(GetGrachtClient(), &msg.base, path, followLinks);
        gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
        sys_file_realpath_result(GetGrachtClient(), &msg.base, &status, buffer, maxLength);
    }
    return status;
}

oserr_t
GetWorkingDirectory(
    _In_ char*  buffer,
    _In_ size_t maxLength)
{
    if (buffer == NULL || maxLength == 0) {
        return OsInvalidParameters;
    }
    return ProcessGetWorkingDirectory(UUID_INVALID, buffer, maxLength);
}

oserr_t
ChangeWorkingDirectory(
    _In_ const char* path)
{
	char        canonBuffer[_MAXPATH];
    oserr_t  osStatus;
    struct DIR* dir;
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
        if (osStatus != OsOK) {
            return osStatus;
        }
        strncat(&canonBuffer[0], path, _MAXPATH);
    }

    if (opendir(&canonBuffer[0], 0, &dir)) {
        // TODO convert errno to osstatus
        return OsError;
    }

    osStatus = GetFilePathFromFd(dir->d_handle, &canonBuffer[0], _MAXPATH);
    closedir(dir);
    if (osStatus != OsOK) {
        return osStatus;
    }
    return ProcessSetWorkingDirectory(&canonBuffer[0]);
}

oserr_t
GetAssemblyDirectory(
    _In_ char*  buffer,
    _In_ size_t maxLength)
{
    if (buffer == NULL || maxLength == 0) {
        return OsInvalidParameters;
    }
    return ProcessGetAssemblyDirectory(UUID_INVALID, buffer, maxLength);
}

oserr_t
GetUserDirectory(
    _In_ char*  buffer,
    _In_ size_t maxLength)
{
    char* path;
	if (buffer == NULL || maxLength == 0) {
		return OsInvalidParameters;
	}

    path = getenv("USRDIR");
    if (path) {
        strncpy(&buffer[0], path, maxLength);
        return OsOK;
    }
    return OsNotSupported;
}

oserr_t
GetUserCacheDirectory(
    _In_ char*  buffer,
    _In_ size_t maxLength)
{
    char* path;
	if (buffer == NULL || maxLength == 0) {
		return OsInvalidParameters;
	}

    path = getenv("USRDIR");
    if (path) {
        strncpy(&buffer[0], path, maxLength);
        strncat(&buffer[0], "/.cache", maxLength);
        return OsOK;
    }
    return OsNotSupported;
}

oserr_t
GetApplicationDirectory(
    _In_ char*  buffer,
    _In_ size_t maxLength)
{
    char* path;
	if (buffer == NULL || maxLength == 0) {
		return OsInvalidParameters;
	}

    path = getenv("APPDIR");
    if (path) {
        strncpy(&buffer[0], path, maxLength);
        return OsOK;
    }
    return OsNotSupported;
}

oserr_t
GetApplicationTemporaryDirectory(
    _In_ char*  buffer,
    _In_ size_t maxLength)
{
    char* path;
	if (buffer == NULL || maxLength == 0) {
		return OsInvalidParameters;
	}

    path = getenv("APPDIR");
    if (path) {
        strncpy(&buffer[0], path, maxLength);
        strncat(&buffer[0], "/.clear", maxLength);
        return OsOK;
    }
    return OsNotSupported;
}
