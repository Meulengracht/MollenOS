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
#include <crtdefs.h>
#include <stdint.h>
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
typedef unsigned int PId_t;

#define PROCESS_STACK_INIT		0x1000
#define PROCESS_STACK_MAX		(4 << 20)
#define PROCESS_PIPE_SIZE		0x2000

/* Structures */

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
	/* Id */
	PId_t Id;
	PId_t Parent;

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
	List_t *SignalQueue;

	/* Return Code */
	int ReturnCode;

} MCoreProcess_t;

/* Process Function Prototypes
 * these are the interesting ones */
_CRT_EXTERN void PmCleanupProcess(MCoreProcess_t *Process);
_CRT_EXTERN void PmTerminateProcess(MCoreProcess_t *Process);
_CRT_EXTERN MCoreProcess_t *PmGetProcess(PId_t ProcessId);
_CRT_EXTERN MString_t *PmGetWorkingDirectory(PId_t ProcessId);
_CRT_EXTERN MString_t *PmGetBaseDirectory(PId_t ProcessId);

/* Signal Functions */
_CRT_EXTERN int SignalCreate(PId_t ProcessId, int Signal);
_CRT_EXTERN void SignalExecute(MCoreProcess_t *Process, MCoreSignal_t *Signal);

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
	PId_t ProcessId;

} MCoreProcessRequest_t;

/* Prototypes */
_CRT_EXTERN void PmInit(void);
_CRT_EXTERN void PmReapZombies(void);

/* Requests */
_CRT_EXTERN void PmCreateRequest(MCoreProcessRequest_t *Request);
_CRT_EXTERN void PmWaitRequest(MCoreProcessRequest_t *Request, size_t Timeout);

#endif //!_MCORE_PROCESS_MANAGER_H_