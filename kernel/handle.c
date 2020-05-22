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

#define __MODULE "handle"
//#define __TRACE

#include <ddk/barrier.h>
#include <ds/list.h>
#include <ds/queue.h>
#include <debug.h>
#include <handle.h>
#include <heap.h>
#include <threading.h>
#include <string.h>

typedef struct ResourceHandle {
    void*              Resource;
    atomic_int         References;
    HandleType_t       Type;
    Flags_t            Flags;
    HandleDestructorFn Destructor;
    element_t*         PathHeader;
    element_t          Header;
} ResourceHandle_t;

static Semaphore_t     EventHandle   = SEMAPHORE_INIT(0, 1);
static queue_t         CleanQueue    = QUEUE_INIT;
static list_t          Handles       = LIST_INIT;                      // TODO: hashtable
static list_t          PathRegister  = LIST_INIT_CMP(list_cmp_string); // TODO: hashtable
static UUId_t          JanitorHandle = UUID_INVALID;
static _Atomic(UUId_t) HandleIdGen   = ATOMIC_VAR_INIT(1); // 0 is reserved for invalid

static inline ResourceHandle_t*
LookupHandleInstance(
    _In_ UUId_t Handle)
{
    return (ResourceHandle_t*)list_find_value(&Handles, (void*)(uintptr_t)Handle);
}

static inline ResourceHandle_t*
LookupSafeHandleInstance(
    _In_ UUId_t Handle)
{
    ResourceHandle_t* Instance = LookupHandleInstance(Handle);
    int               References;
    if (!Instance) {
        return NULL;
    }
    
    References = atomic_load(&Instance->References);
    if (References <= 0) {
        return NULL;
    }
    return Instance;
}

static ResourceHandle_t*
AcquireHandleInstance(
    _In_ UUId_t Handle)
{
    ResourceHandle_t* Instance = LookupHandleInstance(Handle);
    int               PreviousReferences;
    if (!Instance) {
        WARNING("[acquire_handle] failed to find %u");
        return NULL;
    }

    PreviousReferences = atomic_fetch_add(&Instance->References, 1);
    if (PreviousReferences <= 0) {
        // Special case, to prevent race-conditioning. If the reference
        // count ever reach 0 this was called on cleanup.
        WARNING("[acquire_handle] handle was destroyed %u: %i",
            Handle, PreviousReferences);
        return NULL;
    }
    return Instance;
}

UUId_t
CreateHandle(
    _In_ HandleType_t Type,
    _In_ HandleDestructorFn Destructor,
    _In_ void*              Resource)
{
    ResourceHandle_t* Instance;
    UUId_t            HandleId;
    
    Instance = (ResourceHandle_t*)kmalloc(sizeof(ResourceHandle_t));
    if (!Instance) {
        return UUID_INVALID;
    }
    
    HandleId = atomic_fetch_add(&HandleIdGen, 1);
    memset(Instance, 0, sizeof(ResourceHandle_t));
    
    ELEMENT_INIT(&Instance->Header, (uintptr_t)HandleId, Instance);
    Instance->Type       = Type;
    Instance->Resource   = Resource;
    Instance->Destructor = Destructor;
    Instance->References = ATOMIC_VAR_INIT(1);
    smp_wmb();
    
    list_append(&Handles, &Instance->Header);
    
    TRACE("[create_handle] => id %u", HandleId);
    return HandleId;
}

OsStatus_t
AcquireHandle(
    _In_  UUId_t Handle,
    _Out_ void** ResourceOut)
{
    ResourceHandle_t* Instance = AcquireHandleInstance(Handle);
    if (!Instance) {
        return OsDoesNotExist;
    }
    
    if (ResourceOut) {
        smp_rmb();
        *ResourceOut = Instance->Resource;
    }
    return OsSuccess;
}

