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
#include <debug.h>
#include <ds/array.h>
#include <futex.h>
#include <handle.h>
#include <heap.h>
#include <io_events.h>
#include <scheduler.h>
#include <string.h>
#include <threading.h>

typedef struct SystemHandle {
    CollectionItem_t   Header;
    SystemHandleType_t Type;
    const char*        Path;
    Flags_t            Flags;
    atomic_int         References;
    HandleDestructorFn Destructor;
    void*              Resource;
    
    Collection_t       Sets;
} SystemHandle_t;

static Semaphore_t     EventHandle    = SEMAPHORE_INIT(0, 1);
static Collection_t    CleanupHandles = COLLECTION_INIT(KeyId);
static Collection_t    SystemHandles  = COLLECTION_INIT(KeyId);
static UUId_t          JanitorHandle  = UUID_INVALID;
static _Atomic(UUId_t) HandleIdGen    = ATOMIC_VAR_INIT(0);

static inline SystemHandle_t*
LookupHandleInstance(
    _In_ UUId_t Handle)
{
    DataKey_t Key = { .Value.Id = Handle };
    return (SystemHandle_t*)CollectionGetNodeByKey(&SystemHandles, Key, 0);
}

static inline SystemHandle_t*
LookupSafeHandleInstance(
    _In_ UUId_t Handle)
{
    SystemHandle_t* Instance = LookupHandleInstance(Handle);
    if (!Instance || atomic_load(&Instance->References) <= 0) {
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
    SystemHandle_t* Handle;
    
    Handle = (SystemHandle_t*)kmalloc(sizeof(SystemHandle_t));
    if (!Handle) {
        return UUID_INVALID;
    }
    
    memset(Handle, 0, sizeof(SystemHandle_t));
    CollectionConstruct(&Handle->Sets, KeyId);
    Handle->Header.Key.Value.Id = atomic_fetch_add(&HandleIdGen, 1);
    Handle->Type                = Type;
    Handle->Resource            = Resource;
    Handle->Destructor          = Destructor;
    Handle->References          = ATOMIC_VAR_INIT(1);
    CollectionAppend(&SystemHandles, &Handle->Header);
    
    WARNING("[create_handle] => id %u", Handle->Header.Key.Value.Id);
    return Handle->Header.Key.Value.Id;
}

void*
AcquireHandle(
    _In_ UUId_t Handle)
{
    SystemHandle_t* Instance = AcquireHandleInstance(Handle);
    if (!Instance) {
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
    TRACE("RegisterHandlePath(%u, %s)", Handle, Path);
    
    if (!Path) {
        return OsInvalidParameters;
    }
    
    Instance = LookupSafeHandleInstance(Handle);
    if (!Instance) {
        return OsDoesNotExist;
    }
    
    if (LookupHandleByPath(Path, &ExistingHandle) != OsDoesNotExist) {
        return OsExists;
    }
    
    Instance->Path = strdup(Path);
    TRACE("... registered");
    return OsSuccess;
}

// This function iterates the array, not good... TODO find solution
// I want to avoid using a hashtable for this tho, or try to atleast
OsStatus_t
LookupHandleByPath(
    _In_  const char* Path,
    _Out_ UUId_t*     HandleOut)
{
    TRACE("LookupHandleByPath(%s)", Path);
    
    foreach(Node, &SystemHandles) {
        SystemHandle_t* Instance = (SystemHandle_t*)Node;
        if (Instance && Instance->Path && !strcmp(Instance->Path, Path)) {
            TRACE("... found");
            *HandleOut = Instance->Header.Key.Value.Id;
            return OsSuccess;
        }
    }
    TRACE("... not found");
    return OsDoesNotExist;
}

void*
LookupHandle(
    _In_ UUId_t Handle)
{
    SystemHandle_t* Instance = LookupSafeHandleInstance(Handle);
    if (!Instance) {
        return NULL;
    }
    return Instance->Resource;
}

void*
LookupHandleOfType(
    _In_ UUId_t             Handle,
    _In_ SystemHandleType_t Type)
{
    SystemHandle_t* Instance = LookupSafeHandleInstance(Handle);
    if (!Instance || Instance->Type != Type) {
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
    WARNING("[destroy_handle] => %u", Handle);

    References = atomic_fetch_sub(&Instance->References, 1) - 1;
    if (References == 0) {
        WARNING("[destroy_handle] cleaning up %u", Handle);
        CollectionRemoveByNode(&SystemHandles, &Instance->Header);
        CollectionAppend(&CleanupHandles, &Instance->Header);
        SemaphoreSignal(&EventHandle, 1);
    }
}

static void
HandleJanitorThread(
    _In_Opt_ void* Args)
{
    SystemHandle_t* Instance;
    int             Run = 1;
    _CRT_UNUSED(Args);
    
    while (Run) {
        SemaphoreWait(&EventHandle, 0);
        Instance = (SystemHandle_t*)CollectionPopFront(&CleanupHandles);
        while (Instance) {
            if (Instance->Destructor) {
                Instance->Destructor(Instance->Resource);
            }
            if (Instance->Path) {
                kfree((void*)Instance->Path);
            }
            kfree(Instance);
            Instance = (SystemHandle_t*)CollectionPopFront(&CleanupHandles);
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
typedef struct SystemHandleSet {
    _Atomic(int) Pending;
    Collection_t Events;
    RBTree_t     Handles;
    Flags_t      Flags;
} SystemHandleSet_t;

typedef struct SystemHandleEvent {
    CollectionItem_t           Header;
    _Atomic(int)               ActiveEvents;
    struct _SystemHandleEvent* Link;
    UUId_t                     Handle;
    void*                      Context;
    Flags_t                    Configuration;
} SystemHandleEvent_t;

typedef struct SystemHandleSetElement {
    CollectionItem_t    Header;
    SystemHandleSet_t*  Set;
    SystemHandleEvent_t Event;
} SystemHandleSetElement_t;

typedef struct SystemHandleItem {
    RBTreeItem_t              Header;
    SystemHandleSetElement_t* Element;
} SystemHandleItem_t;

static void
DestroyHandleSet(
    _In_ void* Resource)
{
    SystemHandleSet_t*  Set = Resource;
    SystemHandle_t*     Instance;
    SystemHandleItem_t* Item;
    
    do {
        Item = (SystemHandleItem_t*)RBTreeGetMinimum(&Set->Handles);
        if (!Item) {
            break;
        }
        (void)RBTreeRemove(&Set->Handles, Item->Header.Key);
        
        Instance = LookupHandleInstance(Item->Element->Event.Handle);
        if (Instance) {
            CollectionRemoveByNode(&Instance->Sets, &Item->Element->Header);
        }
        kfree(Item->Element);
        kfree(Item);
    } while (Item);
    kfree(Set);
}

UUId_t
CreateHandleSet(
    _In_  Flags_t Flags)
{
    SystemHandleSet_t* Set;
    UUId_t             Handle;
    
    Set = (SystemHandleSet_t*)kmalloc(sizeof(SystemHandleSet_t));
    if (!Set) {
        return UUID_INVALID;
    }
    
    CollectionConstruct(&Set->Events, KeyId);
    RBTreeConstruct(&Set->Handles, KeyId);
    Set->Pending = ATOMIC_VAR_INIT(0);
    Set->Flags   = Flags;
    
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
    SystemHandleItem_t*       Item;
    DataKey_t                 Key = { .Value.Id = Handle };
    
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
        //SetElement->Header
        SetElement->Set = Set;
        //SetElement->Event.Header
        SetElement->Event.Handle = Handle;
        SetElement->Event.Context = Context;
        SetElement->Event.Configuration = Flags;
        
        CollectionAppend(&Instance->Sets, &SetElement->Header);
        if (Flags & IOEVTFRT) {
            CollectionAppend(&Set->Events, &SetElement->Event.Header);
            atomic_fetch_add(&Set->Pending, 1);
        }
        
        // For each handle added we must allocate a handle-wrapper and add it to
        // the handles tree
        Item = (SystemHandleItem_t*)kmalloc(sizeof(SystemHandleItem_t));
        Item->Header.Key = Key;
        Item->Element    = SetElement;
        if (RBTreeAppend(&Set->Handles, &Item->Header) != OsSuccess) {
            ERROR("... failed to append handle to list of handles, it exists?");
            CollectionRemoveByNode(&Instance->Sets, &SetElement->Header);
            DestroyHandle(Handle);
            kfree(SetElement);
            kfree(Item);
            return OsError;
        }
    }
    else if (Operation == IO_EVT_DESCRIPTOR_MOD) {
        Item = (SystemHandleItem_t*)RBTreeLookupKey(&Set->Handles, Key);
        if (!Item) {
            return OsDoesNotExist;
        }
        
        Item->Element->Event.Configuration = Flags;
        Item->Element->Event.Context       = Context;
    }
    else if (Operation == IO_EVT_DESCRIPTOR_DEL) {
        Item = (SystemHandleItem_t*)RBTreeRemove(&Set->Handles, Key);
        if (!Item) {
            return OsDoesNotExist;
        }
        
        Instance = LookupHandleInstance(Handle);
        if (!Instance) {
            return OsDoesNotExist;
        }
        
        CollectionRemoveByNode(&Instance->Sets, &Item->Element->Header);
        DestroyHandle(Handle);
        kfree(Item->Element);
        kfree(Item);
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
    SystemHandleEvent_t* Head;
    int                  NumberOfEvents;
    
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
    
    NumberOfEvents = MIN(NumberOfEvents, MaxEvents);
    Head = (SystemHandleEvent_t*)CollectionSplice(&Set->Events, NumberOfEvents);
    
    while (Head) {
        Event->handle  = Head->Handle;
        Event->events  = atomic_exchange(&Head->ActiveEvents, 0);
        Event->context = Head->Context;
        
        // Handle level triggered here, by adding them back to ready list
        // TODO: is this behaviour correct?
        if (!(Head->Configuration & IOEVTET)) {
            
        }
        
        Head = (SystemHandleEvent_t*)CollectionNext(&Head->Header);
        Event++;
    }
    *NumberOfEventsOut = NumberOfEvents;
    return OsSuccess;
}

OsStatus_t
MarkHandle(
    _In_ UUId_t  Handle,
    _In_ Flags_t Flags)
{
    SystemHandle_t*           Instance = LookupHandleInstance(Handle);
    SystemHandleSetElement_t* SetElement;
    if (!Instance) {
        return OsDoesNotExist;
    }
    
    SetElement = (SystemHandleSetElement_t*)CollectionBegin(&Instance->Sets);
    while (SetElement) {
        if (SetElement->Event.Configuration & Flags) {
            if (!atomic_fetch_or(&SetElement->Event.ActiveEvents, (int)Flags)) {
                CollectionAppend(&SetElement->Set->Events, &SetElement->Event.Header);
                if (!atomic_fetch_add(&SetElement->Set->Pending, 1)) {
                    (void)FutexWake(&SetElement->Set->Pending, 1, 0);
                }
            }
        }
        SetElement = (SystemHandleSetElement_t*)CollectionNext(&SetElement->Header);
    }
    return OsSuccess;
}
