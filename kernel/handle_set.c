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
 * Resource Handle Set Interface
 * - Implementation of handle sets. A handle set is a queue of events that
 *   threads can wait on. Handles are added to the set, and when events are
 *   posted on one of them with MarkHandle, the events are queued in the set and
 *   a waiting thread is woken up to read them with WaitForHandleSet.
 *
 * Subscriptions:
 * Each handle added to a set is called a subscription, and we keep track of them
 * in two places. Each set has a tree of its subscriptions, sorted by the handle
 * ID that was added, which IOSET_MOD and IOSET_DEL use to find them again. All
 * subscriptions are also kept in g_handleSets, grouped by the ID of the resource
 * they watch, which is what MarkHandle uses to find them. For most handles these
 * two IDs are the same. Service references and SHM views are different, as many
 * of them can exist for the same resource. In this case they are grouped by the
 * resource, so an event posted through any of them reaches all of them.
 *
 * Cleanup:
 * A subscription holds no reference to the handle it watches. Instead it
 * remembers the grant it was added through, which is the entry in the grant
 * table of the process that gives it access to the handle. When the process
 * closes its last reference to the handle, or the process exits, the handle
 * code calls HandleSetsRemoveGrant and the subscriptions are removed.
 *
 * Locking:
 * g_handleSetsLock protects the resource entries in g_handleSets, the tree of
 * each set, the lists of pending events and the event counts. MarkHandle can be
 * called from interrupt handlers, so the lock is taken with interrupts disabled,
 * and looking up the resource of a handle must never take a mutex. The lock of
 * a grant table can be held when g_handleSetsLock is taken (see
 * HandleSetsRemoveGrant), so we must never take a grant table lock while
 * holding g_handleSetsLock.
 */

#define __MODULE "handle_set"
//#define __TRACE

#define __need_minmax
#include <debug.h>
#include <ds/list.h>
#include <ds/rbtree.h>
#include <ds/hashtable.h>
#include <futex.h>
#include <handles/handle.h>
#include <handles/global.h>
#include <handles/set.h>
#include <heap.h>
#include <ioset.h>
#include <memoryspace.h>
#include <shm/view.h>
#include <string.h>

#include "handle_private.h"
#include "memory/shm/private.h"

#define VOID_KEY(key) (void*)(uintptr_t)key

struct __HandleSets {
    uuid_t id;
    list_t sets;
};

struct __HandleSet {
    _Atomic(int) EventsPending;
    list_t       events;
    rb_tree_t    handles;
    unsigned int flags;
};

struct __HandleSetElement {
    element_t           set_header;    // This is the header in the resource entry
    rb_leaf_t           handle_header; // This is the leaf in the set's tree
    element_t           event_header;  // This is the header in the set's pending events
    struct __HandleSet* set;           // This is a pointer back to the set it belongs to
    
    // Event data
    uuid_t                     Handle;
    uuid_t                     Resource;
    HandleGrantsTable_t*       GrantTable;
    uuid_t                     CleanupHandle;
    _Atomic(int)               ActiveEvents;
    struct __HandleSetElement* Link;
    union ioset_data           Context;
    unsigned int               Configuration;
};

/**
 * @brief Describes where the events of a handle are posted, and is filled in by
 * __ResolveHandleSource. Resource is the ID that subscriptions are grouped
 * by in g_handleSets. Reference is the handle a temporary global reference was
 * taken on, or UUID_INVALID if none was taken. The caller must release it again
 * when done. Type is the type of the handle, if it was one we know.
 * Registration and ServiceGroup are only set for service references. They are
 * needed because a service usually closes the first reference to a resource
 * once a client has been given access, but still watches the resource through
 * its resource ID (see __ResolveGrantTable).
 */
struct __HandleInformation {
    uuid_t       Resource;
    uuid_t       Reference;
    HandleType_t Type;
    uuid_t       Registration;
    uuid_t       ServiceGroup;
};

/**
 * @brief Used by HandleSetsRemoveGrant to collect the subscriptions to remove.
 * Table and Handle select which subscriptions are removed (see
 * HandleSetsRemoveGrant), and Removed is the list they are collected on. They
 * are collected first and removed afterwards, because removing entries from
 * g_handleSets while walking it can move the entries we have not visited yet.
 */
