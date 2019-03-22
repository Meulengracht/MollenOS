/* MollenOS
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
 * MollenOS Inter-Process Communication Interface
 */

#include <internal/_syscalls.h>
#include <internal/_utils.h>
#include <ddk/ipc/ipc.h>
#include <stdlib.h>
#include <signal.h>

OsStatus_t
RPCExecute(
	_In_ MRemoteCall_t* RemoteCall)
{
    // Install sender
    RemoteCall->From.Process = *GetInternalProcessId();
    return Syscall_RemoteCall(RemoteCall, 0);
}

OsStatus_t
RPCEvent(
	_In_ MRemoteCall_t* RemoteCall)
{
    // Install sender
    RemoteCall->From.Process = *GetInternalProcessId();
    return Syscall_RemoteCall(RemoteCall, 1);
}

OsStatus_t
RPCListen(
    _In_ UUId_t         Handle,
	_In_ MRemoteCall_t* Message,
    _In_ void*          ArgumentBuffer)
{
    return Syscall_RemoteCallWait(Handle, Message, ArgumentBuffer);
}

OsStatus_t
RPCRespond(
    _In_ MRemoteCallAddress_t* RemoteAddress,
    _In_ const void*           Buffer, 
    _In_ size_t                Length)
{
	return Syscall_RemoteCallRespond(RemoteAddress, (void*)Buffer, Length);
}
