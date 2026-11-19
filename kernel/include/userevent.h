/**
 * MollenOS
 *
 * Copyright 2020, Philip Meulengracht
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
 * Synchronization (UserEvent)
 * - Userspace events that can emulate some of the linux event descriptors like eventfd, timerfd etc. They also
 *   provide binary semaphore functionality that can provide light synchronization primitives for interrupts
 */

#ifndef __VALI_USEREVENT_H__
#define __VALI_USEREVENT_H__

#include <os/osdefs.h>
#include <event.h>

/**
 * Initializes the user event memory region. This is where the user events are allocated.
 */
KERNELAPI void KERNELABI
UserEventInitialize(void);

/**
 * Creates a new type of user event handle. Provides an synchronization address that can be used for quick
 * synchronization in userspace.
 * @param initialValue   The inital value of the event, this has different meaning based on the values of flags.
 * @param flags          The configuration of the event, this changes how the event behaves.
 * @param handleOut      The kernal handle for the user event, can then be used in handle sets.
 * @param syncAddressOut The userspace synchronization address where the event is signalled.
 * @return               Status of the creation.
 */
KERNELAPI oserr_t KERNELABI
UserEventCreate(
        _In_  unsigned int initialValue,
        _In_  unsigned int flags,
        _Out_ uuid_t*      handleOut,
        _Out_ atomic_int** syncAddressOut);

/**
 * @brief Signal an event from kernel or interrupt context.
 * The caller must hold a kernel reference for the entire call. Interrupt binding
 * validates the driver's grant and keeps this reference until callbacks stop.
 * This function does not check the interrupted process's grants.
 *
 * The event counter is shared with user space, where read() takes one unit
 * from it. A signal adds one unit, unless the counter is already at the
 * event's maximum (1 for a reset event). When it adds a unit, it then wakes
 * one reader that sleeps in read() on the counter, and marks the handle in
 * the I/O sets that watch it (IOSETSYN, or IOSETTIM for a timeout event).
 * A signal that finds the counter at its maximum changes nothing: it wakes
 * nobody and queues no I/O set event, because the reader that takes the
 * units already in the counter covers this signal too.
 *
 * A caller that stores data for the reader must store it before the signal,
 * and the reader must take the counter before it takes the data. Then no
 * data is left behind with the counter at 0 and no I/O set event queued.
 *
 * @param handle Referenced user-event handle to signal.
 * @return OS_EINCOMPLETE when a unit was added, OS_EOK when the counter was
 *         already at its maximum, or OS_EINVALPARAMS for an invalid event.
 *         Both OS_EINCOMPLETE and OS_EOK mean the event is signalled. A failed
 *         wake or I/O set mark is logged and does not change the result.
 */
KERNELAPI oserr_t KERNELABI
UserEventSignal(
    _In_ uuid_t handle);

#endif //!__VALI_MUTEX_H__