struct __SubscriptionCleanup {
    HandleGrantsTable_t* Table;
    uuid_t               Handle;
    list_t               Removed;
};

static uint64_t handleset_hash(const void* element);
static int      handleset_cmp(const void* element1, const void* element2);

static oserr_t DestroySetElement(struct __HandleSetElement*);
static oserr_t AddHandleToSet(struct __HandleSet*, uuid_t, struct ioset_event*);
static void __UnlinkSetElement(struct __HandleSetElement*);

static hashtable_t g_handleSets;
static Spinlock_t  g_handleSetsLock;
static atomic_bool g_handleSetsInitialized;

/**
 * @brief Finds the resource ID that the events of a handle are posted on.
 * Service references are resolved to the ID the service uses for the resource,
 * and SHM views to the ID of the SHM buffer they are a view of. Plain SHM
 * buffers and service registrations use their own ID. For these four types a
 * temporary global reference is taken on the handle, which the caller must
 * release with ReleaseGlobalHandle(source->Reference).
 * Any other handle, or a number that is not a handle at all, is used as it is,
 * and no reference is taken (source->Reference is UUID_INVALID).
 * This must be safe to call from interrupt handlers, see MarkHandle.
 *
 * @param handle The handle, or number, to resolve.
 * @param source Receives the result.
 * @return OS_EOK, or an error if the reference could not be taken (OS_EOVERFLOW).
 */
static oserr_t
__ResolveHandleSource(
    _In_  uuid_t                      handle,
    _Out_ struct __HandleInformation* source)
{
    struct __ProviderReference* reference;
    SHMView_t*                  view;
    void*                       payload;
    oserr_t                     status;

    *source = (struct __HandleInformation) {
        .Resource = handle,
        .Reference = UUID_INVALID,
        .Registration = UUID_INVALID,
        .ServiceGroup = UUID_INVALID
    };

    // MarkHandle can be called from interrupt handlers, so we must never take a
    // mutex here, which rules out both the service lock and the SHM buffer lock.
    // We only take the global handle lock (in AcquireGlobalHandle), and read
    // fields that never change once the resource has been created.
    status = AcquireGlobalHandleUntyped(handle, &source->Type, (void**)&payload);

    // default to handle as the resource
    source->Reference = handle;

    // The handle does not exist at all. This is also what keeps the
    // resource ID of a service working after the service has closed the first
    // reference to the resource.
    if (status != OS_EOK) {
        return status == OS_ENOENT || status == OS_EUNKNOWN ? OS_EOK : status;
    }

    // Otherwise we dereference the handle to find the actual resource it refers to.
    switch (source->Type) {
    case HandleTypeSHMView:
        view = payload;
        source->Resource = view->Buffer->ID;
        break;
    case HandleTypeProviderReference:
        reference = payload;
        source->Resource = reference->Resource->ID;
        source->Registration = reference->Resource->Provider->ID;
        source->ServiceGroup = reference->Resource->Provider->GroupID;
        break;
    default:
        break;
    }
    return OS_EOK;
}

/**
 * @brief Finds the grant that a new subscription should be removed with, and
 * takes a temporary count on it. The grant is stored in element->GrantTable and
 * element->CleanupHandle. When called by the kernel itself (no security context)
 * nothing is tracked, and the subscription is only removed with IOSET_DEL or
 * with its set.
 * The count keeps the grant open while AddHandleToSet is still adding the
 * subscription, otherwise a close could finish first and the subscription would
 * be left behind. AddHandleToSet drops the count again with DestroyHandle once
 * the subscription has been added, or the add has failed.
 *
 * @param source  The resolved source of the handle being added.
 * @param element The new subscription.
 * @return OS_EOK, or an error from HandleSetsReferenceGrant if the caller has no
 *         grant for a handle that requires one.
 */
