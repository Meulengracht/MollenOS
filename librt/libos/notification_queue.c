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

#include <internal/_syscalls.h>
#include <os/types/syscall.h>
#include <os/notification_queue.h>
#include <os/handle.h>

static void __QueueDestroy(struct OSHandle*);

const OSHandleOps_t g_hqueueOps = {
        .Destroy = __QueueDestroy
};

oserr_t
OSNotificationQueueCreate(
        _In_  unsigned int flags,
        _Out_ OSHandle_t*  handleOut)
{
    oserr_t oserr;
    uuid_t  handleID;

    if (!handleOut) {
        return OS_EINVALPARAMS;
    }

    oserr = Syscall_CreateHandleSet(flags, &handleID);
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = OSHandleWrap(
            handleID,
            OSHANDLE_HQUEUE,
            NULL,
            true,
            handleOut
    );
    if (oserr != OS_EOK) {
        Syscall_DestroyHandle(handleID);
    }
    return oserr;
}

oserr_t
OSNotificationQueueCtrl(
        _In_ OSHandle_t*         setHandle,
        _In_ int                 operation,
        _In_ OSHandle_t*         handle,
        _In_ struct ioset_event* event)
{
    if (setHandle == NULL || handle == NULL) {
        return OS_EINVALPARAMS;
    }
    return Syscall_ControlHandleSet(setHandle->ID, operation, handle->ID, event);
}

oserr_t
OSNotificationQueueWait(
        _In_  OSHandle_t*         setHandle,
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
    
    if (setHandle == NULL || events == NULL || numEventsOut == NULL) {
        return OS_EINVALPARAMS;
    }

    oserr = Syscall_ListenHandleSet(setHandle->ID, asyncContext, &parameters, numEventsOut);
    if (oserr == OS_EFORKED) {
        // The system call was postponed, so we should coordinate with the
        // userspace threading system right here.
        usched_wait_async();
        return asyncContext->ErrorCode;
    }
    return oserr;
}

oserr_t
OSNotificationQueuePost(
        _In_ OSHandle_t*  handle,
        _In_ unsigned int flags)
{
    if (handle == NULL) {
        return OS_EINVALPARAMS;
    }
    return Syscall_HandleSetActivity(handle->ID, flags);
}

static void
__QueueDestroy(struct OSHandle* handle)
{
    (void)Syscall_DestroyHandle(handle->ID);
}
