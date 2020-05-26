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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Handle and HandleSets Support Definitions & Structures
 * - This header describes the base handle-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __DDK_HANDLE_H__
#define __DDK_HANDLE_H__

#include <ddk/ddkdefs.h>
#include <ioevt.h>

/**
 * handle_create
 * * Allocates a new handle for a system resource with a reference of 1.
 */
DDKDECL(OsStatus_t,
handle_create(
    _Out_ UUId_t* handle_out));

/**
 * handle_destroy
 * * Reduces the refcount by 1, when it reaches 0 the handle is destroyed.
 */
DDKDECL(OsStatus_t,
handle_destroy(
    _In_ UUId_t handle));

/**
 * handle_set_path
 * * Reduces the refcount by 1, when it reaches 0 the handle is destroyed.
 */
DDKDECL(OsStatus_t,
handle_set_path(
    _In_ UUId_t      handle,
    _In_ const char* path));

/**
 * handle_set_create
 * * Creates a new handle set that can be used for asynchronus events.
 * @param flags [In] Creation flags that configure the new handle set behaviour. 
 */
DDKDECL(OsStatus_t,
handle_set_create(
    _In_  Flags_t flags,
    _Out_ UUId_t* handle_out));

/**
 * handle_set_ctrl
 * * Add, remove or modify a handle in the set.
 * @param setHandle [In] The handle of the handle set.
 * @param operation [In] The operation that should be performed.
 * @param handle    [In] The handle that should be operated on.
 * @param event     [In] The configuration for the event to be recieved.
 */
DDKDECL(OsStatus_t,
handle_set_ctrl(
    _In_ UUId_t              setHandle,
    _In_ int                 operation,
    _In_ UUId_t              handle,
    _In_ struct ioevt_event* event));

/**
 * handle_set_wait
 * * Waits for the given handle set and stores the events that occurred in the
 * * provided array.
 * @param handle    [In]
 * @param events    [In]
 * @param maxEvents [In]
 * @param timeout   [In]
 */
DDKDECL(OsStatus_t,
handle_set_wait(
    _In_  UUId_t              handle,
    _In_  struct ioevt_event* events,
    _In_  int                 maxEvents,
    _In_  int                 pollEvents,
    _In_  size_t              timeout,
    _Out_ int*                numEventsOut));

/** 
 * handle_set_activity
 * * Marks a handle that an event has been completed. If the handle has any
 * * sets registered they will be notified.
 * @param handle [In] The handle upon which an event has taken place
 * @param flags  [In] The event flags which denote which kind of event.
 */
DDKDECL(OsStatus_t,
handle_set_activity(
    _In_ UUId_t  handle,
    _In_ Flags_t flags));

#endif //!__DDK_HANDLE_H__
