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
 * MollenOS - File Manager Service
 * - Handles all file related services and disk services
 */

/* Includes 
 * - System */
#include "include/vfs.h"
#include <os/mollenos.h>

/* Includes
 * - C-Library */
#include <strings.h>
#include <string.h>
#include <ctype.h>

/* Globals 
 * These are default static environment strings 
 * that can be resolved from an enum array */
const char *GlbEnvironmentalPaths[PathEnvironmentCount] = {
	"./",
	"./",

    // System paths
	":/",
	":/system/",

    // Shared paths
	":/shared/bin/",
	":/shared/documents/",
	":/shared/includes/",
	":/shared/libraries/",
	":/shared/media/",

    // User paths
	":/users/$(user)/",
    ":/users/$(user)/cache",

    // Application paths
	":/shared/appdata/$(app)/",
    ":/shared/appdata/$(app)/temp/"
};

/* These are the default fixed identifiers that can
 * be used in paths to denote access, mostly these
 * identifers must be preceeding the rest of the path */
struct {
	const char*         Identifier;
	EnvironmentPath_t   Resolve;
} GlbIdentifers[] = {
	{ "%sys%", PathSystemDirectory },
	{ NULL, PathCurrentWorkingDirectory }
};

/* VfsPathResolveEnvironment
 * Resolves the given env-path identifier to a string
 * that can be used to locate files. */
MString_t*
VfsPathResolveEnvironment(
    _In_ EnvironmentPath_t Base)
{
	// Variables
	MString_t *ResolvedPath = NULL;
	CollectionItem_t *fNode = NULL;
	int pIndex              = (int)Base;
	int pFound              = 0;
	char PathBuffer[_MAXPATH];

	// Handle Special Case - 0 & 1
	// Just return the current working directory
	if (Base == PathCurrentWorkingDirectory
		|| Base == PathCurrentAssemblyDirectory) {
		memset(&PathBuffer[0], 0, _MAXPATH);
		if (Base == PathCurrentWorkingDirectory) {
			if (GetWorkingDirectory(&PathBuffer[0], _MAXPATH) != OsSuccess) {
				return NULL;
			}
			else {
				return MStringCreate(&PathBuffer[0], StrUTF8);
			}
		}
		else {
			if (GetAssemblyDirectory(&PathBuffer[0], _MAXPATH) != OsSuccess) {
				return NULL;
			}
			else {
				return MStringCreate(&PathBuffer[0], StrUTF8);
			}
		}
	}

	// Create a new string instance to store resolved in
	ResolvedPath = MStringCreate(NULL, StrUTF8);
	_foreach(fNode, VfsGetFileSystems()) {
		FileSystem_t *Fs = (FileSystem_t*)fNode->Data;
		if (Fs->Descriptor.Flags & __FILESYSTEM_BOOT) {
			MStringAppendString(ResolvedPath, Fs->Identifier);
			pFound = 1;
			break;
		}
	}
	if (!pFound) {
		MStringDestroy(ResolvedPath);
		return NULL;
	}

	// Now append the special paths and return it
	MStringAppendCharacters(ResolvedPath, GlbEnvironmentalPaths[pIndex], StrUTF8);
	return ResolvedPath;
}

/* VfsPathCanonicalize
 * Canonicalizes the path by removing extra characters
 * and resolving all identifiers in path */
MString_t*
VfsPathCanonicalize(
    _In_ EnvironmentPath_t  Base, 
    _In_ const char*        Path)
{
	// Variables
	CollectionItem_t *fNode = NULL;
	MString_t *AbsPath      = NULL;
	int i                   = 0;

    // Create result string
    AbsPath = MStringCreate(NULL, StrUTF8);

	// There must be either a FS indicator in a path
	// or an identifier that resolves one for us, otherwise
	// we must assume the path is relative
	if (strchr(Path, ':') == NULL && strchr(Path, '%') == NULL) {
		MString_t *BasePath = VfsPathResolveEnvironment(Base);
		if (BasePath == NULL) {
			MStringDestroy(AbsPath);
			return NULL;
		}
		else {
			MStringCopy(AbsPath, BasePath, -1);
			if (MStringGetCharAt(AbsPath, MStringLength(AbsPath) - 1) != '/') {
				MStringAppendCharacter(AbsPath, '/');
			}
		}
	}

	// Iterate all characters and build a new string
	// containing the canonicalized path simoultanously
	while (Path[i]) {
		if (Path[i] == '/' && i == 0) { // Always skip initial '/'
			i++;
			continue;
		}

		// Special Case 1 - Identifier
		if (Path[i] == '%') {
			int j = 0;
			while (GlbIdentifers[j].Identifier != NULL) { // Iterate all possible identifiers
				if (strcasecmp(GlbIdentifers[j].Identifier, 
					(__CONST char*)&Path[i])) {

					// Resolve filesystem
					_foreach(fNode, VfsGetFileSystems()) {
						FileSystem_t *Fs = (FileSystem_t*)fNode->Data;
						if (Fs->Descriptor.Flags & __FILESYSTEM_BOOT) {
							MStringAppendString(AbsPath, Fs->Identifier);
							break;
						}
					}

					// Resolve identifier 
					MStringAppendCharacters(AbsPath, 
						GlbEnvironmentalPaths[GlbIdentifers[j].Resolve], StrUTF8);
					i += strlen(GlbIdentifers[j].Identifier);

					// Is the next char a '/'? If so skip 
					if (Path[i] == '/' || Path[i] == '\\') {
						i++;
					}
					break;
				}
			}

			// Did we find what we looked for?
			if (GlbIdentifers[j].Identifier != NULL) {
				continue;
			}
		}

		// Special Case 2, 3 and 4
		// 2 - If it's ./ or .\ ignore it
		// 3 - If it's ../ or ..\ go back 
		// 4 - Normal case, copy
		if (Path[i] == '.' && (Path[i + 1] == '/' || Path[i + 1] == '\\')) {
			i += 2;
			continue;
		}
		else if (Path[i] == '.' && Path[i + 1] == '.'
			&& (Path[i + 2] == '/' || Path[i + 2] == '\\')) {
			int Index = MStringFindReverse(AbsPath, '/');
			if (MStringGetCharAt(AbsPath, Index - 1) != ':') {
				MString_t *Modified = MStringSubString(AbsPath, 0, Index);
				MStringDestroy(AbsPath);
				AbsPath = Modified;
			}
		}
		else {
			if (Path[i] == '\\') {
				MStringAppendCharacter(AbsPath, '/');
			}
			else {
				MStringAppendCharacter(AbsPath, Path[i]);
			}
		}
		i++;
	}

	// Replace dublicate // with /
	//MStringReplace(AbsPath, "//", "/");
	return AbsPath;
}
