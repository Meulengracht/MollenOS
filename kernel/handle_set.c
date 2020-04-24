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

#define __MODULE "handle_set"
//#define __TRACE

#include <debug.h>
#include <ddk/barrier.h>
#include <ddk/handle.h>
#include <ds/list.h>
#include <ds/rbtree.h>
#include <futex.h>
#include <handle.h>
#include <heap.h>
#include <string.h>

#define VOID_KEY(key) (void*)(uintptr_t)key

// The HandleSet is the set that is created and contains a list of handles registered
// with the set (HandleItems), and also contains a list of registered events

typedef struct HandleElement {
    element_t Header;
    list_t    Sets;
} HandleElement_t;

typedef struct HandleSet {
    _Atomic(int) Pending;
    list_t       Events;
    rb_tree_t    Handles;
    Flags_t      Flags;
} HandleSet_t;

// A set element is a handle descriptor and an event descriptor
typedef struct HandleSetElement {
    element_t     SetHeader;    // This is the header in the Set
    rb_leaf_t     HandleHeader; // This is a handle header
    element_t     EventHeader;  // This is an event header
    HandleSet_t*  Set;          // This is a pointer back to the set it belongs
    
    // Event data
    UUId_t                   Handle;
    _Atomic(int)             ActiveEvents;
    struct HandleSetElement* Link;
    void*                    Context;
    Flags_t                  Configuration;
} HandleSetElement_t;

static OsStatus_t DestroySetElement(HandleSetElement_t*);
static OsStatus_t AddHandleToSet(HandleSet_t*, UUId_t, void*, Flags_t);

static list_t  HandleElements = LIST_INIT; // Sets per Handle TODO hashtable
//static Mutex_t HandleElementsSyncObject;

static void
DestroyHandleSet(
    _In_ void* Resource)
{
    HandleSet_t* Set = Resource;
    rb_leaf_t*   Leaf;
    WARNING("[handle_set] [destroy]");
    
    do {
        Leaf = rb_tree_minimum(&Set->Handles);
        if (!Leaf) {
            break;
        }
        
        rb_tree_remove(&Set->Handles, Leaf->key);
        DestroySetElement(Leaf->value);
    } while (Leaf);
    kfree(Set);
}

UUId_t
CreateHandleSet(
    _In_  Flags_t Flags)
{
    HandleSet_t* Set;
    UUId_t       Handle;
    WARNING("[handle_set] [create] 0x%x", Flags);
    
    Set = (HandleSet_t*)kmalloc(sizeof(HandleSet_t));
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
    _In_ Flags_t Configuration,
    _In_ void*   Context)
{
    HandleSet_t*        Set = LookupHandleOfType(SetHandle, HandleTypeSet);
    HandleSetElement_t* SetElement;
    OsStatus_t          Status;
    WARNING("[handle_set] [control] %u, %i, %u, 0x%x", 
        SetHandle, Operation, Handle, Configuration);
    
    if (!Set) {
        return OsDoesNotExist;
    }
    
    if (Operation == HANDLE_SET_OP_ADD) {
        Status = AddHandleToSet(Set, Handle, Context, Configuration);
    }
    else if (Operation == HANDLE_SET_OP_MOD) {
        rb_leaf_t* Leaf = rb_tree_lookup(&Set->Handles, VOID_KEY(Handle));
        if (!Leaf) {
            return OsDoesNotExist;
        }
        
        SetElement                = Leaf->value;
        SetElement->Configuration = Configuration;
        SetElement->Context       = Context;
        Status                    = OsSuccess;
    }
    else if (Operation == HANDLE_SET_OP_DEL) {
        rb_leaf_t* Leaf = rb_tree_remove(&Set->Handles, VOID_KEY(Handle));
        if (!Leaf) {
            return OsDoesNotExist;
        }
        
        SetElement = Leaf->value;
        Status     = DestroySetElement(SetElement);
    }
    else {
        Status = OsInvalidParameters;
    }
    return Status;
}

