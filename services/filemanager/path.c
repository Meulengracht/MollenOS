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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * File Manager Service
 * - Handles all file related services and disk services
 */

//#define __TRACE

#include <ctype.h>
#include <ddk/utils.h>
#include <vfs/filesystem.h>
#include <strings.h>
#include <string.h>
#include <stdlib.h>
#include <os/process.h>

#include "sys_path_service_server.h"

#define IS_SEPERATOR(str)     ((str)[0] == '/' || (str)[0] == '\\')
#define IS_EOL(str)           ((str)[0] == '\0')

#define IS_IDENTIFIER(str)    ((str)[0] == '$' && (str)[1] != '(')
#define IS_VARIABLE(str)      ((str)[0] == '$' && (str)[1] == '(')

static struct EnvironmentalPathSettings {
    const char*  identifier;
    unsigned int flags;
} g_environmentPaths[SYS_SYSTEM_PATHS_PATH_COUNT] = {
        // System drive paths [$sys]
        { ":/", __FILESYSTEM_BOOT },
        { ":/themes/", __FILESYSTEM_BOOT },

        // System data paths [$data]
        { ":/", __FILESYSTEM_DATA },
        { ":/bin/", __FILESYSTEM_DATA },
        { ":/include/", __FILESYSTEM_DATA },
        { ":/lib/", __FILESYSTEM_DATA },
        { ":/share/", __FILESYSTEM_DATA },

        // User paths [$home]
        { ":/users/$(user)/", __FILESYSTEM_USER },
        { ":/users/$(user)/cache", __FILESYSTEM_USER },

        // Application paths [$app]
        { ":/users/$(user)/appdata/$(app)/", __FILESYSTEM_USER },
        { ":/users/$(user)/appdata/$(app)/temp/", __FILESYSTEM_USER }
};

static struct VfsIdentifier {
    const char*           identifier;
    enum sys_system_paths resolve;
} g_vfsIdentifiers[] = {
    { "sys", SYS_SYSTEM_PATHS_PATH_SYSTEM },
    { "themes", SYS_SYSTEM_PATHS_PATH_SYSTEM_THEMES },
    { "data", SYS_SYSTEM_PATHS_PATH_DATA },
    { "bin", SYS_SYSTEM_PATHS_PATH_DATA_BIN },
    { "lib", SYS_SYSTEM_PATHS_PATH_DATA_LIB },
    { "share", SYS_SYSTEM_PATHS_PATH_DATA_SHARE },
    { NULL, SYS_SYSTEM_PATHS_PATH_COUNT }
};

static inline OsStatus_t __ResolveDriveIdentifier(MString_t* destination, unsigned int flags)
{
    FileSystem_t* fileSystem = VfsFileSystemGetByFlags(flags);
    if (fileSystem) {
        MStringAppend(destination, fileSystem->mount_point);
        return OsSuccess;
    }
    return OsDoesNotExist;
}

MString_t*
VfsPathResolveEnvironment(
    _In_ enum sys_system_paths base)
{
    MString_t*   resolvedPath;
    unsigned int requiredDiskFlags;
    OsStatus_t   status;
    TRACE("VfsPathResolveEnvironment(base=%u)", base);

    // Create a new string instance to store resolved in
    resolvedPath = MStringCreate(NULL, StrUTF8);
    if (!resolvedPath) {
        goto exit;
    }

    // get which type of resolvement this is
    requiredDiskFlags = g_environmentPaths[(int)base].flags;
    status            = __ResolveDriveIdentifier(resolvedPath, requiredDiskFlags);
    if (status != OsSuccess) {
        MStringDestroy(resolvedPath);
        resolvedPath = NULL;
        goto exit;
    }

    // Now append the special paths and return it
    MStringAppendCharacters(resolvedPath, g_environmentPaths[(int)base].identifier, StrUTF8);

exit:
    TRACE("VfsPathResolveEnvironment returns=%s", MStringRaw(resolvedPath));
    return resolvedPath;
}