static oserr_t
__ResolveGrantTable(
    _In_    const struct __HandleInformation* source,
    _InOut_ struct __HandleSetElement*        element)
{
    MSContextSecurity_t* security = MemorySpaceGetSecurityContext();
    uuid_t               grant = source->Resource;
    oserr_t              status;

    element->CleanupHandle = UUID_INVALID;
    
    if (security == NULL) {
        return OS_EOK;
    }

    element->GrantTable = security->HandleTable;
    if (source->Type == HandleTypeProvider) {
        // Subscriptions on a service registration are not tied to its grant. If
        // the registration is closed, a wakeup that is already queued can still
        // be read from the queue. The subscription itself is removed by
        // IOSET_DEL, when the queue is destroyed or when the service exits.
        return OS_EOK;
    }

    if (source->Reference != UUID_INVALID) {
        grant = source->Reference;
    }

    if (source->Reference == source->Resource &&
        source->ServiceGroup == security->GlobalHandleID) {
        // The service is adding the resource ID of one of its own resources. It
        // usually closes this first reference once a client has been given
        // access, but it still wants events for the resource. So in this case
        // the subscription follows the registration instead, and is removed when
        // the service closes its registration. References the service imports
        // for its own resources still follow their own grant.
        grant = source->Registration;
    }

    // Take a temporary count on the grant until the subscription has been added.
    // If another thread closes the handle in the meantime, the close finishes
    // when AddHandleToSet drops this count. By then the subscription is in place,
    // and the close removes it as it should.
    status = HandleSetsReferenceGrant(grant);
    if (status == OS_EOK) {
        element->CleanupHandle = grant;
    }
    if (status == OS_ENOENT && source->Reference == UUID_INVALID) {
        // The caller has no grant for it, which is allowed for handles of other
        // types and for numbers that are not handles. This includes the resource
        // ID of a service after the first reference has been closed. In this
        // case the subscription is only removed by IOSET_DEL, with its set or
        // when the process exits. Any other error fails the add, so watching a
        // service reference or an SHM handle requires a grant for it.
        return OS_EOK;
    }
    return status;
}

/**
 * @brief Takes a subscription off the list of pending events in its set, and
 * clears the events that were posted on it. Nothing happens if it has no
 * pending events. The caller must hold g_handleSetsLock.
 *
 * @param element The subscription.
 */
static void
__RemovePendingEvent(
    _In_ struct __HandleSetElement* element)
{
    // ActiveEvents, the list of pending events and EventsPending must always
    // agree with each other, so we only change them together while holding
    // g_handleSetsLock. A waiter must also never keep a pointer to a subscription
    // after releasing the lock, because it can be removed and freed as soon as the
    // lock is released (see __TakeSetEvent).
    if (atomic_exchange(&element->ActiveEvents, 0) != 0) {
        list_remove(&element->set->events, &element->event_header);
        atomic_fetch_sub(&element->set->EventsPending, 1);
    }
}

/**
 * @brief Called by HandleSetsRemoveGrant for each resource entry in g_handleSets.
 * Finds the subscriptions in the entry that were added through cleanup->Table.
 * If cleanup->Handle is not UUID_INVALID, only the subscriptions tied to that
 * grant are selected. The selected subscriptions are taken off the list of
 * pending events and put on cleanup->Removed. The caller must hold
 * g_handleSetsLock.
 *
 * @param index   Not used.
 * @param value   The resource entry (struct __HandleSets).
 * @param context The cleanup (struct __SubscriptionCleanup).
 */
static void
__CollectSubscriptions(
    _In_ int         index,
    _In_ const void* value,
    _In_ void*       context)
{
    const struct __HandleSets*    bucket = value;
    struct __SubscriptionCleanup* cleanup = context;
    struct __HandleSetElement*    element;
    element_t*                    node;
    (void)index;

    for (node = bucket->sets.head; node != NULL; node = node->next) {
        element = node->value;
        if (element->GrantTable != cleanup->Table) {
            continue;
        }
        if (cleanup->Handle != UUID_INVALID && element->CleanupHandle != cleanup->Handle) {
            continue;
        }

        __RemovePendingEvent(element);
        list_append(&cleanup->Removed, &element->event_header);
    }
}

