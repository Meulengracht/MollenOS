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
#include <ds/list.h>
#include <ds/rbtree.h>
#include <futex.h>
#include <handle.h>
#include <handle_set.h>
#include <heap.h>
#include <ioset.h>
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
    unsigned int Flags;
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
    union ioset_data Context;
    unsigned int     Configuration;
} HandleSetElement_t;

static OsStatus_t DestroySetElement(HandleSetElement_t*);
static OsStatus_t AddHandleToSet(HandleSet_t*, UUId_t, struct ioset_event*);

static list_t  HandleElements = LIST_INIT; // Sets per Handle TODO hashtable
//static Mutex_t HandleElementsSyncObject;

static void
DestroyHandleSet(
    _In_ void* Resource)
{
    HandleSet_t* Set = Resource;
    rb_leaf_t*   Leaf;
    TRACE("[handle_set] [destroy]");
    
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
    _In_  unsigned int Flags)
{
    HandleSet_t* Set;
    UUId_t       Handle;
    TRACE("[handle_set] [create] 0x%x", Flags);
    
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
    _In_ UUId_t              setHandle,
    _In_ int                 operation,
    _In_ UUId_t              handle,
    _In_ struct ioset_event* event)
{
    HandleSet_t*        set = LookupHandleOfType(setHandle, HandleTypeSet);
    HandleSetElement_t* setElement;
    OsStatus_t          status;
    TRACE("[handle_set] [control] %u, %i, %u", setHandle, operation, handle);
    
    if (!set) {
        return OsDoesNotExist;
    }
    
    if (operation == IOSET_ADD) {
        if (!event) {
            return OsInvalidParameters;
        }
        
        status = AddHandleToSet(set, handle, event);
    }
    else if (operation == IOSET_MOD) {
        rb_leaf_t* leaf;
        
        if (!event) {
            return OsInvalidParameters;
        }
        
        leaf = rb_tree_lookup(&set->Handles, VOID_KEY(handle));
        if (!leaf) {
            return OsDoesNotExist;
        }
        
        setElement                = leaf->value;
        setElement->Configuration = event->events;
        setElement->Context       = event->data;
        status                    = OsSuccess;
    }
    else if (operation == IOSET_DEL) {
        rb_leaf_t* leaf = rb_tree_remove(&set->Handles, VOID_KEY(handle));
        if (!leaf) {
            return OsDoesNotExist;
        }
        
        setElement = leaf->value;
        status     = DestroySetElement(setElement);
    }
    else {
        status = OsInvalidParameters;
    }
    return status;
}

OsStatus_t
WaitForHandleSet(
    _In_  UUId_t              handle,
    _In_  struct ioset_event* events,
    _In_  int                 maxEvents,
    _In_  int                 pollEvents,
    _In_  size_t              timeout,
    _Out_ int*                numEventsOut)
{
    HandleSet_t* set = LookupHandleOfType(handle, HandleTypeSet);
    int          numberOfEvents;
    list_t       spliced;
    element_t*   i;
    int          j, k = pollEvents;
    TRACE("[handle_set] [wait] %u, %i, %i, %" PRIuIN, handle, maxEvents, pollEvents, timeout);
    
    if (!set) {
        return OsDoesNotExist;
    }
    
    // If there are no queued events, but there were pollEvents, let the user
    // handle those first.
    numberOfEvents = atomic_exchange(&set->Pending, 0);
    if (!numberOfEvents && pollEvents > 0) {
        *numEventsOut = pollEvents;
        return OsSuccess;
    }
    
    // Wait for response by 'polling' the value
    while (!numberOfEvents) {
        OsStatus_t Status = FutexWait(&set->Pending, numberOfEvents, 0, timeout);
        if (Status != OsSuccess) {
            return Status;
        }
        numberOfEvents = atomic_exchange(&set->Pending, 0);
    }

    // @todo add the unhandled event count back
    numberOfEvents = MIN(numberOfEvents, maxEvents);
    list_construct(&spliced);
    list_splice(&set->Events, numberOfEvents, &spliced);
    
    TRACE("[handle_set] [wait] num events %i", numberOfEvents);
    smp_rmb();
    _foreach(i, &spliced) {
        HandleSetElement_t* element = i->value;
        
        // reuse an existing structure (combine events)?
        if (pollEvents) {
            struct ioset_event* reuse = NULL;
            for (j = 0; j < pollEvents; j++) {
                if (events[j].data.context == element->Context.context) {
                    reuse = &events[j];
                    break;
                }
            }
            
            if (reuse) {
                reuse->events |= atomic_exchange(&element->ActiveEvents, 0);
                continue;
            }
        }
        
        // otherwise append the event
        events[k].events = atomic_exchange(&element->ActiveEvents, 0);
        events[k].data   = element->Context;
        k++;
    }
    *numEventsOut = k;
    return OsSuccess;
}

