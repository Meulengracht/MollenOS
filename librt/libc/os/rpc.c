/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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

/* Includes
 * - System */
#include <os/syscall.h>
#include <os/ipc/ipc.h>

/* Includes
 * - Library */
#include <stdlib.h>
#include <signal.h>

/* This is for both a kernel solution and a userspace
 * solution of IPC can co-exist */
#ifdef LIBC_KERNEL
__EXTERN
OsStatus_t 
ScRpcExecute(
	_In_ MRemoteCall_t *RemoteCall,
	_In_ int            Asynchronous);

/* RPCExecute/RPCEvent
 * To get a reply from the RPC request, the user
 * must use RPCExecute, this will automatically wait
 * for a reply, whereas RPCEvent will send the request
 * and not block/wait for reply */
OsStatus_t
RPCExecute(
	_In_ MRemoteCall_t *RemoteCall)
{
	RemoteCall->From.Port = -1;
	return ScRpcExecute(RemoteCall, 0);
}

#else

/* RPCExecute/RPCEvent
 * To get a reply from the RPC request, the user
 * must use RPCExecute, this will automatically wait
 * for a reply, whereas RPCEvent will send the request
 * and not block/wait for reply */
OsStatus_t 
RPCExecute(
	_In_ MRemoteCall_t *RemoteCall) {
    return Syscall_RemoteCall(RemoteCall, 0);
}

/* RPCExecute/RPCEvent
 * To get a reply from the RPC request, the user
 * must use RPCExecute, this will automatically wait
 * for a reply, whereas RPCEvent will send the request
 * and not block/wait for reply */
OsStatus_t 
RPCEvent(
	_In_ MRemoteCall_t *RemoteCall) {
    return Syscall_RemoteCall(RemoteCall, 1);
}

/* RPCListen 
 * Call this to wait for a new RPC message, it automatically
 * reads the message, and all the arguments. To avoid freeing
 * an argument, set InUse to 0 */
OsStatus_t 
RPCListen(
	_In_ MRemoteCall_t  *Message,
    _In_ void           *ArgumentBuffer) {
    return Syscall_RemoteCallWait(PIPE_REMOTECALL, Message, ArgumentBuffer);
}

/* RPCRespond
 * This is a wrapper to return a respond message/buffer to the
 * sender of the message, it's good practice to always wait for
 * a result when there is going to be one */ 
OsStatus_t 
RPCRespond(
    _In_ MRemoteCallAddress_t*  RemoteAddress,
    _In_ const void*            Buffer, 
    _In_ size_t                 Length)
{
	return Syscall_RemoteCallRespond(RemoteAddress, (void*)Buffer, Length);
}

#endif