void
HandleSetsRemoveGrant(
    _In_ HandleGrantsTable_t* table,
    _In_ uuid_t               handle)
{
    struct __SubscriptionCleanup cleanup = { .Table = table, .Handle = handle };
    struct __HandleInformation   source;
    struct __HandleSets*         bucket;
    struct __HandleSetElement*   element;
    element_t*                   node;
    bool                         scanAll = handle == UUID_INVALID;

    if (!atomic_load(&g_handleSetsInitialized)) {
        return;
    }

    list_construct(&cleanup.Removed);

    if (handle != UUID_INVALID) {
        // The handle is still alive here, as the caller has not yet dropped the
        // global reference the grant held. If the grant is for a service
        // registration, the subscriptions tied to it can be on any of the
        // resources of the service, so in this case we must check all of them.
        scanAll = LookupGlobalHandle(handle, HandleTypeProvider) != NULL;
        if (__ResolveHandleSource(handle, &source) != OS_EOK) {
            // Removing the subscriptions must not fail just because we could not
            // take a temporary reference on the handle. This rarely happens, and
            // we can simply check all resource entries instead. The result is
            // the same, it just takes longer.
            scanAll = true;
        }
    }

    SpinlockAcquireIrq(&g_handleSetsLock);
    if (scanAll) {
        hashtable_enumerate(&g_handleSets, __CollectSubscriptions, &cleanup);
    } else {
        bucket = hashtable_get(&g_handleSets, &(struct __HandleSets) { .id = source.Resource });
        if (bucket != NULL) {
            __CollectSubscriptions(0, bucket, &cleanup);
        }
    }

    // We only unlink the subscriptions once we are done walking g_handleSets,
    // because removing an entry from the hashtable can move the other entries.
    // They were collected through their event_header, which is free to use as
    // they have already been taken off the list of pending events.
    for (node = cleanup.Removed.head; node != NULL; node = node->next) {
        element = node->value;
        __UnlinkSetElement(element);
    }
    SpinlockReleaseIrq(&g_handleSetsLock);

    while ((node = list_front(&cleanup.Removed)) != NULL) {
        list_remove(&cleanup.Removed, node);
        kfree(node->value);
    }

    if (handle != UUID_INVALID && source.Reference != UUID_INVALID) {
        ReleaseGlobalHandle(source.Reference);
    }
}

oserr_t
HandleSetsInitialize(void)
{
    int status;
    
    status = hashtable_construct(
        &g_handleSets,
        HASHTABLE_MINIMUM_CAPACITY,
        sizeof(struct __HandleSets),
        handleset_hash,
        handleset_cmp
    );
    if (status) {
        return OS_EOOM;
    }

    // Avoid shrinking the hashtable while interrupts are off. Entries are only
    // removed while holding g_handleSetsLock, which disables interrupts.
    g_handleSets.flags = HASHTABLE_FLAG_NO_SHRINK;
    SpinlockConstruct(&g_handleSetsLock);
    atomic_store(&g_handleSetsInitialized, true);
    return OS_EOK;
}

static void
DestroyHandleSet(
    _In_ void* resource)
{
    struct __HandleSet* set = resource;
    rb_leaf_t*          leaf;
    TRACE("DestroyHandleSet()");

    // Other threads can still reach the subscriptions of this set through
    // g_handleSets, for instance when posting events (MarkHandle) or when a grant
    // is closed, so we must hold the lock while removing them.
    SpinlockAcquireIrq(&g_handleSetsLock);
    leaf = rb_tree_minimum(&set->handles);
    while (leaf) {
        DestroySetElement(leaf->value);
        leaf = rb_tree_minimum(&set->handles);
    }
    SpinlockReleaseIrq(&g_handleSetsLock);
    kfree(set);
}

uuid_t
CreateHandleSet(
    _In_ unsigned int flags)
{
    struct __HandleSet* handleSet;
    uuid_t              handleId;
    TRACE("CreateHandleSet(flags=0x%x)", flags);

    handleSet = (struct __HandleSet*)kmalloc(sizeof(struct __HandleSet));
    if (!handleSet) {
        return UUID_INVALID;
    }

    // Initialize the set before creating the handle for it, as other threads can
    // use the handle as soon as it has been created.
    list_construct(&handleSet->events);
    rb_tree_construct(&handleSet->handles);
    handleSet->EventsPending = 0;
    handleSet->flags = flags;

    handleId = CreateHandle(
        HandleTypeSet,
        NULL,
        DestroyHandleSet,
        handleSet
    );

    if (handleId == UUID_INVALID) {
        kfree(handleSet);
    }
    return handleId;
}

