/**
 * MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 * Inter-Process Communication Interface
 */

#include <internal/_syscalls.h>
#include <os/ipc.h>

OsStatus_t
IpcInvoke(
    _In_ thrd_t        Target,
    _In_ IpcMessage_t* Message, 
    _In_ unsigned int  Flags,
    _In_ int           Timeout)
{
    return Syscall_IpcInvoke(Target, Message, Flags, Timeout);
}

OsStatus_t
IpcGetResponse(
    _In_  size_t Timeout,
    _Out_ void** BufferOut)
{
    return Syscall_IpcGetResponse(Timeout, BufferOut);
}

OsStatus_t
IpcReply(
    _In_  void*  Buffer,
    _In_  size_t Length)
{
    return Syscall_IpcReply(Buffer, Length);
}

OsStatus_t
IpcListen(
    _In_  size_t         Timeout,
    _Out_ IpcMessage_t** MessageOut)
{
    return Syscall_IpcListen(Timeout, MessageOut);
}

OsStatus_t
IpcReplyAndListen(
    _In_  void*          Buffer,
    _In_  size_t         Length,
    _In_  size_t         Timeout,
    _Out_ IpcMessage_t** MessageOut)
{
    return Syscall_IpcReplyAndListen(Buffer, Length, Timeout, MessageOut);
}
