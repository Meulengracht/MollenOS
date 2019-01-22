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
 * Shared Object Support Definitions & Structures
 * - This header describes the base sharedobject-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <internal/_syscalls.h>
#include <internal/_utils.h>
#include <os/sharedobject.h>
#include <ds/collection.h>
#include <ds/mstring.h>
#include <os/process.h>
#include <stdlib.h>
#include <stddef.h>
#include <ctype.h>

typedef struct _LibraryItem {
    CollectionItem_t Header;
    Handle_t         Handle;
    int              References;
} LibraryItem_t;

typedef void (*SOInitializer_t)(int);
static Collection_t LoadedLibraries = COLLECTION_INIT(KeyId);

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

Handle_t 
SharedObjectLoad(
	_In_ const char* SharedObject)
{
    SOInitializer_t Initializer = NULL;
    LibraryItem_t* Library      = NULL;
    Handle_t Result             = HANDLE_INVALID;
    DataKey_t Key               = { .Value.Id = SharedObjectHash(SharedObject) };

    // Special case
    if (SharedObject == NULL) {
        return HANDLE_GLOBAL;
    }

    Library = CollectionGetDataByKey(&LoadedLibraries, Key, 0);
    if (Library == NULL) {
        OsStatus_t Status;
        if (IsProcessModule()) {
            Status = Syscall_LibraryLoad(SharedObject, NULL, 0, &Result);
        }
        else {
            Status = ProcessLoadLibrary(SharedObject, &Result);
        }

        if (Status == OsSuccess && Result != HANDLE_INVALID) {
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

void*
SharedObjectGetFunction(
	_In_ Handle_t       Handle, 
	_In_ const char*    Function)
{
	if (Handle == HANDLE_INVALID || Function == NULL) {
		return NULL;
	}

    if (IsProcessModule()) {
        return (void*)Syscall_LibraryFunction(Handle, Function);
    }
	else {
        uintptr_t AddressOfFunction;
        if (ProcessGetLibraryFunction(Handle, Function, &AddressOfFunction) != OsSuccess) {
            return NULL;
        }
        return (void*)AddressOfFunction;
    }
}

OsStatus_t 
SharedObjectUnload(
	_In_ Handle_t Handle)
{
    SOInitializer_t Initialize = NULL;
    LibraryItem_t*  Library    = NULL;

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

            if (IsProcessModule()) {
                return Syscall_LibraryUnload(Handle);
            }
	        else {
                return ProcessUnloadLibrary(Handle);
            }
        }
    }
    return OsError;
}