OsStatus_t
RegisterHandlePath(
    _In_ UUId_t      Handle,
    _In_ const char* Path)
{
    ResourceHandle_t* Instance;
    UUId_t            ExistingHandle;
    char*             PathKey;
    OsStatus_t        Status;
    TRACE("[handle_register_path] %u => %s", Handle, Path);
    
    if (!Path) {
        ERROR("[handle_register_path] path invalid");
        return OsInvalidParameters;
    }
    
    Instance = LookupSafeHandleInstance(Handle);
    if (!Instance) {
        ERROR("[handle_register_path] handle did not exist");
        return OsDoesNotExist;
    }
    
    Status = LookupHandleByPath(Path, &ExistingHandle);
    if (Instance->PathHeader || Status != OsDoesNotExist) {
        ERROR("[handle_register_path] path already registered [%u]", Status);
        return OsExists;
    }
    
    Instance->PathHeader = kmalloc(sizeof(element_t));
    if (!Instance->PathHeader) {
        return OsOutOfMemory;
    }
    
    PathKey = strdup(Path);
    if (!PathKey) {
        kfree(Instance->PathHeader);
        Instance->PathHeader = NULL;
        return OsOutOfMemory;
    }
    
    ELEMENT_INIT(Instance->PathHeader, PathKey, Instance);
    smp_wmb();
    
    list_append(&PathRegister, Instance->PathHeader);
    return OsSuccess;
}

OsStatus_t
LookupHandleByPath(
    _In_  const char* Path,
    _Out_ UUId_t*     HandleOut)
{
    ResourceHandle_t* Instance;
    TRACE("[handle_lookup_by_path] %s", Path);
    
    Instance = list_find_value(&PathRegister, (void*)Path);
    if (!Instance) {
        WARNING("[handle_lookup_by_path] %s not found", Path);
        return OsDoesNotExist;
    }
    
    smp_rmb();
    *HandleOut = (UUId_t)(uintptr_t)Instance->Header.key;
    return OsSuccess;
}

void*
LookupHandle(
    _In_ UUId_t Handle)
{
    ResourceHandle_t* Instance = LookupSafeHandleInstance(Handle);
    if (!Instance) {
        return NULL;
    }
    
    smp_rmb();
    return Instance->Resource;
}

void*
LookupHandleOfType(
    _In_ UUId_t       Handle,
    _In_ HandleType_t Type)
{
    ResourceHandle_t* Instance = LookupSafeHandleInstance(Handle);
    if (!Instance) {
        return NULL;
    }
    
    smp_rmb();
    if (Instance->Type != Type) {
        return NULL;
    }
    return Instance->Resource;
}

void
DestroyHandle(
    _In_ UUId_t Handle)
{
    ResourceHandle_t* Instance = LookupSafeHandleInstance(Handle);
    int               References;
    if (!Instance) {
        return;
    }
    TRACE("[destroy_handle] => %u", Handle);

    References = atomic_fetch_sub(&Instance->References, 1);
    if ((References - 1) == 0) {
        TRACE("[destroy_handle] cleaning up %u", Handle);
        if (Instance->PathHeader) {
            list_remove(&PathRegister, Instance->PathHeader);
        }
        
        list_remove(&Handles, &Instance->Header);
        queue_push(&CleanQueue, &Instance->Header);
        SemaphoreSignal(&EventHandle, 1);
    }
}

static void
HandleJanitorThread(
    _In_Opt_ void* Args)
{
    element_t*        Element;
    ResourceHandle_t* Instance;
    int               Run = 1;
    _CRT_UNUSED(Args);
    
    while (Run) {
        SemaphoreWait(&EventHandle, 0);
        
        Element = queue_pop(&CleanQueue);
        while (Element) {
            smp_rmb();
            Instance = (ResourceHandle_t*)Element->value;
            if (Instance->Destructor) {
                Instance->Destructor(Instance->Resource);
            }
            if (Instance->PathHeader) {
                kfree((void*)Instance->PathHeader->key);
                kfree((void*)Instance->PathHeader);
            }
            kfree(Instance);
            
            Element = queue_pop(&CleanQueue);
        }
    }
}

OsStatus_t
InitializeHandles(void)
{
    return OsSuccess;
}

OsStatus_t
InitializeHandleJanitor(void)
{
    return CreateThread("janitor", HandleJanitorThread, NULL, 0, UUID_INVALID, &JanitorHandle);
}
