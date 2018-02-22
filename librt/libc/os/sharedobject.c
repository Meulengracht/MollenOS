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
 * MollenOS MCore - Shared Object Support Definitions & Structures
 * - This header describes the base sharedobject-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

/* Includes 
 * - System */
#include <os/sharedobject.h>
#include <os/syscall.h>

/* Includes
 * - Library */
#include <ds/collection.h>
#include <stdlib.h>
#include <stddef.h>
#include <ctype.h>

typedef struct _LibraryItem {
    CollectionItem_t Header;
    Handle_t         Handle;
    int              References;
} LibraryItem_t;

/* Globals
 * - Function blueprint for dll-entry */
typedef void (*SOInitializer_t)(int);
static Collection_t LoadedLibraries = COLLECTION_INIT(KeyInteger);

/* SharedObjectHash
 * Helper utility to identify shared libraries */
size_t SharedObjectHash(const char *String) {
	uint8_t* Pointer    = (uint8_t*)String;
	size_t Hash         = 5381;
	int Character       = 0;
	if (String == NULL) {
        return 0;
    }
	while ((Character = tolower(*Pointer++)) != 0)
		Hash = ((Hash << 5) + Hash) + Character; /* hash * 33 + c */
	return Hash;
}

/* SharedObjectLoad
 * Load a shared object given a path
 * path must exists otherwise NULL is returned */
Handle_t 
SharedObjectLoad(
	_In_ const char* SharedObject)
{
    // Variables
    SOInitializer_t Initializer = NULL;
    LibraryItem_t *Library      = NULL;
    Handle_t Result             = HANDLE_INVALID;
    DataKey_t Key               = { (int)SharedObjectHash(SharedObject) };

    // Special case
    if (SharedObject == NULL) {
        return HANDLE_GLOBAL;
    }

    Library = CollectionGetDataByKey(&LoadedLibraries, Key, 0);
    if (Library == NULL) {
	    Result = Syscall_LibraryLoad(SharedObject);
        if (Result != HANDLE_INVALID) {
            Library = (LibraryItem_t*)malloc(sizeof(LibraryItem_t));
            COLLECTION_NODE_INIT((CollectionItem_t*)Library, Key);
            Library->Handle     = Result;
            Library->References = 0;
            CollectionAppend(&LoadedLibraries, (CollectionItem_t*)Library);
        }
    }

    if (Library != NULL) {
        Library->References++;
        if (Library->References == 1) {
            Initializer = (SOInitializer_t)SharedObjectGetFunction(Library->Handle, "__CrtLibraryEntry");
            if (Initializer != NULL) {
                Initializer(DLL_ACTION_INITIALIZE);
            }
        }
        Result = Library->Handle;
    }
    return Result;
}

/* SharedObjectGetFunction
 * Load a function-address given an shared object
 * handle and a function name, function must exist
 * otherwise null is returned */
void*
SharedObjectGetFunction(
	_In_ Handle_t       Handle, 
	_In_ const char*    Function)
{
	if (Handle == HANDLE_INVALID || Function == NULL) {
		return NULL;
	}
	return (void*)Syscall_LibraryFunction(Handle, Function);
}

/* SharedObjectUnload
 * Unloads a valid shared object handle
 * returns OsError on failure */
OsStatus_t 
SharedObjectUnload(
	_In_ Handle_t Handle)
{
    // Variables
    SOInitializer_t Initialize  = NULL;
    LibraryItem_t *Library      = NULL;

	// Sanitize input
	if (Handle == HANDLE_INVALID) {
		return OsError;
	}
    if (Handle == HANDLE_GLOBAL) {
        return OsSuccess;
    }
    foreach(Node, &LoadedLibraries) {
        if (((LibraryItem_t*)Node)->Handle == Handle) {
            Library = (LibraryItem_t*)Node;
            break;
        }
    }
    if (Library != NULL) {
        Library->References--;
        if (Library->References == 0) {
            // Run finalizer before unload
            Initialize = (SOInitializer_t)SharedObjectGetFunction(Handle, "__CrtLibraryEntry");
            if (Initialize != NULL) {
                Initialize(DLL_ACTION_FINALIZE);
            }
	        return Syscall_LibraryUnload(Handle);
        }
    }
    return OsError;
}
