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
	int						Type;
	union {
		__CONST void	*Buffer;
		size_t				Value;
	} Data;
	size_t					Length;
} RPCArgument_t;

/* The base event message structure, any IPC
 * action going through pipes in MollenOS must
 * inherit from this structure for security */
typedef struct _MRemoteCall {
	int						Version;
	int						Function;
	int						Port;
	int						ResponsePort;
	size_t					Length;		/* Excluding this header */
	UUId_t					Sender;		/* Automatically set by OS */
	RPCArgument_t			Arguments[IPC_MAX_ARGUMENTS];
	RPCArgument_t			Result;
} MRemoteCall_t;

/* Cpp Guard */
#ifdef __cplusplus
extern "C" {
#endif

/* RPCInitialize 
 * Initializes a new RPC message of the 
 * given type and length */
static __CRT_INLINE void RPCInitialize(MRemoteCall_t *Ipc, 
	int Version, int Port, int Function)
{
	/* Zero out structure */
	memset((void*)Ipc, 0, sizeof(MRemoteCall_t));

	/* Initialize some of the args */
	Ipc->Version = Version;
	Ipc->Function = Function;
	Ipc->Port = Port;

	/* Standard response pipe */
	Ipc->ResponsePort = PIPE_RPC;
}

/* RPCSetArgument
 * Adds a new argument for the RPC request at
 * the given argument index. It's not possible to override 
 * a current argument */
static __CRT_INLINE void RPCSetArgument(MRemoteCall_t *Rpc,
	int Index, __CONST void *Data, size_t Length)
{
	/* Sanitize the index and the
	 * current argument */
	if (Index >= IPC_MAX_ARGUMENTS
		|| Index < 0 || Rpc->Arguments[Index].Type != ARGUMENT_NOTUSED) {
		return;
	}

	/* Kind of argument? */
	if (Length <= sizeof(size_t)) {
		Rpc->Arguments[Index].Type = ARGUMENT_REGISTER;

		if (Length == 1) {
			Rpc->Arguments[Index].Data.Value = *((uint8_t*)Data);
		}
		else if (Length == 2) {
			Rpc->Arguments[Index].Data.Value = *((uint16_t*)Data);
		}
		else if (Length == 4) {
			Rpc->Arguments[Index].Data.Value = *((uint32_t*)Data);
		}
		else if (Length == 8) {
			Rpc->Arguments[Index].Data.Value = *((size_t*)Data);
		}
	}
	else {
		Rpc->Arguments[Index].Type = ARGUMENT_BUFFER;
		Rpc->Arguments[Index].Data.Buffer = Data;
		Rpc->Arguments[Index].Length = Length;
	}

	/* Increase total length of message */
	Rpc->Length += Length;
}

/* RPCSetResult
 * Installs a result buffer that will be filled
 * with the response from the RPC request */
static __CRT_INLINE void RPCSetResult(MRemoteCall_t *Rpc,
	__CONST void *Data, size_t Length)
{
	/* Always a buffer element as we need
	 * a target to copy the data into */
	Rpc->Result.Type = ARGUMENT_BUFFER;
	Rpc->Result.Data.Buffer = Data;
	Rpc->Result.Length = Length;
}

/* RPCListen 
 * Call this to wait for a new RPC message, it automatically
 * reads the message, and all the arguments. To avoid freeing
 * an argument, set InUse to 0 */
_MOS_API OsStatus_t RPCListen(MRemoteCall_t *Message);

/* RPCCleanup 
 * Call this to cleanup the RPC message, it frees all
 * allocated resources by RPCListen */
_MOS_API OsStatus_t RPCCleanup(MRemoteCall_t *Message);

/* RPCEvaluate/RPCExecute
 * To get a reply from the RPC request, the user
 * must use RPCEvaluate, this will automatically wait
 * for a reply, whereas RPCExecute will send the request
 * and not block/wait for reply */
_MOS_API OsStatus_t RPCEvaluate(MRemoteCall_t *Rpc, UUId_t Target);
_MOS_API OsStatus_t RPCExecute(MRemoteCall_t *Rpc, UUId_t Target);

/* RPCRespond
 * This is a wrapper to return a respond message/buffer to the
 * sender of the message, it's good practice to always wait for
 * a result when there is going to be one */
_MOS_API OsStatus_t RPCRespond(MRemoteCall_t *Rpc, 
	__CONST void *Buffer, size_t Length);

#ifdef __cplusplus
}
#endif

#endif //!_MOLLENOS_RPC_H_
