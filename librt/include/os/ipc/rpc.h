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
 * - Remote Procedure Call routines
 */

#ifndef _MOLLENOS_RPC_H_
#define _MOLLENOS_RPC_H_

/* Guard against inclusion */
#ifndef _MOLLENOS_IPC_H_
#error "You must include ipc.h and not this directly"
#endif

/* Includes 
 * - System */
#include <os/osdefs.h>

/* Includes
 * - C-Library */
#include <string.h>

/* The argument package that can be passed
 * to an IPC function request, we support up
 * to 5 arguments */
typedef struct _MRPCArgument {
	int					InUse;
	const void			*Buffer;
	size_t				Length;
} RPCArgument_t;

/* The base event message structure, any IPC
 * action going through pipes in MollenOS must
 * inherit from this structure for security */
typedef struct _MRemoteCall {
	int					Function;
	int					Port;
	size_t				Length;		/* Excluding this header */
	IpcComm_t			Sender;		/* Automatically set by OS */
	RPCArgument_t		Arguments[IPC_MAX_ARGUMENTS];
	RPCArgument_t		Result;
} MRemoteCall_t;

/* Cpp Guard */
#ifdef __cplusplus
extern "C" {
#endif

/* RPCInitialize 
 * Initializes a new RPC message of the 
 * given type and length */
static __CRT_INLINE void RPCInitialize(MRemoteCall_t *Ipc, int Port, int Function)
{
	memset((void*)Ipc, 0, sizeof(MRemoteCall_t));
	Ipc->Function = Function;
	Ipc->Port = Port;
}

/* RPCSetArgument
 * Adds a new argument for the RPC request at
 * the given argument index. It's not possible to override 
 * a current argument */
static __CRT_INLINE void RPCSetArgument(MRemoteCall_t *Ipc,
	int Index, const void *Data, size_t Length)
{
	/* Sanitize the index and the
	 * current argument */
	if (Index >= IPC_MAX_ARGUMENTS
		|| Index < 0 || Ipc->Arguments[Index].InUse == 1) {
		return;
	}

	Ipc->Arguments[Index].InUse = 1;
	Ipc->Arguments[Index].Buffer = Data;
	Ipc->Arguments[Index].Length = Length;
	Ipc->Length += Length;
}

/* RPCSetResult
 * Installs a result buffer that will be filled
 * with the response from the RPC request */
static __CRT_INLINE void RPCSetResult(MRemoteCall_t *Ipc,
	const void *Data, size_t Length)
{
	Ipc->Result.InUse = 1;
	Ipc->Result.Buffer = Data;
	Ipc->Result.Length = Length;
}

/* RPCEvaluate/RPCExecute
 * To get a reply from the RPC request, the user
 * must use RPCEvaluate, this will automatically wait
 * for a reply, whereas RPCExecute will send the request
 * and not block/wait for reply */
_MOS_API OsStatus_t RPCEvaluate(MRemoteCall_t *Ipc, IpcComm_t Target);
_MOS_API OsStatus_t RPCExecute(MRemoteCall_t *Ipc, IpcComm_t Target);

#ifdef __cplusplus
}
#endif

#endif //!_MOLLENOS_RPC_H_
