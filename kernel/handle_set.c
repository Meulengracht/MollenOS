/**
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Resource Handle Interface
 * - Implementation of the resource handle interface. This provides system-wide
 *   resource handles and maintience of resources. 
 */

#define __MODULE "handle_set"
//#define __TRACE

#define __need_minmax
#include <debug.h>
#include <ds/list.h>
#include <ds/rbtree.h>
#include <ds/hashtable.h>
#include <futex.h>
#include <handle.h>
#include <handle_set.h>
#include <heap.h>
#include <ioset.h>
#include <string.h>

#define VOID_KEY(key) (void*)(uintptr_t)key

// The HandleSet is the set that is created and contains a list of handles registered
// with the set (HandleItems), and also contains a list of registered events
struct handle_sets {
    uuid_t id;
    list_t sets;
};

struct handle_set {
    _Atomic(int) events_pending;
    list_t       events;
    rb_tree_t    handles;
    unsigned int flags;
};

// A set element is a handle descriptor and an event descriptor
struct handleset_element {
    element_t          set_header;    // This is the header in the Set
    rb_leaf_t          handle_header; // This is a handle header
    element_t          event_header;  // This is an event header
    struct handle_set* set;           // This is a pointer back to the set it belongs
    
    // Event data
    uuid_t                    Handle;
    _Atomic(int)              ActiveEvents;
    struct handleset_element* Link;
    union ioset_data          Context;
    unsigned int              Configuration;
};

static uint64_t handleset_hash(const void* element);
static int      handleset_cmp(const void* element1, const void* element2);

static oserr_t DestroySetElement(struct handleset_element*);
static oserr_t AddHandleToSet(struct handle_set*, uuid_t, struct ioset_event*);

static hashtable_t g_handleSets;
static Spinlock_t  g_handleSetsLock; // use irq lock as we use MarkHandle from interrupts

oserr_t
HandleSetsInitialize(void)
{
    int status = hashtable_construct(&g_handleSets, HASHTABLE_MINIMUM_CAPACITY,
                        sizeof(struct handle_sets), handleset_hash,
                        handleset_cmp);
    if (status) {
        return OS_EOOM;
    }
    SpinlockConstruct(&g_handleSetsLock);
    return OS_EOK;
}

static void
DestroyHandleSet(
    _In_ void* resource)
{
    struct handle_set* set = resource;
    rb_leaf_t*         leaf;
    TRACE("DestroyHandleSet()");

    leaf = rb_tree_minimum(&set->handles);
    while (leaf) {
        rb_tree_remove(&set->handles, leaf->key);
        DestroySetElement(leaf->value);
        leaf = rb_tree_minimum(&set->handles);
    }
    kfree(set);
}

uuid_t
CreateHandleSet(
    _In_  unsigned int flags)
{
    struct handle_set* handleSet;
    uuid_t             handleId;
    TRACE("CreateHandleSet(flags=0x%x)", flags);

    handleSet = (struct handle_set*)kmalloc(sizeof(struct handle_set));
    if (!handleSet) {
        return UUID_INVALID;
    }

    handleId = CreateHandle(
            HandleTypeSet,
            DestroyHandleSet,
            handleSet
    );

    // initialize the handle set
    list_construct(&handleSet->events);
    rb_tree_construct(&handleSet->handles);
    handleSet->events_pending = 0;
    handleSet->flags          = flags;

    return handleId;
}

