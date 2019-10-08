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

struct io_event;

/**
 * HandleCreate
 * * Allocates a new handle for a system resource with a reference of 1.
 */
DDKDECL(OsStatus_t,
HandleCreate(
    _Out_ UUId_t* HandleOut));

/**
 * HandleSetCreate
 * * Creates a new handle set that can be used for asynchronus events.
 * @param Flags [In] Creation flags that configure the new handle set behaviour. 
 */
DDKDECL(OsStatus_t,
HandleSetCreate(
    _In_  Flags_t Flags,
    _Out_ UUId_t* HandleOut));

/**
 * HandleSetControl
 * * Add, remove or modify a handle in the set.
 * @param SetHandle [In] The handle of the handle set.
 * @param Operation [In] The operation that should be performed.
 * @param Handle    [In] The handle that should be operated on.
 * @param Flags     [In] The flags that should be configured with the handle.
 */
DDKDECL(OsStatus_t,
HandleSetControl(
    _In_ UUId_t  SetHandle,
    _In_ int     Operation,
    _In_ UUId_t  Handle,
    _In_ Flags_t Flags,
    _In_ int     Context));

/**
 * HandleSetListen
 * * Waits for the given handle set and stores the events that occurred in the
 * * provided array.
 * @param Handle    [In]
 * @param Events    [In]
 * @param MaxEvents [In]
 * @param Timeout   [In]
 */
DDKDECL(OsStatus_t,
HandleSetListen(
    _In_ UUId_t           Handle,
    _In_ struct io_event* Events,
    _In_ int              MaxEvents,
    _In_ size_t           Timeout));

/** 
 * HandleSetActivity
 * * Marks a handle that an event has been completed. If the handle has any
 * * sets registered they will be notified.
 * @param Handle [In] The handle upon which an event has taken place
 * @param Flags  [In] The event flags which denote which kind of event.
 */
DDKDECL(OsStatus_t,
HandleSetActivity(
    _In_ UUId_t  Handle,
    _In_ Flags_t Flags));

#endif //!__DDK_HANDLE_H__
