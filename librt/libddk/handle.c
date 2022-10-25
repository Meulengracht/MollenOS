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

#include <internal/_syscalls.h>
#include <internal/_utils.h>
#include <ddk/handle.h>

oserr_t
handle_create(
        _Out_ uuid_t* handleOut)
{
    if (!handleOut) {
        return OS_EINVALPARAMS;
    }
    return Syscall_CreateHandle(handleOut);
}

oserr_t
handle_destroy(
        _In_ uuid_t handle)
{
    return Syscall_DestroyHandle(handle);
}

oserr_t
handle_set_path(
        _In_ uuid_t      handle,
        _In_ const char* path)
{
    if (!path) {
        return OS_EINVALPARAMS;
    }
    return Syscall_RegisterHandlePath(handle, path);
}

oserr_t
handle_post_notification(
        _In_ uuid_t       handle,
        _In_ unsigned int flags)
{
    return Syscall_HandleSetActivity(handle, flags);
}

oserr_t
notification_queue_create(
        _In_  unsigned int flags,
        _Out_ uuid_t*      handleOut)
{
    if (!handleOut) {
        return OS_EINVALPARAMS;
    }
    return Syscall_CreateHandleSet(flags, handleOut);
}

oserr_t
notification_queue_ctrl(
        _In_ uuid_t              setHandle,
        _In_ int                 operation,
        _In_ uuid_t              handle,
        _In_ struct ioset_event* event)
{
    return Syscall_ControlHandleSet(setHandle, operation, handle, event);
}

oserr_t
notification_queue_wait(
        _In_  uuid_t              handle,
        _In_  struct ioset_event* events,
        _In_  int                 maxEvents,
        _In_  int                 pollEvents,
        _In_  size_t              timeout,
        _Out_ int*                numEventsOut)
{
    HandleSetWaitParameters_t parameters = {
        .events = events,
        .maxEvents = maxEvents,
        .timeout = timeout,
        .pollEvents = pollEvents
    };
    
    if (!events || !numEventsOut) {
        return OS_EINVALPARAMS;
    }
    
    return Syscall_ListenHandleSet(handle, &parameters, numEventsOut);
}