oserr_t
ControlHandleSet(
    _In_ uuid_t              setHandle,
    _In_ int                 operation,
    _In_ uuid_t              handle,
    _In_ struct ioset_event* event)
{
    struct __HandleSet*        set;
    struct __HandleSetElement* element = NULL;
    rb_leaf_t*                 leaf;
    oserr_t                    status;

    // Take a reference on the set while we use it, so it is not destroyed if
    // another thread closes it in the meantime.
    status = AcquireHandleReference(setHandle, HandleTypeSet, (void**)&set);
    if (status != OS_EOK) {
        return status;
    }

    if (operation == IOSET_ADD) {
        status = event != NULL ? AddHandleToSet(set, handle, event) : OS_EINVALPARAMS;
        goto exit;
    }

    if (operation != IOSET_MOD && operation != IOSET_DEL) {
        status = OS_EINVALPARAMS;
        goto exit;
    }

    if (operation == IOSET_MOD && event == NULL) {
        status = OS_EINVALPARAMS;
        goto exit;
    }

    // Subscriptions are looked up by the handle that was added, and not by the
    // resource ID it was resolved to. This way several handles to the same
    // resource can be in the same set, and each of them can be changed or
    // removed on its own.
    SpinlockAcquireIrq(&g_handleSetsLock);
    leaf = rb_tree_lookup(&set->handles, VOID_KEY(handle));
    if (leaf == NULL) {
        status = OS_ENOENT;
    } else {
        element = leaf->value;
        if (operation == IOSET_MOD) {
            element->Configuration = event->events;
            element->Context = event->data;
        } else {
            __UnlinkSetElement(element);
        }
        status = OS_EOK;
    }
    SpinlockReleaseIrq(&g_handleSetsLock);

    // Once unlinked, nothing else can reach the subscription, so it is safe to
    // free it after releasing the lock.
    if (operation == IOSET_DEL && element != NULL) {
        kfree(element);
    }

exit:
    ReleaseGlobalHandle(setHandle);
    return status;
}

/**
 * @brief Takes the first pending event from a set. The events and the context
 * are copied out while holding g_handleSetsLock, and the subscription is taken
 * off the list of pending events. Since the caller only gets a copy, the
 * subscription can be removed (for instance if its grant is closed) while the
 * caller is still using the event.
 *
 * @param set   The set to take an event from.
 * @param event Receives the events and the context of the subscription.
 * @return true if an event was taken, false if the set has no pending events.
 */
static bool
__TakeSetEvent(
    _In_  struct __HandleSet* set,
    _Out_ struct ioset_event* event)
{
    struct __HandleSetElement* element;
    element_t*                 node;

    SpinlockAcquireIrq(&g_handleSetsLock);
    node = list_front(&set->events);
    if (node == NULL) {
        SpinlockReleaseIrq(&g_handleSetsLock);
        return false;
    }

    element = node->value;
    event->events = atomic_load(&element->ActiveEvents);
    event->data = element->Context;
    __RemovePendingEvent(element);
    SpinlockReleaseIrq(&g_handleSetsLock);
    return true;
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
    struct __HandleSet* set;
    struct ioset_event  event;
    oserr_t             status;
    int                 available;
    int                 consumed = 0;
    int                 count = pollEvents;
    int                 i;

    status = AcquireHandleReference(handle, HandleTypeSet, (void**)&set);
    if (status != OS_EOK) {
        return status;
    }

    // Validate the input parameters for the handle set wait operation.
    if (maxEvents <= 0 || pollEvents < 0 || pollEvents > maxEvents) {
        status = OS_EINVALPARAMS;
        goto exit;
    }

    available = maxEvents - pollEvents;
    while (consumed < available) {
        if (!__TakeSetEvent(set, &event)) {
            // If there are no queued events, but we already have events for the
            // user (either pollEvents or events we have taken), let the user
            // handle those first.
            if (count != 0) {
                break;
            }

            // Otherwise we sleep for as long as EventsPending is 0.
            // MarkHandleCallback wakes us up when the first event is posted.
            status = FutexWait(
                asyncContext,
                &set->EventsPending,
                0,
                0,
                NULL,
                0,
                0,
                deadline
            );
            if (status == OS_EFORKED) {
                // The wait is asynchronous, and a forked copy of this thread now
                // continues this call and does the actual waiting. It uses the
                // same set, so the reference we took on the set is handed over to
                // the forked thread, which releases it when it is done. In this
                // case we must return without releasing it.
                return status;
            }
            if (status != OS_EOK) {
                goto exit;
            }
            // We may now be running as the forked thread. If another waiter
            // takes the event before we do, we go back to sleep, and that sleep
            // must not fork the thread a second time. So from here on we sleep
            // like a normal waiter.
            asyncContext = NULL;
            continue;
        }
        consumed++;

        // reuse an existing structure (combine events)?
        for (i = 0; i < pollEvents; i++) {
            if (events[i].data.context == event.data.context) {
                events[i].events |= event.events;
                break;
            }
        }

        // otherwise, append the event
        if (i == pollEvents) {
            events[count++] = event;
        }
    }

    // If we could not take all the events, then wake another waiter for the
    // rest. MarkHandleCallback only wakes a waiter when the first event is
    // posted, so a waiter that is already sleeping would not be woken otherwise.
    if (atomic_load(&set->EventsPending) != 0) {
        (void)FutexWake(&set->EventsPending, 1, 0);
    }

    *numEventsOut = count;
    status = OS_EOK;

exit:
    ReleaseGlobalHandle(handle);
    return status;
}

