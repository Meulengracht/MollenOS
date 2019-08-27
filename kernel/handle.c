/**
 * MollenOS
 *
 * Copyright 2018, Philip Meulengracht
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
 * Resource Handle Interface
 * - Implementation of the resource handle interface. This provides system-wide
 *   resource handles and maintience of resources. 
 */
#define __MODULE "HNDL"
//#define __TRACE

#include <arch/thread.h>
#include <assert.h>
#include <debug.h>
#include <ds/array.h>
#include <handle.h>
#include <heap.h>
#include <scheduler.h>
#include <string.h>
#include <threading.h>

static Semaphore_t EventHandle   = SEMAPHORE_INIT(0, 1);
static Array_t*    SystemHandles = NULL;
static UUId_t      JanitorHandle = UUID_INVALID;

UUId_t
CreateHandle(
    _In_ SystemHandleType_t Type,
    _In_ Flags_t            Flags,
    _In_ HandleDestructorFn Destructor,
    _In_ void*              Resource)
{
    SystemHandle_t* Handle = (SystemHandle_t*)kmalloc(sizeof(SystemHandle_t));
    UUId_t          Id;
    
    Handle->Type       = Type;
    Handle->Flags      = Flags;
    Handle->Resource   = Resource;
    Handle->Destructor = Destructor;
    Handle->Path       = NULL;
    atomic_store_explicit(&Handle->References, 1, memory_order_relaxed);
    Id = ArrayAppend(SystemHandles, Handle);
    if (Id == UUID_INVALID) {
        kfree(Handle);
    }
    return Id;
}

void*
AcquireHandle(
    _In_ UUId_t Handle)
{
    SystemHandle_t* Instance = ARRAY_GET(SystemHandles, Handle);
    int             PreviousReferences;
    if (Instance == NULL) {
        return NULL;
    }

    PreviousReferences = atomic_fetch_add(&Instance->References, 1);
    if (PreviousReferences <= 0) {
        // Special case, to prevent race-conditioning. If the reference
        // count ever reach 0 this was called on cleanup.
        return NULL;
    }
    return Instance->Resource;
}

OsStatus_t
RegisterHandlePath(
    _In_ UUId_t      Handle,
    _In_ const char* Path)
{
    SystemHandle_t* Instance;
    UUId_t          ExistingHandle;
    
    if (!Path) {
        return OsInvalidParameters;
    }
    
    Instance = ARRAY_GET(SystemHandles, Handle);
    if (!Instance) {
        return OsDoesNotExist;
    }
    
    if (LookupHandleByPath(Path, &ExistingHandle) != OsDoesNotExist) {
        return OsExists;
    }
    
    Instance->Path = strdup(Path);
    return OsSuccess;
}

OsStatus_t
LookupHandleByPath(
    _In_  const char* Path,
    _Out_ UUId_t*     HandleOut)
{
    size_t i;
    
    for (i = 0; i < SystemHandles->Capacity; i++) {
        SystemHandle_t* Instance = (SystemHandle_t*)ARRAY_GET(SystemHandles, i);
        if (Instance && Instance->Path && !strcmp(Instance->Path, Path)) {
            return OsSuccess;
        }
    }
    return OsDoesNotExist;
}

void*
LookupHandle(
    _In_ UUId_t Handle)
{
    SystemHandle_t* Instance = ARRAY_GET(SystemHandles, Handle);
    if (Instance == NULL) {
        return NULL;
    }
    return Instance->Resource;
}

void*
LookupHandleOfType(
    _In_ UUId_t             Handle,
    _In_ SystemHandleType_t Type)
{
    SystemHandle_t* Instance = ARRAY_GET(SystemHandles, Handle);
    if (Instance == NULL || Instance->Type != Type) {
        return NULL;
    }
    return Instance->Resource;
}

void
EnumerateHandlesOfType(
    _In_ SystemHandleType_t Type,
    _In_ void               (*Fn)(void*, void*),
    _In_ void*              Context)
{
    size_t i;
    
    for (i = 0; i < SystemHandles->Capacity; i++) {
        SystemHandle_t* Instance = (SystemHandle_t*)ARRAY_GET(SystemHandles, i);
        if (Instance->Type == Type) {
            Fn(Instance->Resource, Context);
        }
    }
}

void
DestroyHandle(
    _In_ UUId_t Handle)
{
    SystemHandle_t* Instance = ARRAY_GET(SystemHandles, Handle);
    int             References;

    if (Instance == NULL) {
        return;
    }

    References = atomic_fetch_sub(&Instance->References, 1) - 1;
    if (References == 0) {
        Instance->Flags |= HandleCleanup;
        SemaphoreSignal(&EventHandle, 1);
    }
}

static void 
HandleJanitorThread(
    _In_Opt_ void* Args)
{
    SystemHandle_t* Instance;
    int             Run = 1;
    size_t          i;
    _CRT_UNUSED(Args);
    
    while (Run) {
        SemaphoreWait(&EventHandle, 0);
        for (i = 0; i < SystemHandles->Capacity; i++) {
            Instance = (SystemHandle_t*)ARRAY_GET(SystemHandles, i);
            if (Instance->Flags & HandleCleanup) {
                ArrayRemove(SystemHandles, i);
                if (Instance->Destructor) {
                    Instance->Destructor(Instance->Resource);
                }
                if (Instance->Path) {
                    kfree((void*)Instance->Path);
                }
                kfree(Instance);
            }
        }
    }
}

OsStatus_t
InitializeHandles(void)
{
    return ArrayCreate(ARRAY_CAN_EXPAND, 128, &SystemHandles);
}

OsStatus_t
InitializeHandleJanitor(void)
{
    return CreateThread("janitor", HandleJanitorThread, NULL, 0, UUID_INVALID, &JanitorHandle);
}
