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

/* RPCEvaluate/RPCExecute
 * To get a reply from the RPC request, the user
 * must use RPCEvaluate, this will automatically wait
 * for a reply, whereas RPCExecute will send the request
 * and not block/wait for reply */
OsStatus_t RPCEvaluate(MRemoteCall_t *Rpc, UUId_t Target)
{
	return Syscall3(SYSCALL_RPCEVAL, SYSCALL_PARAM(Rpc), SYSCALL_PARAM(Target), 0);
}

/* RPCEvaluate/RPCExecute
 * To get a reply from the RPC request, the user
 * must use RPCEvaluate, this will automatically wait
 * for a reply, whereas RPCExecute will send the request
 * and not block/wait for reply */
OsStatus_t RPCExecute(MRemoteCall_t *Rpc, UUId_t Target)
{
	return Syscall3(SYSCALL_RPCEVAL, SYSCALL_PARAM(Rpc), SYSCALL_PARAM(Target), 1);
}

/* RPCListen 
 * Call this to wait for a new RPC message, it automatically
 * reads the message, and all the arguments. To avoid freeing
 * an argument, set InUse to 0 */
OsStatus_t RPCListen(MRemoteCall_t *Message) 
{
	/* Storage for parameters */
	int i = 0;

	/* Wait for a new rpc message */
	if (!PipeRead(PIPE_DEFAULT, Message, sizeof(MRemoteCall_t))) {
		for (i = 0; i < IPC_MAX_ARGUMENTS; i++) {
			if (Message->Arguments[i].Type == ARGUMENT_BUFFER) {
				Message->Arguments[i].Data.Buffer = 
					(__CRT_CONST void*)malloc(Message->Arguments[i].Length);
				PipeRead(PIPE_DEFAULT, (void*)Message->Arguments[i].Data.Buffer, 
					Message->Arguments[i].Length);
			}
			else if (Message->Arguments[i].Type == ARGUMENT_NOTUSED) {
				Message->Arguments[i].Data.Buffer = NULL;
			}
		}

		/* Done! */
		return OsNoError;
	}
	else {
		/* Something went wrong, wtf? */
		return OsError;
	}
}

/* RPCCleanup 
 * Call this to cleanup the RPC message, it frees all
 * allocated resources by RPCListen */
OsStatus_t RPCCleanup(MRemoteCall_t *Message)
{
	/* Cleanup buffers */
	for (int i = 0; i < IPC_MAX_ARGUMENTS; i++) {
		if (Message->Arguments[i].Type == ARGUMENT_BUFFER) {
			free((void*)Message->Arguments[i].Data.Buffer);
			Message->Arguments[i].Data.Buffer = NULL;
		}
	}

	/* Done */
	return OsNoError;
}

/* RPCRespond
 * This is a wrapper to return a respond message/buffer to the
 * sender of the message, it's good practice to always wait for
 * a result when there is going to be one */
OsStatus_t RPCRespond(MRemoteCall_t *Rpc,
	__CRT_CONST void *Buffer, size_t Length)
{
	/* Write the result back to the caller */
	return PipeSend(Rpc->Sender, Rpc->ResponsePort, Buffer, Length);
}

/* IPC - Sleep
 * This suspends the current process-thread
 * and puts it in a sleep state untill either
 * the timeout runs out or it recieves a wake
 * signal. */
int WaitForSignal(size_t Timeout)
{
	return Syscall1(SYSCALL_PSIGWAIT, SYSCALL_PARAM(Timeout));
}

/* IPC - Wake
 * This wakes up a thread in suspend mode on the
 * target. This should be used in conjunction with
 * the Sleep. */
int SignalProcess(UUId_t Target)
{
	return Syscall1(SYSCALL_PSIGSEND, SYSCALL_PARAM(Target));
}