oserr_t
ControlHandleSet(
        _In_ uuid_t              setHandle,
        _In_ int                 operation,
        _In_ uuid_t              handle,
        _In_ struct ioset_event* event)
{
    struct handle_set*        set = LookupHandleOfType(setHandle, HandleTypeSet);
    struct handleset_element* setElement;
    oserr_t                osStatus;
    TRACE("ControlHandleSet(setHandle=%u, op=%i, handle=%u)", setHandle, operation, handle);

    if (!set) {
        return OS_ENOENT;
    }
    
    if (operation == IOSET_ADD) {
        if (!event) {
            return OS_EINVALPARAMS;
        }

        osStatus = AddHandleToSet(set, handle, event);
    }
    else if (operation == IOSET_MOD) {
        rb_leaf_t* leaf;
        
        if (!event) {
            return OS_EINVALPARAMS;
        }
        
        leaf = rb_tree_lookup(&set->handles, VOID_KEY(handle));
        if (!leaf) {
            return OS_ENOENT;
        }
        
        setElement                = leaf->value;
        setElement->Configuration = event->events;
        setElement->Context       = event->data;
        osStatus                  = OS_EOK;
    }
    else if (operation == IOSET_DEL) {
        rb_leaf_t* leaf = rb_tree_remove(&set->handles, VOID_KEY(handle));
        if (!leaf) {
            return OS_ENOENT;
        }
        
        setElement = leaf->value;
        osStatus   = DestroySetElement(setElement);
    }
    else {
        osStatus = OS_EINVALPARAMS;
    }
    return osStatus;
}

