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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Resource Handle Set Interface
 * - Handle sets are queues of events that threads can wait on. Handles are
 *   added to a set, and when events are posted on one of them with MarkHandle,
 *   the threads waiting on the set are woken up to read them.
 */

#ifndef __HANDLES_SET_H__
#define __HANDLES_SET_H__

#include <os/types/async.h>
#include <os/types/time.h>

struct ioset_event;

/**
 * @brief Sets up handle sets. This must be called once during boot, before any
 * handle set is created.
 *
 * @return OS_EOK, or OS_EOOM if memory could not be allocated.
 */
KERNELAPI oserr_t KERNELABI HandleSetsInitialize(void);

/**
 * @brief Creates a new handle set, which is a queue of events that threads can
 * wait on with WaitForHandleSet. The calling process gets the first reference to
 * the set.
 *
 * @param flags Flags for the new set. They are stored with the set, but are not
 *              used for anything yet.
 * @return The handle of the new set, or UUID_INVALID on failure.
 */
KERNELAPI uuid_t KERNELABI
CreateHandleSet(
    _In_  unsigned int flags);

/**
 * @brief Adds, changes or removes a handle in a handle set. Each handle added to
 * a set is called a subscription, and IOSET_MOD and IOSET_DEL find it again by
 * the handle that was added.
 * Any number can be added, even one that is not a handle. To add a service
 * reference or an SHM handle the caller must have a grant for it. The exception
 * is a service adding the ID of one of its own resources, which needs a grant
 * for the registration of the service instead.
 * Events are posted on the resource behind the handle. So for service references
 * and SHM views, events posted through any reference to the same resource reach
 * the subscription. Several handles to the same resource can be in the same set,
 * each with their own events and context.
 * A subscription does not keep the handle alive. It is removed when the process
 * that added it closes its last reference to the handle, or exits. When a service
 * adds the ID of one of its own resources, the subscription is removed when the
 * service closes its registration instead.
 *
 * @param setHandle The handle set. The caller must have a grant for it.
 * @param operation IOSET_ADD, IOSET_MOD or IOSET_DEL.
 * @param handle    The handle to add, change or remove.
 * @param event     The events to watch for, and the context that is given back
 *                  with them. Not used for IOSET_DEL, so it may be NULL.
 * @return OS_EOK on success. OS_EINVALPARAMS for an unknown operation, a missing
 *         event, or if the handle is already in the set. OS_ENOENT if the handle
 *         is not in the set, or the caller has no grant for a handle that needs
 *         one. Otherwise an error from looking up the set or the handle, or
 *         OS_EOOM.
 */
KERNELAPI oserr_t KERNELABI
ControlHandleSet(
        _In_ uuid_t              setHandle,
        _In_ int                 operation,
        _In_ uuid_t              handle,
        _In_ struct ioset_event* event);

/**
 * @brief Waits for events in a handle set, and stores them in the events array.
 * Queued events are taken until the array is full, or there are no more. If
 * there are none at all, and pollEvents is 0, the thread sleeps until an event
 * is posted, or the deadline is reached.
 * When pollEvents is not 0, the first pollEvents entries of the array are
 * already filled in by the caller. New events with the same context as one of
 * these entries are combined into that entry, otherwise they are added after
 * them.
 * If asyncContext is given and the thread has to sleep, the wait is done
 * asynchronously instead. In this case OS_EFORKED is returned right away, while
 * a forked copy of the thread does the actual waiting.
 *
 * @param handle       The handle set. The caller must have a grant for it.
 * @param asyncContext Context for an asynchronous wait, or NULL to wait normally.
 * @param events       The array that receives the events.
 * @param maxEvents    The number of entries in events. Must be above 0.
 * @param pollEvents   The number of entries in events that are already filled in.
 *                     Must be between 0 and maxEvents.
 * @param deadline     When to stop waiting, or NULL to wait without a time limit.
 * @param numEventsOut Receives the number of entries in events that are filled in,
 *                     including the pollEvents entries.
 * @return OS_EOK, OS_EFORKED if the wait continues asynchronously, OS_EINVALPARAMS
 *         for invalid parameters, an error from looking up the set, or an error
 *         from FutexWait, for instance OS_ETIMEOUT when the deadline is reached.
 */
KERNELAPI oserr_t KERNELABI
WaitForHandleSet(
        _In_  uuid_t              handle,
        _In_  OSAsyncContext_t*   asyncContext,
        _In_  struct ioset_event* events,
        _In_  int                 maxEvents,
        _In_  int                 pollEvents,
        _In_  OSTimestamp_t*      deadline,
        _Out_ int*                numEventsOut);

/**
 * @brief Posts events on a handle, which wakes up the handle sets that watch it.
 * Events are posted on the resource behind the handle. So for service references
 * and SHM views, every subscription on the same resource gets them, no matter
 * which reference it was added with. Any other number is used as it is. Each
 * subscription only gets the events it asked for.
 * No access checks are done, so the caller needs no grant for the handle, and
 * posting events gives no access to the resource. No reference to the handle is
 * kept after the call. This is safe to call from interrupt handlers.
 *
 * @param handle The handle, or number, to post events on.
 * @param flags  The events to post, as defined in ioset.h.
 * @return OS_EOK, OS_ENOENT if nothing watches the resource, or an error if a
 *         temporary reference to the handle could not be taken.
 */
KERNELAPI oserr_t KERNELABI
MarkHandle(
        _In_ uuid_t       handle,
        _In_ unsigned int flags);

#endif //! __HANDLES_SET_H__
