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
 * MollenOS - File Manager Service
 * - Handles all file related services and disk services
 */
//#define __TRACE

#include "include/vfs.h"
#include <os/mollenos.h>
#include <ddk/utils.h>
#include <strings.h>
#include <string.h>
#include <ctype.h>

#define IS_SEPERATOR(StringPointer)     ((StringPointer)[0] == '/' || (StringPointer)[0] == '\\')
#define IS_EOL(StringPointer)           ((StringPointer)[0] == '\0')

#define IS_IDENTIFIER(StringPointer)    ((StringPointer)[0] == '$' && (StringPointer)[1] != '(')
#define IS_VARIABLE(StringPointer)      ((StringPointer)[0] == '$' && (StringPointer)[1] == '(')

/* Globals 
 * These are default static environment strings 
 * that can be resolved from an enum array */
static const char *EnvironmentalPaths[PathEnvironmentCount] = {
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
static struct VfsIdentifier {
	const char*         Identifier;
	EnvironmentPath_t   Resolve;
} VfsIdentifiers[] = {
	{ "sys", PathSystemDirectory },
	{ "bin", PathCommonBin },
	{ NULL, PathSystemDirectory }
};

/* VfsPathResolveEnvironment
 * Resolves the given env-path identifier to a string
 * that can be used to locate files. */
MString_t*
VfsPathResolveEnvironment(
    _In_ EnvironmentPath_t Base)
{
	MString_t *ResolvedPath = NULL;
	CollectionItem_t *fNode = NULL;
	int pIndex              = (int)Base;
	int pFound              = 0;

	// Create a new string instance to store resolved in
	ResolvedPath = MStringCreate(NULL, StrUTF8);
	_foreach(fNode, VfsGetFileSystems()) {
		FileSystem_t *Fs = (FileSystem_t*)fNode->Data;
		if (Fs->Descriptor.Flags & __FILESYSTEM_BOOT) {
			MStringAppend(ResolvedPath, Fs->Identifier);
			pFound = 1;
			break;
		}
	}
	if (!pFound) {
		MStringDestroy(ResolvedPath);
		return NULL;
	}

	// Now append the special paths and return it
	MStringAppendCharacters(ResolvedPath, EnvironmentalPaths[pIndex], StrUTF8);
	return ResolvedPath;
}

/* VfsExpandIdentifier
 * Expands the given identifier into the string passed. This append the character to the end of the string. 
 * If an identifier is not present in the beginning of the string, it is invalid. */
OsStatus_t
VfsExpandIdentifier(
    _In_ MString_t*     TargetString,
    _In_ const char*    Identifier)
{
	CollectionItem_t*   Node    = NULL;
    int                 j       = 0;
    while (VfsIdentifiers[j].Identifier != NULL) { // Iterate all possible identifiers
        struct VfsIdentifier* VfsIdent = &VfsIdentifiers[j];
        size_t IdentifierLength = strlen(VfsIdent->Identifier);
        if (!strncasecmp(VfsIdent->Identifier, (const char*)&Identifier[1], IdentifierLength)) {
            _foreach(Node, VfsGetFileSystems()) { // Resolve filesystem
                FileSystem_t *Fs = (FileSystem_t*)Node->Data;
                if (Fs->Descriptor.Flags & __FILESYSTEM_BOOT) {
                    MStringAppend(TargetString, Fs->Identifier);
                    break;
                }
            }
            MStringAppendCharacters(TargetString, EnvironmentalPaths[VfsIdent->Resolve], StrUTF8);
            return OsSuccess;
        }
        j++;
    }
    return OsError;
}

/* VfsPathCanonicalize
 * Canonicalizes the path by removing extra characters
 * and resolving all identifiers in path */
MString_t*
VfsPathCanonicalize(
    _In_ const char*        Path)
{
	MString_t*  AbsPath;
	int         i = 0;

    TRACE("VfsPathCanonicalize(%s)", Path);

	// Iterate all characters and build a new string
	// containing the canonicalized path simoultanously
    AbsPath = MStringCreate(NULL, StrUTF8);
	while (Path[i]) {
		if (IS_SEPERATOR(&Path[i]) && i == 0) { // Always skip initial '/'
			i++;
			continue;
		}

		// Special case 1 - Identifier
		if (IS_IDENTIFIER(&Path[i])) {
            /* OsStatus_t Status = */ VfsExpandIdentifier(AbsPath, &Path[i]);
            while (!IS_EOL(&Path[i]) && !IS_SEPERATOR(&Path[i])) {
                i++;
            }
            if (IS_SEPERATOR(&Path[i])) {
                i++; // Skip seperator
            }
            continue;
		}

        // Special case 2 - variables
        if (IS_VARIABLE(&Path[i])) {
            // VfsExpandVariable();
            while (Path[i] != ')') {
                i++;
            }
            i++; // Skip the paranthesis
            if (IS_SEPERATOR(&Path[i])) {
                i++; // skip seperator as well
            }
            continue;
        }

		// Special case 3, 4 and 5
		// 3 - If it's ./ or .\ ignore it
		// 4 - If it's ../ or ..\ go back 
		// 5 - Normal case, copy
		if (Path[i] == '.' && IS_SEPERATOR(&Path[i + 1])) {
			i += 2;
			continue;
		}
        else if (Path[i] == '.' && Path[i + 1] == '\0') {
            break;
        }
		else if (Path[i] == '.' && Path[i + 1] == '.' && (IS_SEPERATOR(&Path[i + 2]) || Path[i + 2] == '\0')) {
            int Index = 0;
            while (Index != MSTRING_NOT_FOUND) {
                Index = MStringFindReverse(AbsPath, '/', 0);
                if (Index == (MStringLength(AbsPath) - 1) && 
                    MStringGetCharAt(AbsPath, Index - 1) != ':') {
                    MString_t* Modified = MStringSubString(AbsPath, 0, Index);
                    MStringDestroy(AbsPath);
                    AbsPath = Modified;
                }
                else {
                    break;
                }
            }
            
            if (Index != MSTRING_NOT_FOUND) {
                TRACE("Going back in %s", MStringRaw(AbsPath));
				MString_t* Modified = MStringSubString(AbsPath, 0, Index + 1); // Include the '/'
				MStringDestroy(AbsPath);
				AbsPath = Modified;
            }
		}
		else {
            // Don't double add '/'
            if (IS_SEPERATOR(&Path[i])) {
                int Index = MStringFindReverse(AbsPath, '/', 0);
                if ((Index + 1) != MStringLength(AbsPath)) {
                    MStringAppendCharacter(AbsPath, '/');
                }
            }
			else {
				MStringAppendCharacter(AbsPath, Path[i]);
			}
		}
		i++;
	}
    TRACE("=> %s", MStringRaw(AbsPath));
	return AbsPath;
}
