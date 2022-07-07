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
KERNELAPI oscode_t KERNELABI
UserEventCreate(
        _In_  unsigned int initialValue,
        _In_  unsigned int flags,
        _Out_ uuid_t*      handleOut,
        _Out_ atomic_int** syncAddressOut);

/**
 * Signals a userevent handle, based on the type of userevent a notification is also raised on the handle.
 * @param handle The handle to signal the event on. Must be an user-event handle.
 * @return       Status of the signal operation.
 */
KERNELAPI oscode_t KERNELABI
UserEventSignal(
        _In_ uuid_t handle);

#endif //!__VALI_MUTEX_H__
