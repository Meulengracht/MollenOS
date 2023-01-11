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

#define __TRACE

#include <ddk/handle.h>
#include <internal/_syscalls.h>
#include <os/types/syscall.h>

oserr_t
OSHandleCreate(
        _Out_ uuid_t* handleOut)
{
    if (!handleOut) {
        return OS_EINVALPARAMS;
    }
    return Syscall_CreateHandle(handleOut);
}

oserr_t
OSHandleDestroy(
        _In_ uuid_t handle)
{
    return Syscall_DestroyHandle(handle);
}

oserr_t
OSNotificationQueuePost(
        _In_ uuid_t       handle,
        _In_ unsigned int flags)
{
    return Syscall_HandleSetActivity(handle, flags);
}

oserr_t
OSNotificationQueueCreate(
        _In_  unsigned int flags,
        _Out_ uuid_t*      handleOut)
{
    if (!handleOut) {
        return OS_EINVALPARAMS;
    }
    return Syscall_CreateHandleSet(flags, handleOut);
}

oserr_t
OSNotificationQueueCtrl(
        _In_ uuid_t              setHandle,
        _In_ int                 operation,
        _In_ uuid_t              handle,
        _In_ struct ioset_event* event)
{
    return Syscall_ControlHandleSet(setHandle, operation, handle, event);
}

oserr_t
OSNotificationQueueWait(
        _In_  uuid_t              handle,
        _In_  struct ioset_event* events,
        _In_  int                 maxEvents,
        _In_  int                 pollEvents,
        _In_  OSTimestamp_t*      deadline,
        _Out_ int*                numEventsOut,
        _In_  OSAsyncContext_t*   asyncContext)
{
    oserr_t                   oserr;
    HandleSetWaitParameters_t parameters = {
        .Events = events,
        .MaxEvents = maxEvents,
        .Deadline = deadline,
        .PollEvents = pollEvents
    };
    
    if (!events || !numEventsOut) {
        return OS_EINVALPARAMS;
    }

    oserr = Syscall_ListenHandleSet(handle, asyncContext, &parameters, numEventsOut);
    if (oserr == OS_EFORKED) {
        // The system call was postponed, so we should coordinate with the
        // userspace threading system right here.
        usched_wait_async();
        return asyncContext->ErrorCode;
    }
    return oserr;
}
