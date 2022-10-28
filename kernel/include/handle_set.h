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
 * - Implementation of the resource handle interface. This provides system-wide
 *   resource handles and maintience of resources. 
 */

#ifndef __HANDLE_SET_H__
#define __HANDLE_SET_H__

#include <os/types/syscall.h>

struct ioset_event;

KERNELAPI oserr_t KERNELABI HandleSetsInitialize(void);

/**
 * CreateHandleSet
 * * Creates a new handle set that can be used for asynchronus events.
 * @param flags [In] Creation flags that configure the new handle set behaviour.
 */
KERNELAPI uuid_t KERNELABI
CreateHandleSet(
    _In_  unsigned int flags);

/**
 * ControlHandleSet
 * * Add, remove or modify a handle in the set.
 * @param SetHandle [In] The handle of the handle set.
 * @param Operation [In] The operation that should be performed.
 * @param Handle    [In] The handle that should be operated on.
 * @param Flags     [In] The flags that should be configured with the handle.
 * @param Context   [In] The context pointer that should be included in events.
 */
KERNELAPI oserr_t KERNELABI
ControlHandleSet(
        _In_ uuid_t              setHandle,
        _In_ int                 operation,
        _In_ uuid_t              handle,
        _In_ struct ioset_event* event);

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
KERNELAPI oserr_t KERNELABI
WaitForHandleSet(
        _In_  uuid_t              handle,
        _In_  OSSyscallContext_t* syscallContext,
        _In_  struct ioset_event* events,
        _In_  int                 maxEvents,
        _In_  int                 pollEvents,
        _In_  size_t              timeout,
        _Out_ int*                numEventsOut);

/** 
 * @brief Marks a handle that an event has been completed. If the handle has any
 * sets registered they will be notified.
 * @param handle [In] The handle upon which an event has taken place
 * @param flags  [In] The event flags that are defined in ioset.h.
 */
KERNELAPI oserr_t KERNELABI
MarkHandle(
        _In_ uuid_t       handle,
        _In_ unsigned int flags);

#endif //! __HANDLE_SET_H__
