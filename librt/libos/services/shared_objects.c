/**
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <ctype.h>
#include <ds/hashtable.h>
#include <errno.h>
#include <internal/_utils.h>
#include <os/once.h>
#include "os/services/sharedobject.h"
#include <strings.h>

#include <ddk/service.h>
#include <gracht/link/vali.h>
#include <sys_library_service_client.h>

struct library_element {
    const char* path;
    atomic_int* references;
    Handle_t    handle;
    uintptr_t   entryAddress;
};

static uint64_t so_hash(const void* element);
static int      so_cmp(const void* element1, const void* element2);
static void     so_enumerate(int index, const void* element, void* userContext);

typedef void (*SOInitializer_t)(int);

static hashtable_t g_libraries;
static Mutex_t     g_librariesLock = MUTEX_INIT(MUTEX_PLAIN);
static OnceFlag_t  g_initCalled    = ONCE_FLAG_INIT;
static bool        g_initialized   = false;

static void __InitializeSO(void)
{
    hashtable_construct(
            &g_libraries,
            0,
            sizeof(struct library_element),
            so_hash, so_cmp
    );
    g_initialized = true;
}

Handle_t 
OSLibraryLoad(
	_In_ const char* SharedObject)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetProcessService());
    SOInitializer_t          Initializer;
    struct library_element*  library;
    Handle_t                 handle = HANDLE_INVALID;
    oserr_t                  oserr  = OS_EOK;
    uintptr_t                entryAddress;

    // Special case
    if (SharedObject == NULL) {
        return HANDLE_GLOBAL;
    }

    assert(__crt_is_phoenix() == 0);

    // Make sure the SO system is initialized. We do this on the first call to load
    // as there is no reason to this before.
    if (!g_initialized) {
        CallOnce(&g_initCalled, __InitializeSO);
    }

    MutexLock(&g_librariesLock);
    library = hashtable_get(&g_libraries, &(struct library_element) { .path = SharedObject });
    if (library) {
        atomic_fetch_add(library->references, 1);
        handle = library->handle;
        MutexUnlock(&g_librariesLock);
        return handle;
    }

    sys_library_load(GetGrachtClient(), &msg.base, __crt_process_id(), SharedObject);
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_library_load_result(GetGrachtClient(), &msg.base, &oserr, (uintptr_t*)&handle, &entryAddress);

    if (oserr == OS_EOK && handle != HANDLE_INVALID) {
        struct library_element element;
        element.references = (atomic_int*)malloc(sizeof(atomic_int));
        if (!element.references) {
            _set_errno(ENOMEM);
            return HANDLE_INVALID;
        }
        
        element.path = strdup(SharedObject);
        if (!element.path) {
            free(element.references);
            _set_errno(ENOMEM);
            return HANDLE_INVALID;
        }

        element.handle = handle;
        element.entryAddress = entryAddress;
        atomic_store(element.references, 1);
        hashtable_set(&g_libraries, &element);
        MutexUnlock(&g_librariesLock);
        
        // run initializer
        Initializer = (SOInitializer_t)(void*)entryAddress;
        if (Initializer != NULL) {
            Initializer(DLL_ACTION_INITIALIZE);
        }
    }
    else {
        MutexUnlock(&g_librariesLock);
    }

    OsErrToErrNo(oserr);
    return handle;
}

void*
OSLibraryLookupFunction(
	_In_ Handle_t       handle,
	_In_ const char*    function)
{
    oserr_t oserr;

    if (handle == HANDLE_INVALID || function == NULL) {
	    _set_errno(EINVAL);
		return NULL;
	}

    if (!g_initialized) {
        errno = ENOSYS;
        return NULL;
    }

    assert(__crt_is_phoenix() == 0);

    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetProcessService());
    uintptr_t                functionAddress;

    sys_library_get_function(GetGrachtClient(), &msg.base, __crt_process_id(), (uintptr_t)handle, function);
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_library_get_function_result(GetGrachtClient(), &msg.base, &oserr, &functionAddress);
    OsErrToErrNo(oserr);
    if (oserr != OS_EOK) {
        return NULL;
    }
    return (void*)functionAddress;
}

struct so_enum_context {
    Handle_t                handle;
    struct library_element* library;
};

oserr_t
OSLibraryUnload(
	_In_ Handle_t handle)
{
    SOInitializer_t        initialize = NULL;
    struct so_enum_context enumContext;
    int                    references;
    oserr_t                status = OS_EOK;

	if (handle == HANDLE_INVALID) {
	    _set_errno(EINVAL);
		return OS_EUNKNOWN;
	}
    
    if (handle == HANDLE_GLOBAL) {
        return OS_EOK;
    }

    if (!g_initialized) {
        errno = ENOSYS;
        return OS_ENOTSUPPORTED;
    }

    assert(__crt_is_phoenix() == 0);

    enumContext.handle  = handle;
    enumContext.library = NULL;

    MutexLock(&g_librariesLock);
    hashtable_enumerate(&g_libraries, so_enumerate, &enumContext);
    if (!enumContext.library) {
        MutexUnlock(&g_librariesLock);
        errno = ENOENT;
        return OS_ENOENT;
    }
    
    references = atomic_fetch_sub(enumContext.library->references, 1);
    if (references == 1) {
        // Run finalizer before unload
        initialize = (SOInitializer_t)(void*)enumContext.library->entryAddress;
        if (initialize != NULL) {
            initialize(DLL_ACTION_FINALIZE);
        }

        struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetProcessService());
        sys_library_unload(GetGrachtClient(), &msg.base, __crt_process_id(), (uintptr_t)handle);
        gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
        sys_library_unload_result(GetGrachtClient(), &msg.base, &status);
        OsErrToErrNo(status);
    }
    MutexUnlock(&g_librariesLock);
    return status;
}

static void so_enumerate(int index, const void* element, void* userContext)
{
    const struct library_element* library     = element;
    struct so_enum_context*       enumContext = userContext;
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

	while ((character = tolower(*pointer)) != 0) {
        hash = ((hash << 5) + hash) + character; /* hash * 33 + c */
	    pointer++;
	}
	return hash;
}

static int so_cmp(const void* element1, const void* element2)
{
    const struct library_element* library1 = element1;
    const struct library_element* library2 = element2;
    return strcasecmp(library1->path, library2->path);
}