oserr_t
WaitForHandleSet(
        _In_  uuid_t              handle,
        _In_  OSAsyncContext_t*   asyncContext,
        _In_  struct ioset_event* events,
        _In_  int                 maxEvents,
        _In_  int                 pollEvents,
        _In_  OSTimestamp_t*      deadline,
        _Out_ int*                numEventsOut)
{
    struct handle_set* set = LookupHandleOfType(handle, HandleTypeSet);
    int                numberOfEvents;
    list_t             spliced;
    int                k = pollEvents;
    TRACE("WaitForHandleSet(%u, %i, %i)", handle, maxEvents, pollEvents);

    if (!set) {
        return OS_ENOENT;
    }
    
    // If there are no queued events, but there were pollEvents, let the user
    // handle those first.
    numberOfEvents = atomic_exchange(&set->events_pending, 0);
    if (!numberOfEvents && pollEvents > 0) {
        *numEventsOut = pollEvents;
        return OS_EOK;
    }
    
    // Wait for response by 'polling' the value
    while (!numberOfEvents) {
        oserr_t oserr = FutexWait(
                asyncContext,
                &set->events_pending,
                numberOfEvents,
                0,
                NULL,
                0,
                0,
                deadline
        );
        if (oserr != OS_EOK) {
            return oserr;
        }
        numberOfEvents = atomic_exchange(&set->events_pending, 0);
    }

    numberOfEvents = MIN(numberOfEvents, maxEvents);
    list_construct(&spliced);
    list_splice(&set->events, numberOfEvents, &spliced);
    if (numberOfEvents > maxEvents) {
        // add the event count back that we are not handling
        atomic_fetch_add(&set->events_pending, numberOfEvents - maxEvents);
    }
    
    TRACE("WaitForHandleSet numberOfEvents=%i", numberOfEvents);
    foreach(i, &spliced) {
        struct handleset_element* element = i->value;
        
        // reuse an existing structure (combine events)?
        if (pollEvents) {
            struct ioset_event* reuse = NULL;
            for (int j = 0; j < pollEvents; j++) {
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
        
        // otherwise, append the event
        events[k].events = atomic_exchange(&element->ActiveEvents, 0);
        events[k].data   = element->Context;
        k++;
    }
    *numEventsOut = k;
    return OS_EOK;
}

static int
MarkHandleCallback(
    _In_ int        index,
    _In_ element_t* element,
    _In_ void*      context)
{
    struct handleset_element* setElement     = element->value;
    unsigned int              flags          = (unsigned int)(uintptr_t)context;
    unsigned int              acceptedEvents = setElement->Configuration & flags;
    TRACE("MarkHandleCallback(config=0x%x, accept=0x%x)", setElement->Configuration, acceptedEvents);
    
    if (acceptedEvents) {
        int previousEvents = atomic_fetch_or(&setElement->ActiveEvents, (int)acceptedEvents);
        if (!previousEvents) {
            list_append(&setElement->set->events, &setElement->event_header);

            previousEvents = atomic_fetch_add(&setElement->set->events_pending, 1);
            if (!previousEvents) {
                (void)FutexWake(&setElement->set->events_pending, 1, 0);
            }
        }
    }
    return LIST_ENUMERATE_CONTINUE;
}

oserr_t
MarkHandle(
        _In_ uuid_t       handle,
        _In_ unsigned int flags)
{
    struct handle_sets* element;
    TRACE("MarkHandle(handle=%u, flags=0x%x)", handle, flags);

    SpinlockAcquireIrq(&g_handleSetsLock);
    element = hashtable_get(&g_handleSets, &(struct handle_sets) { .id = handle });
    SpinlockReleaseIrq(&g_handleSetsLock);

    if (!element) {
        return OS_ENOENT;
    }

    list_enumerate(&element->sets, MarkHandleCallback, (void*)(uintptr_t)flags);
    return OS_EOK;
}

static oserr_t
DestroySetElement(
    _In_ struct handleset_element* setElement)
{
    struct handle_sets* element;

    SpinlockAcquireIrq(&g_handleSetsLock);
    element = hashtable_get(&g_handleSets, &(struct handle_sets) { .id = setElement->Handle });
    SpinlockReleaseIrq(&g_handleSetsLock);

    if (element) {
        list_remove(&element->sets, &setElement->set_header);
        if (!list_count(&element->sets)) {
            SpinlockAcquireIrq(&g_handleSetsLock);
            hashtable_remove(&g_handleSets, &(struct handle_sets) { .id = setElement->Handle });
            SpinlockReleaseIrq(&g_handleSetsLock);
        }
    }

    // If we have an event queued up, we should now remove it
    list_remove(&setElement->set->events, &setElement->event_header);
    kfree(setElement);
    return OS_EOK;
}

static oserr_t
AddHandleToSet(
        _In_ struct handle_set*  set,
        _In_ uuid_t              handle,
        _In_ struct ioset_event* event)
{
    struct handle_sets*       element;
    struct handleset_element* setElement;
    int                       status;

    SpinlockAcquireIrq(&g_handleSetsLock);
    element = hashtable_get(&g_handleSets, &(struct handle_sets) { .id = handle });
    if (!element) {
        hashtable_set(&g_handleSets, &(struct handle_sets) { .id = handle, .sets = LIST_INIT });
        element = hashtable_get(&g_handleSets, &(struct handle_sets) { .id = handle });
    }
    SpinlockReleaseIrq(&g_handleSetsLock);

    // Now we have access to the handle-set and the target handle, so we can go ahead
    // and add the target handle to the set-tree and then create the set element for
    // the handle
    
    // For each handle added we must allocate a SetElement and add it to the
    // handle instance
    setElement = (struct handleset_element*)kmalloc(sizeof(struct handleset_element));
    if (!setElement) {
        return OS_EOOM;
    }
    
    memset(setElement, 0, sizeof(struct handleset_element));
    ELEMENT_INIT(&setElement->set_header, 0, setElement);
    ELEMENT_INIT(&setElement->event_header, 0, setElement);
    RB_LEAF_INIT(&setElement->handle_header, handle, setElement);
    
    setElement->set           = set;
    setElement->Handle        = handle;
    setElement->Context       = event->data;
    setElement->Configuration = event->events;
    
    // Append to the list of sets on the target handle we are going to listen
    // too. 
    list_append(&element->sets, &setElement->set_header);
    
    // Register the target handle in the current set, so we can clean up again
    status = rb_tree_append(&set->handles, &setElement->handle_header);
    if (status) {
        ERROR("AddHandleToSet rb_tree_append failed with %i", status);
        list_remove(&element->sets, &setElement->set_header);
        kfree(setElement);
        return OS_EINVALPARAMS;
    }
    return OS_EOK;
}

static uint64_t handleset_hash(const void* element)
{
    const struct handle_sets* entry = element;
    return entry->id; // already unique identifier
}

static int handleset_cmp(const void* element1, const void* element2)
{
    const struct handle_sets* lh = element1;
    const struct handle_sets* rh = element2;

    // return 0 on true, 1 on false
    return lh->id == rh->id ? 0 : 1;
}
