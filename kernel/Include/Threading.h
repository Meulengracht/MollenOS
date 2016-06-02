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
* MollenOS MCore Threading
*
*/

#ifndef _MCORE_THREADING_H_
#define _MCORE_THREADING_H_

/* Includes */
#include <MollenOS.h>
#include <Arch.h>
#include <MString.h>
#include <List.h>
#include <stdint.h>

/* Definitions */
typedef void(*ThreadEntry_t)(void*);
typedef unsigned int TId_t;

/* Threading Flags */
#define THREADING_USERMODE		0x1
#define THREADING_CPUBOUND		0x2
#define THREADING_SYSTEMTHREAD	0x4
#define THREADING_IDLE			0x8
#define THREADING_ENTER_SLEEP	0x10
#define THREADING_FINISHED		0x20
#define THREADING_TRANSITION	0x40
#define THREADING_INHERIT		0x80

/* Structures */
typedef struct _MCoreThread
{
	/* Name */
	char *Name;

	/* Thread Attributes */
	int Flags;
	int RetCode;

	/* Scheduler Information */
	size_t TimeSlice;
	int Queue;
	
	/* Synchronization */
	Addr_t *SleepResource;
	size_t Sleep;

	/* Ids */
	TId_t ThreadId;
	TId_t ParentId;
	uint32_t ProcessId;
	Cpu_t CpuId;

	/* Address Space */
	AddressSpace_t *AddrSpace;

	/* Architecture Data */
	void *ThreadData;

	/* Entry point */
	ThreadEntry_t Func;
	void *Args;

} MCoreThread_t;

/* Prototypes */

/* Initializors used by kernel threading
 * and support, don't call otherwise */
_CRT_EXTERN void ThreadingInit(void);
_CRT_EXTERN void ThreadingApInit(Cpu_t Cpu);

/* Create a new thread with the given name,
 * entry point, arguments and flags, if name 
 * is null, a generic name will be generated 
 * Thread is started as soon as possible */
_CRT_EXPORT TId_t ThreadingCreateThread(char *Name, ThreadEntry_t Function, void *Args, int Flags);

/* Exits the current thread by marking it finished
 * and yielding control to scheduler */
_CRT_EXPORT void ThreadingExitThread(int ExitCode);

/* Kills a thread with the given id 
 * this force-kills the thread, thread
 * might not be killed immediately */
_CRT_EXPORT void ThreadingKillThread(TId_t ThreadId);

/* Can be used to wait for a thread 
 * the return value of this function
 * is the ret-code of the thread */
_CRT_EXPORT int ThreadingJoinThread(TId_t ThreadId);

/* Processes */
_CRT_EXTERN void ThreadingEnterUserMode(void *ProcessInfo);
_CRT_EXTERN void ThreadingTerminateProcessThreads(uint32_t ProcessId);
_CRT_EXTERN void ThreadingReapZombies(void);

/* Sleep, Wake, etc */
_CRT_EXTERN int ThreadingYield(void *Args);
_CRT_EXTERN void ThreadingWakeCpu(Cpu_t Cpu);
_CRT_EXTERN MCoreThread_t *ThreadingSwitch(Cpu_t Cpu, MCoreThread_t *Current, uint8_t PreEmptive);

/* Gets */
_CRT_EXTERN TId_t ThreadingGetCurrentThreadId(void);

/* Lookup thread by the given 
 * thread-id, returns NULL if invalid */
_CRT_EXTERN MCoreThread_t *ThreadingGetThread(TId_t ThreadId);
_CRT_EXTERN MCoreThread_t *ThreadingGetCurrentThread(Cpu_t Cpu);
_CRT_EXTERN list_node_t *ThreadingGetCurrentNode(Cpu_t Cpu);
_CRT_EXTERN int ThreadingIsCurrentTaskIdle(Cpu_t Cpu);
_CRT_EXTERN int ThreadingIsEnabled(void);
_CRT_EXPORT void ThreadingDebugPrint(void);

#endif