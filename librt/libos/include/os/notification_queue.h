/**
 * Copyright 2023, Philip Meulengracht
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
 */

#ifndef __OS_NOTIFICATIONQUEUE_H__
#define __OS_NOTIFICATIONQUEUE_H__

#include <ioset.h>
#include <os/types/handle.h>
#include <os/types/async.h>
#include <os/types/time.h>

/**
 * Creates a new handle set that can be used for asynchronus events.
 * @param flags     Creation flags that configure the new handle set behaviour.
 * @param handleOut A unique identifier to an OS handle of the type Set
 * @return          status of the queue creation
 */
CRTDECL(oserr_t,
OSNotificationQueueCreate(
        _In_  unsigned int flags,
        _Out_ OSHandle_t*  handleOut));

/**
 * OSNotificationQueueCtrl
 * * Add, remove or modify a handle in the set.
 * @param setHandle [In] The handle of the handle set.
 * @param operation [In] The operation that should be performed.
 * @param handle    [In] The handle that should be operated on.
 * @param event     [In] The configuration for the event to be recieved.
 */
CRTDECL(oserr_t,
OSNotificationQueueCtrl(
        _In_ OSHandle_t*         setHandle,
        _In_ int                 operation,
        _In_ uuid_t              handle,
        _In_ struct ioset_event* event));

/**
 * OSNotificationQueueWait
 * * Waits for the given handle set and stores the events that occurred in the
 * * provided array.
 * @param handle    [In]
 * @param events    [In]
 * @param maxEvents [In]
 * @param timeout   [In]
 */
CRTDECL(oserr_t,
OSNotificationQueueWait(
        _In_ OSHandle_t*          setHandle,
        _In_  struct ioset_event* events,
        _In_  int                 maxEvents,
        _In_  int                 pollEvents,
        _In_  OSTimestamp_t*      deadline,
        _Out_ int*                numEventsOut,
        _In_  OSAsyncContext_t*   asyncContext));

/** 
 * OSNotificationQueuePost
 * * Marks a handle that an event has been completed. If the handle has any
 * * sets registered they will be notified.
 * @param handle [In] The handle upon which an event has taken place
 * @param flags  [In] The event flags which denote which kind of event.
 */
CRTDECL(oserr_t,
OSNotificationQueuePost(
        _In_ OSHandle_t*  handle,
        _In_ unsigned int flags));

#endif //!__OS_NOTIFICATIONQUEUE_H__
