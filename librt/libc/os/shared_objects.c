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
#include <ds/hashtable.h>
#include <ds/mstring.h>
#include <errno.h>
#include <internal/_ipc.h>
#include <internal/_syscalls.h>
#include <internal/_utils.h>
#include <os/mollenos.h>
#include <os/sharedobject.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <threads.h>

typedef struct library_element {
    const char* path;
    atomic_int* references;
    Handle_t    handle;
};

static uint64_t so_hash(const void* element);
static int      so_cmp(const void* lh, const void* rh);

typedef void (*SOInitializer_t)(int);

static hashtable_t libraries;
static mtx_t       librariesLock;

void
StdSoInitialize(void)
{
    hashtable_construct(&libraries, 0, sizeof(struct library_element), so_hash, so_cmp);
    mtx_init(&librariesLock);
}

Handle_t 
SharedObjectLoad(
	_In_ const char* SharedObject)
{
    SOInitializer_t          Initializer;
    struct library_element*  library;
    Handle_t                 handle = HANDLE_INVALID;
    OsStatus_t               Status = OsSuccess;

    // Special case
    if (SharedObject == NULL) {
        return HANDLE_GLOBAL;
    }

    mtx_lock(&librariesLock);
    library = hashtable_get(&libraries, &(struct library_element) { .path = SharedObject });
    if (library) {
        atomic_fetch_and_add(library->references, 1);
        handle = Library->Handle;
        mtx_unlock(&librariesLock);
        return handle;
    }

    if (IsProcessModule()) {
        Status = Syscall_LibraryLoad(SharedObject, &handle);
    }
    else {
        struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetProcessService());
        svc_library_load(GetGrachtClient(), &msg.base, *GetInternalProcessId(), SharedObject);
        gracht_client_wait_message(GetGrachtClient(), &msg.base, GetGrachtBuffer(), GRACHT_WAIT_BLOCK);
        svc_library_load_result(GetGrachtClient(), &msg.base, &Status, &handle);
    }

    if (Status == OsSuccess && handle != HANDLE_INVALID) {
        struct library_element element;
        element->references = (atomic_int*)malloc(sizeof(atomic_int));
        if (!element->references) {
            _set_errno(ENOMEM);
            return HANDLE_INVALID;
        }
        
        element.path = strdup(SharedObject);
        if (!element.path) {
            free(element->references);
            _set_errno(ENOMEM);
            return HANDLE_INVALID;
        }

        element.handle = handle;
        atomic_store(element.references, 1);
        hashtable_set(&libraries, &element);
        mtx_unlock(&librariesLock);
        
        // run initializer
        Initializer = (SOInitializer_t)SharedObjectGetFunction(handle, "__CrtLibraryEntry");
        if (Initializer != NULL) {
            Initializer(DLL_ACTION_INITIALIZE);
        }
    }
    else {
        mtx_unlock(&librariesLock);
    }

    OsStatusToErrno(Status);
    return handle;
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
        
        svc_library_get_function(GetGrachtClient(), &msg.base, *GetInternalProcessId(), Handle, Function);
        gracht_client_wait_message(GetGrachtClient(), &msg.base, GetGrachtBuffer(), GRACHT_WAIT_BLOCK);
        svc_library_get_function_result(GetGrachtClient(), &msg.base, &Status, &AddressOfFunction);
        OsStatusToErrno(Status);
        if (Status != OsSuccess) {
            return NULL;
        }
        return (void*)AddressOfFunction;
    }
}

struct so_enum_context {
    UUId_t                  handle;
    struct library_element* library;
};

OsStatus_t
SharedObjectUnload(
	_In_ Handle_t Handle)
{
    SOInitializer_t        Initialize = NULL;
    struct so_enum_context enumContext;
    int                    references;
    OsStatus_t             status = OsSuccess;

	if (Handle == HANDLE_INVALID) {
	    _set_errno(EINVAL);
		return OsError;
	}
    
    if (Handle == HANDLE_GLOBAL) {
        return OsSuccess;
    }

    enumContext.handle  = Handle;
    enumContext.library = NULL;

    mtx_lock(&librariesLock);
    hashtable_enumerate(&libraries, so_enumerate, &enumContext);
    if (!enumContext.library) {
        mtx_unlock(&librariesLock);
        errno = ENOENT;
        return OsDoesNotExist;
    }
    
    references = atomic_fetch_and_sub(enumContext.library->references, 1);
    if (references == 1) {
        // Run finalizer before unload
        Initialize = (SOInitializer_t)SharedObjectGetFunction(Handle, "__CrtLibraryEntry");
        if (Initialize != NULL) {
            Initialize(DLL_ACTION_FINALIZE);
        }
        
        if (IsProcessModule()) {
            status = Syscall_LibraryUnload(Handle);
        }
        else {
            struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetProcessService());
            svc_library_unload(GetGrachtClient(), &msg.base, *GetInternalProcessId(), Handle);
            gracht_client_wait_message(GetGrachtClient(), &msg.base, GetGrachtBuffer(), GRACHT_WAIT_BLOCK);
            svc_library_unload_result(GetGrachtClient(), &msg.base, &status);
        }
        OsStatusToErrno(status);
    }
    mtx_unlock(&librariesLock);
    return status;
}

static void so_enumerate(int index, const void* element, void* userContext)
{
    const struct library_element* library     = element;
    struct so_enum_context*       enumContext = usercontext;
    if (enumContext->handle == library->handle) {
        enumContext->library = (struct library_element*)library;
    }
}

static uint64_t so_hash(const void* element)
{
    const struct library_element* library = element;
	
    uint8_t* pointer = (uint8_t*)library->path;
	uint64_t hash    = 5381;
	int      character;
	if (!pointer) {
        return 0;
    }

	while ((character = tolower(*pointer++)) != 0)
		hash = ((hash << 5) + hash) + character; /* hash * 33 + c */
	return hash;
}

static int so_cmp(const void* element1, const void* element2)
{
    const struct library_element* library1 = element1;
    const struct library_element* library2 = element2;
    return strcasecmp(library1->path, library2->path);
}
