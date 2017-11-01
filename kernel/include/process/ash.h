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
 * MollenOS MCore - Server & Process Management
 * - The process/server manager is known as Phoenix
 */

#ifndef _MCORE_ASH_H_
#define _MCORE_ASH_H_

/* Includes 
 * - C-Library */
#include <os/osdefs.h>
#include <ds/blbitmap.h>
#include <ds/mstring.h>
#include <ds/list.h>
#include <signal.h>

/* Includes
 * - System */
#include <system/addresspace.h>
#include <process/pe.h>
#include <mutex.h>
#include <pipe.h>

/* Settings for ashes in the system, they
 * should not really be tinkered with, these are
 * just some arbitrary sizes that are good enough */
#define ASH_STACK_INIT			0x1000
#define ASH_STACK_MAX			(4 << 20)
#define ASH_PIPE_SIZE			0x2000

/* Ash Queries
 * List of the different options
 * for querying of ashes */
typedef enum _AshQueryFunction {
	AshQueryName,
	AshQueryMemory,
	AshQueryParent,
	AshQueryTopMostParent
} AshQueryFunction_t;

/* Signal Table 
 * This is used for interrupt-signals
 * across threads and processes */
typedef struct _MCoreSignalTable {
	uintptr_t Handlers[NUMSIGNALS + 1];
} MCoreSignalTable_t;

/* A Signal Entry 
 * This is used to describe a signal 
 * that is waiting for execution */
typedef struct _MCoreSignal {
	int Signal;
	uintptr_t Handler;
	Context_t Context;
} MCoreSignal_t;

/* This is the different types of ashes
 * that exists in MollenOS */
typedef enum _MCoreAshType
{
	AshBase,
	AshServer,
	AshProcess
} MCoreAshType_t;

/* The phoenix base structure, this contains
* basic information shared across all ashes
* Whether it's a process or a server */
typedef struct _MCoreAsh
{
	// Ids related to this Ash,
	// both it's own id, it's main thread
	// and it's parent ash
	UUId_t MainThread;
	UUId_t Id;
	UUId_t Parent;
	MCoreAshType_t Type;

	// The name of the Ash, this is usually
	// derived from the file that spawned it
	MString_t *Name;
	MString_t *Path;

	// The communication line for this Ash
	// all types of ash need some form of com
	List_t *Pipes;

	// Memory management and information,
	// Ashes run in their own space, and have their
	// own bitmap allocators
	AddressSpace_t *AddressSpace;
	BlockBitmap_t *Heap;
	BlockBitmap_t *Shm;

	// Signal support for Ashes
	MCoreSignalTable_t Signals;
	MCoreSignal_t *ActiveSignal;
	List_t *SignalQueue;

	// Below is everything related to
	// the startup and the executable information
	// that the Ash has
	MCorePeFile_t *Executable;
	uintptr_t NextLoadingAddress;
	uint8_t *FileBuffer;
	size_t FileBufferLength;
	uintptr_t StackStart;

	// This is the return/code
	// that gets set upon ash-exit
	int Code;

} MCoreAsh_t;

/* This function loads the executable and
 * prepares the ash-environment, at this point
 * it won't be completely running yet, it needs
 * its own thread for that. Returns 0 on success */
KERNELAPI int PhoenixInitializeAsh(MCoreAsh_t *Ash, MString_t *Path);

/* This is a wrapper for starting up a base Ash
 * and uses <PhoenixInitializeAsh> to setup the env
 * and do validation before starting */
KERNELAPI UUId_t PhoenixStartupAsh(MString_t *Path);

/* This is the finalizor function for starting
 * up a new base Ash, it finishes setting up the environment
 * and memory mappings, must be called on it's own thread */
KERNELAPI void PhoenixFinishAsh(MCoreAsh_t *Ash);

/* These function manipulate pipes on the given port
 * there are some pre-defined ports on which pipes
 * can be opened, window manager etc */
KERNELAPI OsStatus_t PhoenixOpenAshPipe(MCoreAsh_t *Ash, int Port, Flags_t Flags);
KERNELAPI OsStatus_t PhoenixWaitAshPipe(MCoreAsh_t *Ash, int Port);
KERNELAPI OsStatus_t PhoenixCloseAshPipe(MCoreAsh_t *Ash, int Port);
KERNELAPI MCorePipe_t *PhoenixGetAshPipe(MCoreAsh_t *Ash, int Port);

/* Ash Function Prototypes
 * these are the interesting ones */
KERNELAPI int PhoenixQueryAsh(MCoreAsh_t *Ash,
	AshQueryFunction_t Function, void *Buffer, size_t Length);
KERNELAPI void PhoenixCleanupAsh(MCoreAsh_t *Ash);
KERNELAPI void PhoenixTerminateAsh(MCoreAsh_t *Ash);


/* PhoenixRegisterAlias
 * Allows a server to register an alias for its id
 * which means that id (must be above PHOENIX_ALIAS_BASE)
 * will always refer the calling process */
KERNELAPI
OsStatus_t
KERNELABI
PhoenixRegisterAlias(
	_In_ MCoreAsh_t *Ash, 
	_In_ UUId_t Alias);

/* PhoenixGetAsh
 * This function looks up a ash structure by the given id */
KERNELAPI
MCoreAsh_t*
KERNELABI
PhoenixGetAsh(
	_In_ UUId_t AshId);

/* PhoenixGetCurrentAsh
 * Retrives the current ash for the running thread */
KERNELAPI 
MCoreAsh_t*
KERNELABI
PhoenixGetCurrentAsh(void);

/* PhoenixGetAshByName
 * This function looks up a ash structure by the given name */
KERNELAPI
MCoreAsh_t*
KERNELABI
PhoenixGetAshByName(
	_In_ __CONST char *Name);

#endif //!_MCORE_ASH_H_
