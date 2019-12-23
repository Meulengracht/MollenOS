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
#include <ddk/handle.h>
#include <ddk/barrier.h>
#include <ds/list.h>
#include <ds/queue.h>
#include <ds/rbtree.h>
#include <debug.h>
#include <futex.h>
#include <handle.h>
#include <heap.h>
#include <io_events.h>
#include <scheduler.h>
#include <string.h>
#include <threading.h>

typedef struct SystemHandle {
    void*              Resource;
    atomic_int         References;
    SystemHandleType_t Type;
    Flags_t            Flags;
    HandleDestructorFn Destructor;
    
    element_t*         PathHeader;
    element_t          Header;
    list_t             Sets;
} SystemHandle_t;

static Semaphore_t     EventHandle    = SEMAPHORE_INIT(0, 1);
static queue_t         CleanQueue     = QUEUE_INIT;
static list_t          SystemHandles  = LIST_INIT;                      // TODO: hashtable
static list_t          PathRegister   = LIST_INIT_CMP(list_cmp_string); // TODO: hashtable
static UUId_t          JanitorHandle  = UUID_INVALID;
static _Atomic(UUId_t) HandleIdGen    = ATOMIC_VAR_INIT(0);

static inline SystemHandle_t*
LookupHandleInstance(
    _In_ UUId_t Handle)
{
    return (SystemHandle_t*)list_find_value(&SystemHandles, (void*)(uintptr_t)Handle);
}

static inline SystemHandle_t*
LookupSafeHandleInstance(
    _In_ UUId_t Handle)
{
    SystemHandle_t* Instance = LookupHandleInstance(Handle);
    int             References;
    if (!Instance) {
        return NULL;
    }
    
    References = atomic_load(&Instance->References);
    if (References <= 0) {
        return NULL;
    }
    return Instance;
}

static SystemHandle_t*
AcquireHandleInstance(
    _In_ UUId_t Handle)
{
    SystemHandle_t* Instance = LookupHandleInstance(Handle);
    int             PreviousReferences;
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
    _In_ SystemHandleType_t Type,
    _In_ HandleDestructorFn Destructor,
    _In_ void*              Resource)
{
    SystemHandle_t* Instance;
    UUId_t          HandleId;
    
    Instance = (SystemHandle_t*)kmalloc(sizeof(SystemHandle_t));
    if (!Instance) {
        return UUID_INVALID;
    }
    
    HandleId = atomic_fetch_add(&HandleIdGen, 1);
    memset(Instance, 0, sizeof(SystemHandle_t));
    
    list_construct(&Instance->Sets);
    ELEMENT_INIT(&Instance->Header, HandleId, Instance);
    Instance->Type       = Type;
    Instance->Resource   = Resource;
    Instance->Destructor = Destructor;
    Instance->References = ATOMIC_VAR_INIT(1);
    smp_wmb();
    
    list_append(&SystemHandles, &Instance->Header);
    
    TRACE("[create_handle] => id %u", HandleId);
    return HandleId;
}

void*
AcquireHandle(
    _In_ UUId_t Handle)
{
    SystemHandle_t* Instance = AcquireHandleInstance(Handle);
    if (!Instance) {
        return NULL;
    }
    
    smp_rmb();
    return Instance->Resource;
}

OsStatus_t
RegisterHandlePath(
    _In_ UUId_t      Handle,
    _In_ const char* Path)
{
    SystemHandle_t* Instance;
    UUId_t          ExistingHandle;
    char*           PathKey;
    OsStatus_t      Status;
    WARNING("[handle_register_path] %u => %s", Handle, Path);
    
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

    WARNING("[handle_register_path] registered");
    return OsSuccess;
}

OsStatus_t
LookupHandleByPath(
    _In_  const char* Path,
    _Out_ UUId_t*     HandleOut)
{
    SystemHandle_t* Instance;
    TRACE("[handle_lookup_by_path] %s", Path);
    
    Instance = list_find_value(&PathRegister, (void*)Path);
    if (!Instance) {
        WARNING("[handle_lookup_by_path] not found");
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
    SystemHandle_t* Instance = LookupSafeHandleInstance(Handle);
    if (!Instance) {
        return NULL;
    }
    
    smp_rmb();
    return Instance->Resource;
}

void*
LookupHandleOfType(
    _In_ UUId_t             Handle,
    _In_ SystemHandleType_t Type)
{
    SystemHandle_t* Instance = LookupSafeHandleInstance(Handle);
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
    SystemHandle_t* Instance = LookupSafeHandleInstance(Handle);
    int             References;
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
        
        list_remove(&SystemHandles, &Instance->Header);
        queue_push(&CleanQueue, &Instance->Header);
        SemaphoreSignal(&EventHandle, 1);
    }
}