static int
MarkHandleCallback(
    _In_ int        index,
    _In_ element_t* element,
    _In_ void*      context)
{
    struct __HandleSetElement* setElement     = element->value;
    unsigned int               flags          = (unsigned int)(uintptr_t)context;
    unsigned int               acceptedEvents = setElement->Configuration & flags;
    TRACE("MarkHandleCallback(config=0x%x, accept=0x%x)", setElement->Configuration, acceptedEvents);
    
    if (acceptedEvents) {
        int previousEvents = atomic_fetch_or(&setElement->ActiveEvents, (int)acceptedEvents);
        if (!previousEvents) {
            list_append(&setElement->set->events, &setElement->event_header);

            previousEvents = atomic_fetch_add(&setElement->set->EventsPending, 1);
            if (!previousEvents) {
                (void)FutexWake(&setElement->set->EventsPending, 1, 0);
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
    struct __HandleSets*       element;
    struct __HandleInformation source;
    oserr_t                    status;
    TRACE("MarkHandle(handle=%u, flags=0x%x)", handle, flags);

    // The handle that events are posted with does not have to be the same handle
    // that was added to a set. So we resolve it to the resource ID that the
    // subscriptions are grouped by (see __ResolveHandleSource).
    status = __ResolveHandleSource(handle, &source);
    if (status != OS_EOK) {
        return status;
    }
    
    SpinlockAcquireIrq(&g_handleSetsLock);
    element = hashtable_get(&g_handleSets, &(struct __HandleSets) { .id = source.Resource });
    if (!element) {
        status = OS_ENOENT;
    } else {
        list_enumerate(&element->sets, MarkHandleCallback, (void*)(uintptr_t)flags);
    }
    SpinlockReleaseIrq(&g_handleSetsLock);
    if (source.Reference != UUID_INVALID) {
        ReleaseGlobalHandle(source.Reference);
    }
    return status;
}

/**
 * @brief Removes a subscription from its set, from its resource entry in
 * g_handleSets and from the list of pending events. The resource entry is
 * removed as well if this was the last subscription in it. The subscription
 * itself is not freed. The caller must hold g_handleSetsLock.
 *
 * @param setElement The subscription to remove.
 */
static void
__UnlinkSetElement(
    _In_ struct __HandleSetElement* setElement)
{
    struct __HandleSets* element;

    // The set finds the subscription by the handle that was added, while
    // g_handleSets finds it by the resource ID, so each of them is given its
    // own key.
    rb_tree_remove(&setElement->set->handles, VOID_KEY(setElement->Handle));
    element = hashtable_get(&g_handleSets, &(struct __HandleSets) { .id = setElement->Resource });
    if (element) {
        list_remove(&element->sets, &setElement->set_header);
        if (!list_count(&element->sets)) {
            hashtable_remove(&g_handleSets, &(struct __HandleSets) { .id = setElement->Resource });
        }
    }
    __RemovePendingEvent(setElement);
}

/**
 * @brief Removes a subscription (see __UnlinkSetElement) and frees it. The
 * caller must hold g_handleSetsLock.
 *
 * @param setElement The subscription to destroy.
 * @return Always OS_EOK.
 */
static oserr_t
DestroySetElement(
    _In_ struct __HandleSetElement* setElement)
{
    __UnlinkSetElement(setElement);
    kfree(setElement);
    return OS_EOK;
}

/**
 * @brief Adds a handle to a set. The handle is resolved to the resource ID that
 * its events are posted on, and the new subscription is tied to the grant it
 * should be removed with (see __ResolveGrantTable).
 *
 * @param set    The set to add the handle to.
 * @param handle The handle, or number, to add.
 * @param event  The events to watch for (events), and the context that is given
 *               back with them (data).
 * @return OS_EOK, OS_EINVALPARAMS if the handle is already in the set, OS_EOOM if
 *         we run out of memory, or an error from resolving the handle or from
 *         finding its grant.
 */
static oserr_t
AddHandleToSet(
    _In_ struct __HandleSet* set,
    _In_ uuid_t              handle,
    _In_ struct ioset_event* event)
{
    struct __HandleSets*       bucket;
    struct __HandleSetElement* element;
    struct __HandleInformation source;
    uuid_t                     grant = UUID_INVALID;
    oserr_t                    status;

    status = __ResolveHandleSource(handle, &source);
    if (status != OS_EOK) {
        return status;
    }

    element = kmalloc(sizeof(struct __HandleSetElement));
    if (element == NULL) {
        status = OS_EOOM;
        goto release;
    }

    memset(element, 0, sizeof(struct __HandleSetElement));
    ELEMENT_INIT(&element->set_header, 0, element);
    ELEMENT_INIT(&element->event_header, 0, element);
    RB_LEAF_INIT(&element->handle_header, handle, element);
    element->set = set;
    element->Handle = handle;
    element->Resource = source.Resource;
    element->Context = event->data;
    element->Configuration = event->events;

    status = __ResolveGrantTable(&source, element);
    if (status != OS_EOK) {
        goto free_element;
    }
    grant = element->CleanupHandle;

    SpinlockAcquireIrq(&g_handleSetsLock);
    if (rb_tree_lookup(&set->handles, VOID_KEY(handle)) != NULL) {
        status = OS_EINVALPARAMS;
        goto unlock;
    }

    bucket = hashtable_get(&g_handleSets, &(struct __HandleSets) { .id = source.Resource });
    if (bucket == NULL) {
        hashtable_set(&g_handleSets, &(struct __HandleSets) {
            .id = source.Resource,
            .sets = LIST_INIT
        });

        bucket = hashtable_get(&g_handleSets, &(struct __HandleSets) { .id = source.Resource });
        if (bucket == NULL) {
            status = OS_EOOM;
            goto unlock;
        }
    }

    if (rb_tree_append(&set->handles, &element->handle_header) != 0) {
        if (list_count(&bucket->sets) == 0) {
            hashtable_remove(&g_handleSets, &(struct __HandleSets) { .id = source.Resource });
        }
        status = OS_EINVALPARAMS;
        goto unlock;
    }

    list_append(&bucket->sets, &element->set_header);
    status = OS_EOK;

unlock:
    SpinlockReleaseIrq(&g_handleSetsLock);

free_element:
    if (status != OS_EOK) {
        kfree(element);
    }

release:
    // The subscription does not keep the handle or its grant alive, so we drop
    // the temporary references again. The grant count is dropped only now that
    // the subscription has been added. So if another thread closed the handle
    // in the meantime, this becomes the final close, and it removes the new
    // subscription again.
    if (grant != UUID_INVALID) {
        DestroyHandle(grant);
    }
    if (source.Reference != UUID_INVALID) {
        ReleaseGlobalHandle(source.Reference);
    }
    return status;
}

static uint64_t handleset_hash(const void* element)
{
    const struct __HandleSets* entry = element;
    return entry->id; // already unique identifier
}

static int handleset_cmp(const void* element1, const void* element2)
{
    const struct __HandleSets* lh = element1;
    const struct __HandleSets* rh = element2;

    // return 0 on true, 1 on false
    return lh->id == rh->id ? 0 : 1;
}
