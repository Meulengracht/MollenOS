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
#include "include/vfs.h"
#include <strings.h>
#include <string.h>

#include "sys_path_service_server.h"

#define IS_SEPERATOR(str)     ((str)[0] == '/' || (str)[0] == '\\')
#define IS_EOL(str)           ((str)[0] == '\0')

#define IS_IDENTIFIER(str)    ((str)[0] == '$' && (str)[1] != '(')
#define IS_VARIABLE(str)      ((str)[0] == '$' && (str)[1] == '(')

static const char* g_environmentPaths[SYS_SYSTEM_PATHS_PATH_COUNT] = {
    // System paths
	":/",
	":/system/",
    ":/system/themes/",

    // Shared paths
	":/shared/bin/",
    ":/shared/include/",
	":/shared/lib/",
	":/shared/share/",

    // User paths
	":/users/$(user)/",
    ":/users/$(user)/cache",

    // Application paths
	":/shared/appdata/$(app)/",
    ":/shared/appdata/$(app)/temp/"
};

static struct VfsIdentifier {
	const char*           identifier;
	enum sys_system_paths resolve;
} g_vfsIdentifiers[] = {
	{ "sys", SYS_SYSTEM_PATHS_PATH_SYSTEM },
    { "themes", SYS_SYSTEM_PATHS_PATH_SYSTEM_THEMES },
	{ "bin", SYS_SYSTEM_PATHS_PATH_COMMON_BIN },
    { "lib", SYS_SYSTEM_PATHS_PATH_COMMON_LIB },
    { "share", SYS_SYSTEM_PATHS_PATH_COMMON_SHARE },
	{ NULL, SYS_SYSTEM_PATHS_PATH_COUNT }
};

static inline OsStatus_t __ResolveBootDriveIdentifier(MString_t* destination)
{
    foreach(element, VfsGetFileSystems()) {
        FileSystem_t *Fs = (FileSystem_t*)element->value;
        if (Fs->descriptor.Flags & __FILESYSTEM_BOOT) {
            MStringAppend(destination, Fs->identifier);
            return OsSuccess;
        }
    }
    return OsDoesNotExist;
}

MString_t*
VfsPathResolveEnvironment(
    _In_ enum sys_system_paths base)
{
	MString_t* resolvedPath = NULL;
	OsStatus_t status;
	TRACE("VfsPathResolveEnvironment(base=%u)", base);

	// Create a new string instance to store resolved in
	resolvedPath = MStringCreate(NULL, StrUTF8);
	if (!resolvedPath) {
	    goto exit;
	}

	status = __ResolveBootDriveIdentifier(resolvedPath);
	if (status != OsSuccess) {
	    MStringDestroy(resolvedPath);
        resolvedPath = NULL;
        goto exit;
	}

	// Now append the special paths and return it
	MStringAppendCharacters(resolvedPath, g_environmentPaths[(int)base], StrUTF8);

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

        if (!strncasecmp(vfsIdentifier->identifier, (const char*)&identifier[1], identifierLength)) {
            osStatus = __ResolveBootDriveIdentifier(destination);
            if (osStatus != OsSuccess) {
                break;
            }

            MStringAppendCharacters(destination, g_environmentPaths[vfsIdentifier->resolve], StrUTF8);
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

void sys_path_resolve_invocation(struct gracht_message* message, const enum sys_system_paths path)
{
    MString_t* resolvedPath;
    TRACE("svc_path_resolve_callback(base=%u)", path);

    resolvedPath = VfsPathResolveEnvironment(path);
    if (!resolvedPath) {
        sys_path_resolve_response(message, OsDoesNotExist, "");
        return;
    }

    sys_path_resolve_response(message, OsSuccess, MStringRaw(resolvedPath));
    MStringDestroy(resolvedPath);
}

void sys_path_canonicalize_invocation(struct gracht_message* message, const char* path)
{
    MString_t* canonicalizedPath;
    TRACE("svc_path_canonicalize_callback(path=%s)", path);

    canonicalizedPath = VfsPathCanonicalize(path);
    if (!canonicalizedPath) {
        sys_path_canonicalize_response(message, OsDoesNotExist, "");
        return;
    }
    
    sys_path_canonicalize_response(message, OsSuccess, MStringRaw(canonicalizedPath));
    MStringDestroy(canonicalizedPath);
}