static int
MarkHandleCallback(
    _In_ int        Index,
    _In_ element_t* Element,
    _In_ void*      Context)
{
    HandleSetElement_t* SetElement = Element->value;
    unsigned int        Flags      = (unsigned int)(uintptr_t)Context;
    TRACE("[handle_set] [mark_cb] 0x%x", SetElement->Configuration);
    
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
    _In_ UUId_t       Handle,
    _In_ unsigned int Flags)
{
    HandleElement_t* Element = list_find_value(&HandleElements, VOID_KEY(Handle));
    if (!Element) {
        return OsDoesNotExist;
    }
    
    TRACE("[handle_set] [mark] handle %u - 0x%x", Handle, Flags);
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
    _In_ HandleSet_t*        set,
    _In_ UUId_t              handle,
    _In_ struct ioset_event* event)
{
    HandleElement_t*    element;
    HandleSetElement_t* setElement;
    
    // Start out by acquiring an reference on the handle
    if (AcquireHandle(handle, NULL) != OsSuccess) {
        ERROR("[AddHandleToSet] failed to acquire handle %u", handle);
        return OsDoesNotExist;
    }
    
    element = list_find_value(&HandleElements, VOID_KEY(handle));
    if (!element) {
        element = (HandleElement_t*)kmalloc(sizeof(HandleElement_t));
        if (!element) {
            return OsOutOfMemory;
        }
        
        ELEMENT_INIT(&element->Header, VOID_KEY(handle), element);
        list_construct(&element->Sets);
        
        list_append(&HandleElements, &element->Header);
    }
    
    // Now we have access to the handle-set and the target handle, so we can go ahead
    // and add the target handle to the set-tree and then create the set element for
    // the handle
    
    // For each handle added we must allocate a SetElement and add it to the
    // handle instance
    setElement = (HandleSetElement_t*)kmalloc(sizeof(HandleSetElement_t));
    if (!setElement) {
        DestroyHandle(handle);
        return OsOutOfMemory;
    }
    
    memset(setElement, 0, sizeof(HandleSetElement_t));
    ELEMENT_INIT(&setElement->SetHeader, 0, setElement);
    ELEMENT_INIT(&setElement->EventHeader, 0, setElement);
    RB_LEAF_INIT(&setElement->HandleHeader, handle, setElement);
    
    setElement->Set           = set;
    setElement->Handle        = handle;
    setElement->Context       = event->data;
    setElement->Configuration = event->events;
    smp_mb();
    
    // Append to the list of sets on the target handle we are going to listen
    // too. 
    list_append(&element->Sets, &setElement->SetHeader);
    
    // Register the target handle in the current set, so we can clean up again
    if (rb_tree_append(&set->Handles, &setElement->HandleHeader) != OsSuccess) {
        ERROR("... failed to append handle to list of handles, it exists?");
        list_remove(&element->Sets, &setElement->SetHeader);
        DestroyHandle(handle);
        kfree(setElement);
        return OsError;
    }
    return OsSuccess;
}
