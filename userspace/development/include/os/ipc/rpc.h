/* MollenOS
 *
 * Copyright 2011 - 2016, Philip Meulengracht
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
 * MollenOS Remote Procedure Call Interface
 * - MollenOS SDK, used as a basis for drivers
 */

#ifndef _MCORE_RPC_H_
#define _MCORE_RPC_H_

/* Includes
 * - C-Library */
#include <os/syscall.h>
#include <os/osdefs.h>
#include <string.h>

/* The maximum number of arguments for a RPC procedure
 * Do not change this value, it will result in undefined
 * behaviour for the running application */
#define RPC_MAX_ARGS					5
#define DECLRPC(__func, __args, __rettype, ...)	MCoreRPCResult * ##__func##RPC##__args { \
 void *ReturnValue = __func(__VA_ARGS__); \
 return RPCReturn(ReturnValue, sizeof(__rettype)); \
}

/* RPC argument type, indicates whether the
 * data passed is a value or a pointer */
typedef enum _MCoreRPCArgumentType {
	RPCValue,
	RPCPointer
} MCoreRPCArgumentType;

/* RPC argument package, describes an argument for
 * an RPC procedure, and how it should be handled/copied
 * in target address space */
typedef struct _MCoreRPCArgument {
	MCoreRPCArgumentType	Type;
	int 					InUse;
	const void 				*Data;
	size_t 					Length;
} MCoreRPCArgument_t;
typedef struct _MCoreRPCArgument MCoreRPCResult;

/* The base of a RPC structure, and is
 * used by any RPC procedure to indicate which
 * library/process to access, the function it
 * should evaluate and how parameters are */
typedef struct _MCoreRPC {
	const char 			*Library;
	const char 			*Function;
	int 				InterfaceVersion;
	MCoreRPCArgument_t 	Arguments[RPC_MAX_ARGS];
	void				*ResultBuffer;
} MCoreRPC_t;

/* Guard against CPP code */
#ifdef __cplusplus
extern "C" {
#endif

/* RPCInitialize
 * Helper function to initialize an RPC 
 * structure for a remote procedure */
static __CRT_INLINE void RPCInitialize(MCoreRPC_t *Rpc, 
	const char *Library, const char *Function, int InterfaceVersion)
{
	memset(Rpc, 0, sizeof(MCoreRPC_t));
	Rpc->Library = Library;
	Rpc->Function = Function;
	Rpc->InterfaceVersion = InterfaceVersion;
}

/* RPCSetArgument
 * Helper function to initialize an RPC
 * argument for the remote procedure */
static __CRT_INLINE void RPCSetArgument(MCoreRPC_t *Rpc,
	int Index, const void *Data, size_t Length)
{
	/* Sanitize index */
	if (Index >= RPC_MAX_ARGS
		|| Index < 0) {
		return;
	}

	Rpc->Arguments[Index].InUse = 1;
	Rpc->Arguments[Index].Data = Data;
	Rpc->Arguments[Index].Length = Length;
}

/* RPCEvaluate
 * Helper function for executing a remote procedure
 * call, it returns the data from the function, if
 * no values are expected a NULL will be passed.
 * If the call was succesfull, ErrorCode will be 0 */
static __CRT_INLINE void *RPCEvaluate(MCoreRPC_t *Rpc, int *ErrorCode)
{
	/* This will be a wrapper for a system call */
	return (void*)Syscall2(SYSCALL_RPCEVAL, 
		SYSCALL_PARAM(Rpc), SYSCALL_PARAM(ErrorCode));
}

/* End of the cpp guard */
#ifdef __cplusplus
}
#endif

#endif //!_MCORE_RPC_H_