static OsStatus_t
VfsExpandIdentifier(
    _In_ MString_t*  destination,
    _In_ const char* identifier)
{
    int        j        = 0;
    OsStatus_t osStatus = OsDoesNotExist;
    TRACE("VfsExpandIdentifier(identifier=%s)", identifier);

    while (g_vfsIdentifiers[j].identifier != NULL) { // Iterate all possible identifiers
        struct VfsIdentifier* vfsIdentifier    = &g_vfsIdentifiers[j];
        size_t                identifierLength = strlen(vfsIdentifier->identifier);
        unsigned int          requiredDiskFlags;

        if (!strncasecmp(vfsIdentifier->identifier, (const char*)&identifier[1], identifierLength)) {
            requiredDiskFlags = g_environmentPaths[vfsIdentifier->resolve].flags;
            osStatus = __ResolveDriveIdentifier(destination, requiredDiskFlags);
            if (osStatus != OsSuccess) {
                break;
            }

            MStringAppendCharacters(destination, g_environmentPaths[vfsIdentifier->resolve].identifier, StrUTF8);
            break;
        }
        j++;
    }

    TRACE("VfsExpandIdentifier returns=%u", osStatus);
    return osStatus;
}

MString_t*
VfsPathCanonicalize(
    _In_ const char* path)
{
    MString_t* absolutePath;
    int        i = 0;

    TRACE("VfsPathCanonicalize(path=%s)", path);

    // Iterate all characters and build a new string
    // containing the canonicalized path simoultanously
    absolutePath = MStringCreate(NULL, StrUTF8);
    if (!absolutePath) {
        return NULL;
    }

    while (path[i]) {
        if (IS_SEPERATOR(&path[i]) && i == 0) { // Always skip initial '/'
            i++;
            continue;
        }

        // Special case 1 - Identifier
        // To avoid abuse, we clear the string before expanding an identifier
        // in ANY case
        if (IS_IDENTIFIER(&path[i])) {
            MStringZero(absolutePath);
            /* OsStatus_t Status = */ VfsExpandIdentifier(absolutePath, &path[i]);
            while (!IS_EOL(&path[i]) && !IS_SEPERATOR(&path[i])) {
                i++;
            }
            if (IS_SEPERATOR(&path[i])) {
                i++; // Skip seperator
            }
            continue;
        }

        // Special case 2 - variables
        if (IS_VARIABLE(&path[i])) {
            // VfsExpandVariable();
            while (path[i] != ')') {
                i++;
            }
            i++; // Skip the paranthesis
            if (IS_SEPERATOR(&path[i])) {
                i++; // skip seperator as well
            }
            continue;
        }

        // Special case 3, 4 and 5
        // 3 - If it's ./ or .\ ignore it
        // 4 - If it's ../ or ..\ go back 
        // 5 - Normal case, copy
        if (path[i] == '.' && IS_SEPERATOR(&path[i + 1])) {
            i += 2;
            continue;
        }
        else if (path[i] == '.' && path[i + 1] == '\0') {
            break;
        }
        else if (path[i] == '.' && path[i + 1] == '.' && (IS_SEPERATOR(&path[i + 2]) || path[i + 2] == '\0')) {
            int previousIndex = 0;

            // find the previous path segment
            while (previousIndex != MSTRING_NOT_FOUND) {
                previousIndex = MStringFindReverse(absolutePath, '/', 0);

                if (previousIndex == (MStringLength(absolutePath) - 1) &&
                    MStringGetCharAt(absolutePath, previousIndex - 1) != ':') {
                    MString_t* subPath = MStringSubString(absolutePath, 0, previousIndex);
                    if (!subPath) {
                        previousIndex = MSTRING_NOT_FOUND;
                        break;
                    }

                    MStringDestroy(absolutePath);
                    absolutePath = subPath;
                }
                else {
                    break;
                }
            }
            
            if (previousIndex != MSTRING_NOT_FOUND) {
                TRACE("[vfs] [path] going back in %s", MStringRaw(absolutePath));
                MString_t* subPath = MStringSubString(absolutePath, 0, previousIndex + 1); // Include the '/'
                if (subPath) {
                    MStringDestroy(absolutePath);
                    absolutePath = subPath;
                }
            }
        }
        else {
            // Don't double add '/'
            if (IS_SEPERATOR(&path[i])) {
                int seperatorIndex = MStringFindReverse(absolutePath, '/', 0);
                if ((seperatorIndex + 1) != MStringLength(absolutePath)) {
                    MStringAppendCharacter(absolutePath, '/');
                }
            }
            else {
                MStringAppendCharacter(absolutePath, path[i]);
            }
        }
        i++;
    }
    TRACE("VfsPathCanonicalize returns=%s", MStringRaw(absolutePath));
    return absolutePath;
}

