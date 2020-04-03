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
 * Resource Handle Set Interface
 * - Implementation of the resource handle interface. This provides system-wide
 *   resource handles and maintience of resources. 
 */

#ifndef __HANDLE_SET_H__
#define __HANDLE_SET_H__

#include <os/osdefs.h>

typedef struct handle_event handle_event_t;

/**
 * CreateHandleSet
 * * Creates a new handle set that can be used for asynchronus events.
 * @param Flags [In] Creation flags that configure the new handle set behaviour. 
 */
KERNELAPI UUId_t KERNELABI
CreateHandleSet(
    _In_  Flags_t Flags);

/**
 * ControlHandleSet
 * * Add, remove or modify a handle in the set.
 * @param SetHandle [In] The handle of the handle set.
 * @param Operation [In] The operation that should be performed.
 * @param Handle    [In] The handle that should be operated on.
 * @param Flags     [In] The flags that should be configured with the handle.
 * @param Context   [In] The context pointer that should be included in events.
 */
KERNELAPI OsStatus_t KERNELABI
ControlHandleSet(
    _In_ UUId_t  SetHandle,
    _In_ int     Operation,
    _In_ UUId_t  Handle,
    _In_ Flags_t Flags,
    _In_ void*   Context);

/**
 * WaitForHandleSet
 * * Waits for the given handle set and stores the events that occurred in the
 * * provided array.
 * @param Handle            [In]
 * @param Events            [In]
 * @param MaxEvents         [In]
 * @param Timeout           [In]
 * @param NumberOfEventsOut [Out]
 */
KERNELAPI OsStatus_t KERNELABI
WaitForHandleSet(
    _In_  UUId_t          Handle,
    _In_  handle_event_t* Events,
    _In_  int             MaxEvents,
    _In_  size_t          Timeout,
    _Out_ int*            NumberOfEventsOut);

/** 
 * MarkHandle
 * * Marks a handle that an event has been completed. If the handle has any
 * * sets registered they will be notified.
 * @param Handle [In] The handle upon which an event has taken place
 * @param Flags  [In] The event flags which denote which kind of event.
 */
KERNELAPI OsStatus_t KERNELABI
MarkHandle(
    _In_ UUId_t  Handle,
    _In_ Flags_t Flags);

#endif //! __HANDLE_SET_H__