static void
HandleJanitorThread(
    _In_Opt_ void* Args)
{
    element_t*      Element;
    SystemHandle_t* Instance;
    int             Run = 1;
    _CRT_UNUSED(Args);
    
    while (Run) {
        SemaphoreWait(&EventHandle, 0);
        
        Element = queue_pop(&CleanQueue);
        while (Element) {
            smp_rmb();
            Instance = (SystemHandle_t*)Element->value;
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

///////////////////////////////////////////////////////////////////////////////
/// Handle Sets implementation, a bit coupled to io_events.h as it implements
/// the neccessary functionality to listen to multiple handles
///////////////////////////////////////////////////////////////////////////////

// The HandleSet is the set that is created and contains a list of handles registered
// with the set (HandleItems), and also contains a list of registered events
typedef struct SystemHandleSet {
    _Atomic(int) Pending;
    list_t       Events;
    rb_tree_t    Handles;
    Flags_t      Flags;
} SystemHandleSet_t;

typedef struct SystemHandleSetElement {
    element_t           SetHeader;
    rb_leaf_t           HandleHeader;
    element_t           EventHeader;
    SystemHandleSet_t*  Set;
    
    // Event data
    _Atomic(int)                   ActiveEvents;
    struct SystemHandleSetElement* Link;
    UUId_t                         Handle;
    void*                          Context;
    Flags_t                        Configuration;
} SystemHandleSetElement_t;

static void
DestroyHandleSet(
    _In_ void* Resource)
{
    SystemHandleSet_t*        Set = Resource;
    SystemHandleSetElement_t* Element;
    SystemHandle_t*           Instance;
    rb_leaf_t*                Leaf;
    WARNING("[handle_set] [destroy]");
    
    do {
        Leaf = rb_tree_minimum(&Set->Handles);
        if (!Leaf) {
            break;
        }
        
        smp_rmb();
        rb_tree_remove(&Set->Handles, Leaf->key);
        
        Element  = Leaf->value;
        Instance = LookupHandleInstance(Element->Handle);
        if (Instance) {
            list_remove(&Instance->Sets, &Element->SetHeader);
        }
        kfree(Element);
    } while (Leaf);
    kfree(Set);
}

UUId_t
CreateHandleSet(
    _In_  Flags_t Flags)
{
    SystemHandleSet_t* Set;
    UUId_t             Handle;
    WARNING("[handle_set] [create] 0x%x", Flags);
    
    Set = (SystemHandleSet_t*)kmalloc(sizeof(SystemHandleSet_t));
    if (!Set) {
        return UUID_INVALID;
    }
    
    list_construct(&Set->Events);
    rb_tree_construct(&Set->Handles);
    Set->Pending = ATOMIC_VAR_INIT(0);
    Set->Flags   = Flags;
    
    // CreateHandle implies a write memory barrier
    Handle = CreateHandle(HandleTypeSet, DestroyHandleSet, Set);
    if (Handle == UUID_INVALID) {
        kfree(Set);
    }
    return Handle;
}

OsStatus_t
ControlHandleSet(
    _In_ UUId_t  SetHandle,
    _In_ int     Operation,
    _In_ UUId_t  Handle,
    _In_ Flags_t Flags,
    _In_ void*   Context)
{
    SystemHandle_t*           Instance;
    SystemHandleSet_t*        Set = LookupHandleOfType(SetHandle, HandleTypeSet);
    SystemHandleSetElement_t* SetElement;
    WARNING("[handle_set] [control] %u, %i, %u", 
        LODWORD(SetHandle), Operation, LODWORD(Handle));
    
    if (!Set) {
        return OsDoesNotExist;
    }
    
    if (Operation == IO_EVT_DESCRIPTOR_ADD) {
        Instance = AcquireHandleInstance(Handle);
        if (!Instance) {
            return OsDoesNotExist;
        }
        // Now we have access to the handle-set and the target handle, so we can go ahead
        // and add the target handle to the set-tree and then create the set element for
        // the handle
        
        // For each handle added we must allocate a SetElement and add it to the
        // handle instance
        SetElement = (SystemHandleSetElement_t*)kmalloc(sizeof(SystemHandleSetElement_t));
        if (!SetElement) {
            DestroyHandle(Handle);
            return OsOutOfMemory;
        }
        
        memset(SetElement, 0, sizeof(SystemHandleSetElement_t));
        ELEMENT_INIT(&SetElement->SetHeader, 0, SetElement);
        ELEMENT_INIT(&SetElement->EventHeader, 0, SetElement);
        RB_LEAF_INIT(&SetElement->HandleHeader, Handle, SetElement);
        
        SetElement->Set = Set;
        
        SetElement->Handle = Handle;
        SetElement->Context = Context;
        SetElement->Configuration = Flags;
        smp_mb();
        
        // Append to the list of sets on the target handle we are going to listen
        // too. 
        list_append(&Instance->Sets, &SetElement->SetHeader);
        if (Flags & IOEVTFRT) {
            // Should we register an initial event?
            int PreviousPending;
            list_append(&Set->Events, &SetElement->EventHeader);
            PreviousPending = atomic_fetch_add(&Set->Pending, 1);
        }
        
        // Register the target handle in the current set, so we can clean up again
        if (rb_tree_append(&Set->Handles, &SetElement->HandleHeader) != OsSuccess) {
            ERROR("... failed to append handle to list of handles, it exists?");
            list_remove(&Instance->Sets, &SetElement->SetHeader);
            DestroyHandle(Handle);
            kfree(SetElement);
            return OsError;
        }
    }
    else if (Operation == IO_EVT_DESCRIPTOR_MOD) {
        rb_leaf_t* Leaf = rb_tree_lookup(&Set->Handles, (void*)Handle);
        if (!Leaf) {
            return OsDoesNotExist;
        }
        
        smp_rmb();
        SetElement = Leaf->value;
        SetElement->Configuration = Flags;
        SetElement->Context       = Context;
    }
    else if (Operation == IO_EVT_DESCRIPTOR_DEL) {
        rb_leaf_t* Leaf = rb_tree_lookup(&Set->Handles, (void*)Handle);
        if (!Leaf) {
            return OsDoesNotExist;
        }
        
        Instance = LookupHandleInstance(Handle);
        if (!Instance) {
            return OsDoesNotExist;
        }
        
        smp_rmb();
        SetElement = Leaf->value;
        list_remove(&Instance->Sets, &SetElement->SetHeader);
        DestroyHandle(Handle);
        kfree(SetElement);
    }
    else {
        return OsInvalidParameters;
    }
    return OsSuccess;
}

OsStatus_t
WaitForHandleSet(
    _In_  UUId_t          Handle,
    _In_  handle_event_t* Events,
    _In_  int             MaxEvents,
    _In_  size_t          Timeout,
    _Out_ int*            NumberOfEventsOut)
{
    SystemHandleSet_t*   Set = LookupHandleOfType(Handle, HandleTypeSet);
    handle_event_t*      Event = Events;
    int                  NumberOfEvents;
    list_t               Spliced;
    element_t*           i;
    WARNING("[handle_set] [wait] %u, %i, %" PRIuIN, 
        LODWORD(Handle), MaxEvents, Timeout);
    
    if (!Set) {
        return OsDoesNotExist;
    }
    
    // Wait for response by 'polling' the value
    NumberOfEvents = atomic_exchange(&Set->Pending, 0);
    while (!NumberOfEvents) {
        if (FutexWait(&Set->Pending, NumberOfEvents, 0, Timeout) == OsTimeout) {
            return OsTimeout;
        }
        NumberOfEvents = atomic_exchange(&Set->Pending, 0);
    }
    
    WARNING("[handle_set] [wait] wake");
    
    list_construct(&Spliced);
    NumberOfEvents = MIN(NumberOfEvents, MaxEvents);
    list_splice(&Set->Events, NumberOfEvents, &Spliced);
    
    smp_rmb();
    WARNING("[handle_set] [wait] num events %i", NumberOfEvents);
    _foreach(i, &Spliced) {
        SystemHandleSetElement_t* Element = i->value;
        
        Event->events  = atomic_exchange(&Element->ActiveEvents, 0);
        Event->handle  = Element->Handle;
        Event->context = Element->Context;
        
        // Handle level triggered here, by adding them back to ready list
        // TODO: is this behaviour correct?
        if (!(Element->Configuration & IOEVTET)) {
            
        }
        Event++;
    }
    *NumberOfEventsOut = NumberOfEvents;
    return OsSuccess;
}

static int
MarkHandleCallback(
    _In_ int        Index,
    _In_ element_t* Element,
    _In_ void*      Context)
{
    SystemHandleSetElement_t* SetElement = Element->value;
    Flags_t                   Flags      = (Flags_t)Context;
    WARNING("[handle_set] [mark_cb] 0x%x", SetElement->Configuration);
    
    if (SetElement->Configuration & Flags) {
        int Previous;
        Previous = atomic_fetch_or(&SetElement->ActiveEvents, (int)Flags);
        WARNING("[handle_set] [mark_cb] previous %i", Previous);
        if (!Previous) {
            list_append(&SetElement->Set->Events, &SetElement->EventHeader);
            
            Previous = atomic_fetch_add(&SetElement->Set->Pending, 1);
            WARNING("[handle_set] [mark_cb] pending %i", Previous);
            if (!Previous) {
                WARNING("[handle_set] [mark_cb] wake");
                (void)FutexWake(&SetElement->Set->Pending, 1, 0);
            }
        }
    }
    return LIST_ENUMERATE_CONTINUE;
}

OsStatus_t
MarkHandle(
    _In_ UUId_t  Handle,
    _In_ Flags_t Flags)
{
    SystemHandle_t* Instance = LookupHandleInstance(Handle);
    if (!Instance) {
        return OsDoesNotExist;
    }
    WARNING("[handle_set] [mark] handle %u - 0x%x", LODWORD(Handle), Flags);
    
    list_enumerate(&Instance->Sets, MarkHandleCallback, (void*)(uintptr_t)Flags);
    return OsSuccess;
}
