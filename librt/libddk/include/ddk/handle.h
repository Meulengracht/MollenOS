/**
 * MollenOS
 *
 * Copyright 2019, Philip Meulengracht
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
 * Handle and HandleSets Support Definitions & Structures
 * - This header describes the base handle-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __DDK_HANDLE_H__
#define __DDK_HANDLE_H__

#include <ddk/ddkdefs.h>
#include <ioset.h>

/**
 * Allocates a new handle for a system resource with a reference of 1.
 * @param handleOut A unique identifier to a generic OS handle
 * @return          Status of the handle creation
 */
DDKDECL(oscode_t,
        handle_create(
    _Out_ uuid_t* handleOut));

/**
 * Reduces the refcount by 1, when it reaches 0 the handle is destroyed.
 * @param handle A generic OS handle
 * @return       Status of the operation
 */
DDKDECL(oscode_t,
        handle_destroy(
    _In_ uuid_t handle));

/**
 * Registers a unique system wide path for the handle, so the handle can be accessed across the system
 * without knowing handle itself
 * @param handle A generic OS handle
 * @param path   The path to be registered for the handle
 * @return       Whether or not the path was successfully registered for the handle
 */
DDKDECL(oscode_t,
        handle_set_path(
    _In_ uuid_t      handle,
    _In_ const char* path));

/**
 * Creates a new handle set that can be used for asynchronus events.
 * @param flags     Creation flags that configure the new handle set behaviour.
 * @param handleOut A unique identifier to an OS handle of the type Set
 * @return          status of the queue creation
 */
DDKDECL(oscode_t,
        notification_queue_create(
    _In_  unsigned int flags,
    _Out_ uuid_t*      handleOut));

/**
 * notification_queue_ctrl
 * * Add, remove or modify a handle in the set.
 * @param setHandle [In] The handle of the handle set.
 * @param operation [In] The operation that should be performed.
 * @param handle    [In] The handle that should be operated on.
 * @param event     [In] The configuration for the event to be recieved.
 */
DDKDECL(oscode_t,
        notification_queue_ctrl(
    _In_ uuid_t              setHandle,
    _In_ int                 operation,
    _In_ uuid_t              handle,
    _In_ struct ioset_event* event));

/**
 * notification_queue_wait
 * * Waits for the given handle set and stores the events that occurred in the
 * * provided array.
 * @param handle    [In]
 * @param events    [In]
 * @param maxEvents [In]
 * @param timeout   [In]
 */
DDKDECL(oscode_t,
        notification_queue_wait(
    _In_  uuid_t              handle,
    _In_  struct ioset_event * events,
    _In_  int                 maxEvents,
    _In_  int                 pollEvents,
    _In_  size_t              timeout,
    _Out_ int*                numEventsOut));

/** 
 * handle_post_notification
 * * Marks a handle that an event has been completed. If the handle has any
 * * sets registered they will be notified.
 * @param handle [In] The handle upon which an event has taken place
 * @param flags  [In] The event flags which denote which kind of event.
 */
DDKDECL(oscode_t,
        handle_post_notification(
    _In_ uuid_t       handle,
    _In_ unsigned int flags));

#endif //!__DDK_HANDLE_H__
