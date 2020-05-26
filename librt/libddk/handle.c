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

#include <internal/_syscalls.h>
#include <internal/_utils.h>
#include <ddk/handle.h>

OsStatus_t
handle_create(
    _Out_ UUId_t* handle_out)
{
    if (!handle_out) {
        return OsInvalidParameters;
    }
    return Syscall_CreateHandle(handle_out);
}

OsStatus_t
handle_destroy(
    _In_ UUId_t handle)
{
    return Syscall_DestroyHandle(handle);
}

OsStatus_t
handle_set_path(
    _In_ UUId_t      handle,
    _In_ const char* path)
{
    if (!path) {
        return OsInvalidParameters;
    }
    return Syscall_RegisterHandlePath(handle, path);
}

OsStatus_t
handle_set_create(
    _In_  Flags_t flags,
    _Out_ UUId_t* handle_out)
{
    if (!handle_out) {
        return OsInvalidParameters;
    }
    return Syscall_CreateHandleSet(flags, handle_out);
}

OsStatus_t
handle_set_ctrl(
    _In_ UUId_t              setHandle,
    _In_ int                 operation,
    _In_ UUId_t              handle,
    _In_ struct ioevt_event* event)
{
    return Syscall_ControlHandleSet(setHandle, operation, handle, event);
}

OsStatus_t
handle_set_wait(
    _In_  UUId_t              handle,
    _In_  struct ioevt_event* events,
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
        return OsInvalidParameters;
    }
    
    return Syscall_ListenHandleSet(handle, &parameters, numEventsOut);
}

OsStatus_t
handle_set_activity(
    _In_ UUId_t  handle,
    _In_ Flags_t flags)
{
    return Syscall_HandleSetActivity(handle, flags);
}
