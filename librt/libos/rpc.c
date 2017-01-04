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

/* RPCEvaluate/RPCExecute
 * To get a reply from the RPC request, the user
 * must use RPCEvaluate, this will automatically wait
 * for a reply, whereas RPCExecute will send the request
 * and not block/wait for reply */
OsStatus_t RPCEvaluate(MEventMessage_t *Ipc, IpcComm_t Target)
{
	return Syscall3(SYSCALL_RPCEVAL, SYSCALL_PARAM(Ipc), SYSCALL_PARAM(Target), 0);
}

/* RPCEvaluate/RPCExecute
 * To get a reply from the RPC request, the user
 * must use RPCEvaluate, this will automatically wait
 * for a reply, whereas RPCExecute will send the request
 * and not block/wait for reply */
OsStatus_t RPCExecute(MEventMessage_t *Ipc, IpcComm_t Target)
{
	return Syscall3(SYSCALL_RPCEVAL, SYSCALL_PARAM(Ipc), SYSCALL_PARAM(Target), 1);
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
int SignalProcess(IpcComm_t Target)
{
	return Syscall1(SYSCALL_PSIGSEND, SYSCALL_PARAM(Target));
}