OsStatus_t
WaitForHandleSet(
    _In_  UUId_t          Handle,
    _In_  handle_event_t* Events,
    _In_  int             MaxEvents,
    _In_  size_t          Timeout,
    _Out_ int*            NumberOfEventsOut)
{
    HandleSet_t*    Set   = LookupHandleOfType(Handle, HandleTypeSet);
    handle_event_t* Event = Events;
    int             NumberOfEvents;
    list_t          Spliced;
    element_t*      i;
    WARNING("[handle_set] [wait] %u, %i, %" PRIuIN, 
        Handle, MaxEvents, Timeout);
    
    if (!Set) {
        return OsDoesNotExist;
    }
    
    // Wait for response by 'polling' the value
    NumberOfEvents = atomic_exchange(&Set->Pending, 0);
    while (!NumberOfEvents) {
        OsStatus_t Status = FutexWait(&Set->Pending, NumberOfEvents, 0, Timeout);
        if (Status != OsSuccess) {
            return Status;
        }
        NumberOfEvents = atomic_exchange(&Set->Pending, 0);
    }
    
    list_construct(&Spliced);
    NumberOfEvents = MIN(NumberOfEvents, MaxEvents);
    list_splice(&Set->Events, NumberOfEvents, &Spliced);
    
    smp_rmb();
    WARNING("[handle_set] [wait] num events %i", NumberOfEvents);
    _foreach(i, &Spliced) {
        HandleSetElement_t* Element = i->value;
        
        Event->events  = atomic_exchange(&Element->ActiveEvents, 0);
        Event->handle  = Element->Handle;
        Event->context = Element->Context;
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
    HandleSetElement_t* SetElement = Element->value;
    Flags_t             Flags      = (Flags_t)(uintptr_t)Context;
    WARNING("[handle_set] [mark_cb] 0x%x", SetElement->Configuration);
    
    if (SetElement->Configuration & Flags) {
        int Previous;
        Previous = atomic_fetch_or(&SetElement->ActiveEvents, (int)Flags);
        if (!Previous) {
            list_append(&SetElement->Set->Events, &SetElement->EventHeader);
            
            Previous = atomic_fetch_add(&SetElement->Set->Pending, 1);
            if (!Previous) {
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
    HandleElement_t* Element = list_find_value(&HandleElements, VOID_KEY(Handle));
    if (!Element) {
        return OsDoesNotExist;
    }
    
    WARNING("[handle_set] [mark] handle %u - 0x%x", Handle, Flags);
    list_enumerate(&Element->Sets, MarkHandleCallback, (void*)(uintptr_t)Flags);
    return OsSuccess;
}

static OsStatus_t
DestroySetElement(
    _In_ HandleSetElement_t* SetElement)
{
    HandleElement_t* Element = list_find_value(&HandleElements, VOID_KEY(SetElement->Handle));
    if (Element) {
        list_remove(&Element->Sets, &SetElement->SetHeader);
        if (!list_count(&Element->Sets)) {
            // remove?
        }
    }
    else {
        // well this is wierd if we get here, log this inconsistency?
    }
    
    // If we have an event queued up, we should now remove it
    list_remove(&SetElement->Set->Events, &SetElement->EventHeader);
    
    // At this point we should now not exist in any of the 3 lists
    DestroyHandle(SetElement->Handle);
    kfree(SetElement);
    return OsSuccess;
}

static OsStatus_t
AddHandleToSet(
    _In_ HandleSet_t* Set,
    _In_ UUId_t       Handle,
    _In_ void*        Context,
    _In_ Flags_t      Configuration)
{
    HandleElement_t*    Element;
    HandleSetElement_t* SetElement;
    void*               HandleData;
    
    // Start out by acquiring an reference on the handle
    HandleData = AcquireHandle(Handle);
    if (!HandleData) {
        return OsDoesNotExist;
    }
    
    Element = list_find_value(&HandleElements, VOID_KEY(Handle));
    if (!Element) {
        Element = (HandleElement_t*)kmalloc(sizeof(HandleElement_t));
        if (!Element) {
            return OsOutOfMemory;
        }
        
        ELEMENT_INIT(&Element->Header, VOID_KEY(Handle), Element);
        list_construct(&Element->Sets);
        
        list_append(&HandleElements, &Element->Header);
    }
    
    // Now we have access to the handle-set and the target handle, so we can go ahead
    // and add the target handle to the set-tree and then create the set element for
    // the handle
    
    // For each handle added we must allocate a SetElement and add it to the
    // handle instance
    SetElement = (HandleSetElement_t*)kmalloc(sizeof(HandleSetElement_t));
    if (!SetElement) {
        DestroyHandle(Handle);
        return OsOutOfMemory;
    }
    
    memset(SetElement, 0, sizeof(HandleSetElement_t));
    ELEMENT_INIT(&SetElement->SetHeader, 0, SetElement);
    ELEMENT_INIT(&SetElement->EventHeader, 0, SetElement);
    RB_LEAF_INIT(&SetElement->HandleHeader, Handle, SetElement);
    
    SetElement->Set           = Set;
    SetElement->Handle        = Handle;
    SetElement->Context       = Context;
    SetElement->Configuration = Configuration;
    smp_mb();
    
    // Append to the list of sets on the target handle we are going to listen
    // too. 
    list_append(&Element->Sets, &SetElement->SetHeader);
    //if (Flags & IOEVTFRT) {
    //    // Should we register an initial event?
    //    int PreviousPending;
    //    list_append(&Set->Events, &SetElement->EventHeader);
    //    PreviousPending = atomic_fetch_add(&Set->Pending, 1);
    //}
    
    // Register the target handle in the current set, so we can clean up again
    if (rb_tree_append(&Set->Handles, &SetElement->HandleHeader) != OsSuccess) {
        ERROR("... failed to append handle to list of handles, it exists?");
        list_remove(&Element->Sets, &SetElement->SetHeader);
        DestroyHandle(Handle);
        kfree(SetElement);
        return OsError;
    }
    return OsSuccess;
}
