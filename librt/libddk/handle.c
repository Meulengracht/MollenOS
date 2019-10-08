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
#include <ddk/handle.h>

OsStatus_t
HandleCreate(
    _Out_ UUId_t* HandleOut)
{
    if (!HandleOut) {
        return OsInvalidParameters;
    }
    return Syscall_CreateHandle(HandleOut);
}

OsStatus_t
HandleSetCreate(
    _In_  Flags_t Flags,
    _Out_ UUId_t* HandleOut)
{
    if (!HandleOut) {
        return OsInvalidParameters;
    }
    return Syscall_CreateHandleSet(Flags, HandleOut);
}

OsStatus_t
HandleSetControl(
    _In_ UUId_t  SetHandle,
    _In_ int     Operation,
    _In_ UUId_t  Handle,
    _In_ Flags_t Flags,
    _In_ int     Context)
{
    return Syscall_ControlHandleSet(SetHandle, Operation, Handle, Flags, Context);
}

OsStatus_t
HandleSetListen(
    _In_ UUId_t           Handle,
    _In_ struct io_event* Events,
    _In_ int              MaxEvents,
    _In_ size_t           Timeout)
{
    if (!Events) {
        return OsInvalidParameters;
    }
    return Syscall_ListenHandleSet(Handle, Events, MaxEvents, Timeout);
}

OsStatus_t
HandleSetActivity(
    _In_ UUId_t  Handle,
    _In_ Flags_t Flags)
{
    return Syscall_HandleSetActivity(Handle, Flags);
}
