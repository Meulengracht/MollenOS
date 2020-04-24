/**
 * MollenOS
 *
 * Copyright 2019, Philip Meulengracht
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
 * DLL Service Definitions & Structures
 * - This header describes the base library-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <ctype.h>
#include <ds/collection.h>
#include <ds/mstring.h>
#include <errno.h>
#include <internal/_ipc.h>
#include <internal/_syscalls.h>
#include <internal/_utils.h>
#include <os/mollenos.h>
#include <os/sharedobject.h>
#include <stdlib.h>
#include <string.h>

typedef struct LibraryItem {
    CollectionItem_t Header;
    Handle_t         Handle;
    int              References;
} LibraryItem_t;

typedef void (*SOInitializer_t)(int);
static Collection_t LoadedLibraries = COLLECTION_INIT(KeyId);

static size_t SharedObjectHash(const char *String) {
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
    LibraryItem_t*  Library     = NULL;
    Handle_t        Result      = HANDLE_INVALID;
    DataKey_t       Key         = { .Value.Id = SharedObjectHash(SharedObject) };
    OsStatus_t      Status      = OsSuccess;

    // Special case
    if (SharedObject == NULL) {
        return HANDLE_GLOBAL;
    }

    Library = CollectionGetDataByKey(&LoadedLibraries, Key, 0);
    if (Library == NULL) {
        if (IsProcessModule()) {
            Status = Syscall_LibraryLoad(SharedObject, &Result);
        }
        else {
            struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetProcessService());
            svc_library_load(GetGrachtClient(), &msg, *GetInternalProcessId(),
                SharedObject, &Status, &Result);
            gracht_vali_message_finish(&msg);
        }

        if (Status == OsSuccess && Result != HANDLE_INVALID) {
            Library = (LibraryItem_t*)malloc(sizeof(LibraryItem_t));
            if (Library == NULL) {
                _set_errno(ENOMEM);
                return HANDLE_INVALID;
            }
            memset(Library, 0, sizeof(LibraryItem_t));
            
            Library->Header.Key.Value.Id = Key.Value.Id;
            Library->Handle              = Result;
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
    OsStatusToErrno(Status);
    return Result;
}

void*
SharedObjectGetFunction(
	_In_ Handle_t       Handle, 
	_In_ const char*    Function)
{
    OsStatus_t Status;
	if (Handle == HANDLE_INVALID || Function == NULL) {
	    _set_errno(EINVAL);
		return NULL;
	}

    if (IsProcessModule()) {
        return (void*)Syscall_LibraryFunction(Handle, Function);
    }
	else {
        struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetProcessService());
        uintptr_t AddressOfFunction;
        
        svc_library_get_function(GetGrachtClient(), &msg, *GetInternalProcessId(),
            Handle, Function, &Status, &AddressOfFunction);
        gracht_vali_message_finish(&msg);
        OsStatusToErrno(Status);
        if (Status != OsSuccess) {
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
    OsStatus_t      Status;

	if (Handle == HANDLE_INVALID) {
	    _set_errno(EINVAL);
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
                Status = Syscall_LibraryUnload(Handle);
            }
	        else {
	            struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetProcessService());
                svc_library_unload(GetGrachtClient(), &msg, *GetInternalProcessId(),
                    Handle, &Status);
                gracht_vali_message_finish(&msg);
            }
            OsStatusToErrno(Status);
            return Status;
        }
    }
	_set_errno(EFAULT);
    return OsError;
}