void ResolvePath(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    MString_t* resolvedPath;

    resolvedPath = VfsPathResolveEnvironment((enum sys_system_paths)request->parameters.resolve.base);
    if (!resolvedPath) {
        sys_path_resolve_response(request->message, OsDoesNotExist, "");
        VfsRequestDestroy(request);
        return;
    }

    sys_path_resolve_response(request->message, OsSuccess, MStringRaw(resolvedPath));
    MStringDestroy(resolvedPath);
    VfsRequestDestroy(request);
}

void CanonicalizePath(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    MString_t* canonicalizedPath;

    // we do not register these requests, they go about anonoumously
    canonicalizedPath = VfsPathCanonicalize(request->parameters.canonicalize.path);
    if (!canonicalizedPath) {
        sys_path_canonicalize_response(request->message, OsDoesNotExist, "");
        goto cleanup_request;
    }

    sys_path_canonicalize_response(request->message, OsSuccess, MStringRaw(canonicalizedPath));
    MStringDestroy(canonicalizedPath);

cleanup_request:
    free((void*)request->parameters.canonicalize.path);
    VfsRequestDestroy(request);
}

static OsStatus_t
VfsGuessBasePath(
        _In_ const char* path,
        _In_ char*       result)
{
    char* dot;

    TRACE("VfsGuessBasePath(path=%s)", path);
    if (!path || !result) {
        return OsInvalidParameters;
    }

    dot = strrchr(path, '.');
    if (dot) {
        // Binaries are found in common
        if (!strcmp(dot, ".run") || !strcmp(dot, ".dll")) {
            strcpy(result, "$bin/");
        }
            // Resources are found in system folder
        else {
            strcpy(result, "$data/");
        }
    }
        // Assume we are looking for folders in data folder
    else {
        strcpy(result, "$data/");
    }

    TRACE("VfsGuessBasePath returns=%s", result);
    return OsSuccess;
}

MString_t*
VfsPathResolve(
        _In_ UUId_t      processId,
        _In_ const char* path)
{
    MString_t* resolvedPath = NULL;
    OsStatus_t osStatus;

    TRACE("VfsPathResolve(processId=%u, path=%s)", processId, path);

    if (strchr(path, ':') == NULL && strchr(path, '$') == NULL) {
        char* basePath = (char*)malloc(_MAXPATH);
        if (!basePath) {
            ERROR("VfsPathResolve out of memory");
            return NULL;
        }
        memset(basePath, 0, _MAXPATH);

        osStatus = ProcessGetWorkingDirectory(processId, &basePath[0], _MAXPATH);
        if (osStatus != OsSuccess) {
            WARNING("VfsPathResolve failed to get working directory [%u], guessing base path", osStatus);

            osStatus = VfsGuessBasePath(path, &basePath[0]);
            if (osStatus != OsSuccess) {
                ERROR("VfsPathResolve failed to guess the base path: %u", osStatus);
                free(basePath);
                return NULL;
            }
        }
        else {
            strcat(basePath, "/");
        }
        strcat(basePath, path);
        resolvedPath = VfsPathCanonicalize(basePath);

        free(basePath);
    }
    else {
        resolvedPath = VfsPathCanonicalize(path);
    }

    TRACE("VfsPathResolve returns=%s", MStringRaw(resolvedPath));
    return resolvedPath;
}
