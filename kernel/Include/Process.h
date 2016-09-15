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
* MollenOS MCore - Process Manager
*/

#ifndef _MCORE_PROCESS_MANAGER_H_
#define _MCORE_PROCESS_MANAGER_H_

/* Includes */

/* C-Library Includes */
#include <os/osdefs.h>
#include <signal.h>
#include <ds/list.h>
#include <ds/mstring.h>

#include <Arch.h>
#include <Modules/PeLoader.h>
#include <Bitmap.h>
#include <Pipe.h>
#include <Events.h>
#include <Heap.h>

/* Definitions */
#define PROCESS_STACK_INIT		0x1000
#define PROCESS_STACK_MAX		(4 << 20)
#define PROCESS_PIPE_SIZE		0x2000
#define PROCESS_NO_PROCESS		0xFFFFFFFF

/* Signal Table 
 * This is used for interrupt-signals
 * across threads and processes */
typedef struct _MCoreSignalTable {

	/* An array of handlers */
	Addr_t Handlers[NUMSIGNALS + 1];

} MCoreSignalTable_t;

/* A Signal Entry 
 * This is used to describe a signal 
 * that is waiting for execution */
typedef struct _MCoreSignal {
	
	/* The signal */
	int Signal;

	/* The handler */
	Addr_t Handler;

	/* The execution context */
	Context_t Context;

} MCoreSignal_t;

/* The process structure, contains information
 * and stats about a running process, also contains
 * it's address space, shm, etc etc */
typedef struct _MCoreProcess
{
	/* Ids */
	ThreadId_t MainThread;
	ProcId_t Id;
	ProcId_t Parent;

	/* Name */
	MString_t *Name;

	/* Working Directory */
	MString_t *WorkingDirectory;
	MString_t *BaseDirectory;

	/* Open Files */
	List_t *OpenFiles;

	/* Pipe */
	MCorePipe_t *Pipe;

	/* Address Space */
	AddressSpace_t *AddressSpace;

	/* Heap(s) */
	Heap_t *Heap;
	Bitmap_t *Shm;

	/* Startup Args */
	uint8_t *fBuffer;
	MString_t *Arguments;

	/* Executable */
	MCorePeFile_t *Executable;
	Addr_t NextBaseAddress;

	/* Stack Start in Kernel */
	Addr_t StackStart;

	/* Signal Support */
	MCoreSignalTable_t Signals;
	MCoreSignal_t *ActiveSignal;
	List_t *SignalQueue;

	/* Return Code */
	int ReturnCode;

} MCoreProcess_t;

/* Process Queries
 * List of the different options
 * for process queries */
typedef enum _ProcessQueryFunction
{
	ProcessQueryName,
	ProcessQueryMemory,
	ProcessQueryParent,
	ProcessQueryTopMostParent

} ProcessQueryFunction_t;

/* Process Function Prototypes
 * these are the interesting ones */
_CRT_EXTERN int PmQueryProcess(MCoreProcess_t *Process, ProcessQueryFunction_t Function, void *Buffer, size_t Length);
_CRT_EXTERN void PmCleanupProcess(MCoreProcess_t *Process);
_CRT_EXTERN void PmTerminateProcess(MCoreProcess_t *Process);
_CRT_EXTERN MCoreProcess_t *PmGetProcess(ProcId_t ProcessId);
_CRT_EXTERN MString_t *PmGetWorkingDirectory(ProcId_t ProcessId);
_CRT_EXTERN MString_t *PmGetBaseDirectory(ProcId_t ProcessId);

/* Signal Functions */
_CRT_EXTERN void SignalHandle(ThreadId_t ThreadId);
_CRT_EXTERN int SignalCreate(ProcId_t ProcessId, int Signal);
_CRT_EXTERN void SignalExecute(MCoreProcess_t *Process, MCoreSignal_t *Signal);

/* Architecture Specific  
 * Must be implemented in the arch-layer */
_CRT_EXTERN void SignalDispatch(MCoreProcess_t *Process, MCoreSignal_t *Signal);

/*************************************
 ******** PROCESS - MANAGER **********
 *************************************/

/* Process Request Type */
typedef enum _ProcessRequestType
{
	ProcessSpawn,
	ProcessKill,
	ProcessQuery

} ProcessRequestType_t;

/* Process Request */
typedef struct _MCoreProcessRequest
{
	/* Event Base */
	MCoreEvent_t Base;

	/* Creation Data */
	MString_t *Path;
	MString_t *Arguments;

	/* Process Id */
	ProcId_t ProcessId;

} MCoreProcessRequest_t;

/* Prototypes */
_CRT_EXTERN void PmInit(void);
_CRT_EXTERN void PmReapZombies(void);

/* Requests */
_CRT_EXTERN void PmCreateRequest(MCoreProcessRequest_t *Request);
_CRT_EXTERN void PmWaitRequest(MCoreProcessRequest_t *Request, size_t Timeout);

#endif //!_MCORE_PROCESS_MANAGER_H_