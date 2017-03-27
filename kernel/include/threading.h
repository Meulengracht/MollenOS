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
 * MollenOS MCore Threading Interface
 * - Handles all common threading across architectures
 *   and implements systems like signaling, synchronization and rpc
 */

#ifndef _MCORE_THREADING_H_
#define _MCORE_THREADING_H_

/* Includes 
 * - System */
#include <system/addresspace.h>
#include <mollenos.h>
#include <mutex.h>
#include <pipe.h>

/* Includes 
 * - C-Library */
#include <os/osdefs.h>
#include <ds/list.h>

/* Define the thread entry point signature */
#ifndef __THREADING_ENTRY
#define __THREADING_ENTRY
typedef void(*ThreadEntry_t)(void*);
#endif

/* Threading flags
 * The first two bits denode the thread
 * runtime mode, which is either:
 * 0 => Kernel
 * 1 => User
 * 2 => Driver
 * 3 => Reserved 
 * Bit 3: If it's currently in switch-mode */
#define THREADING_KERNELMODE		0x0
#define THREADING_USERMODE			0x1
#define THREADING_DRIVERMODE		0x2
#define THREADING_SWITCHMODE		0x4
#define THREADING_MODEMASK			0x3
#define THREADING_RUNMODE(Flags)	(Flags & THREADING_MODEMASK)

/* The rest of the bits denode special
 * other run-modes */
#define THREADING_CPUBOUND			0x8
#define THREADING_SYSTEMTHREAD		0x10
#define THREADING_IDLE				0x20
#define THREADING_ENTER_SLEEP		0x40
#define THREADING_FINISHED			0x80
#define THREADING_INHERIT			0x100
#define THREADING_TRANSITION		0x200

/* The different possible threading priorities 
 * Normal is the default thread-priority, and Critical
 * should only be used by the system */
typedef enum _MCoreThreadPriority {
	PriorityLow,
	PriorityNormal,
	PriorityHigh,
	PriorityCritical
} MCoreThreadPriority_t;

/* The shared Thread structure used in MCore
 * it contains a <ThreadData> which points to 
 * architecture specific thread data, but rest
 * of the information here is shared in MCore */
typedef struct _MCoreThread {
	UUId_t						 Id;
	UUId_t						 ParentId;
	UUId_t						 AshId;
	UUId_t						 CpuId;
	
	MCorePipe_t					*Pipe;
	AddressSpace_t				*AddressSpace;
	__CONST char				*Name;
	Flags_t						 Flags;
	int							 RetCode;

	MCoreThreadPriority_t		 Priority;
	size_t						 TimeSlice;
	int							 Queue;

	uintptr_t						*SleepResource;
	size_t						 Sleep;

	void						*ThreadData;

	ThreadEntry_t				 Function;
	void						*Args;
} MCoreThread_t;

/* ThreadingInitialize
 * Initializes threading on the given cpu-core
 * and initializes the current 'context' as the
 * idle-thread, first time it's called it also
 * does initialization of threading system */
__EXTERN void ThreadingInitialize(UUId_t Cpu);

/* Create a new thread with the given name,
 * entry point, arguments and flags, if name 
 * is NULL, a generic name will be generated 
 * Thread is started as soon as possible */
__EXTERN UUId_t ThreadingCreateThread(const char *Name,
	ThreadEntry_t Function, void *Arguments, Flags_t Flags);

/* ThreadingExitThread
 * Exits the current thread by marking it finished
 * and yielding control to scheduler */
__EXTERN void ThreadingExitThread(int ExitCode);

/* ThreadingKillThread
 * Kills a thread with the given id, the thread
 * might not be killed immediately */
__EXTERN void ThreadingKillThread(UUId_t ThreadId);

/* ThreadingJoinThread
 * Can be used to wait for a thread the return 
 * value of this function is the ret-code of the thread */
__EXTERN int ThreadingJoinThread(UUId_t ThreadId);

/* ThreadingEnterUserMode
 * Initializes non-kernel mode and marks the thread
 * for transitioning, there is no return from this function */
__EXTERN void ThreadingEnterUserMode(void *AshInfo);

/* ThreadingTerminateAshThreads
 * Marks all threads belonging to the given ashid
 * as finished and they will be cleaned up on next switch */
__EXTERN void ThreadingTerminateAshThreads(UUId_t AshId);

/* ThreadingIsEnabled
 * Returns 1 if the threading system has been
 * initialized, otherwise it returns 0 */
__EXTERN int ThreadingIsEnabled(void);

/* ThreadingIsCurrentTaskIdle
 * Is the given cpu running it's idle task? */
__EXTERN int ThreadingIsCurrentTaskIdle(UUId_t Cpu);

/* ThreadingGetCurrentMode
 * Returns the current run-mode for the current
 * thread on the current cpu */
__EXTERN Flags_t ThreadingGetCurrentMode(void);

/* ThreadingGetCurrentThread
 * Retrieves the current thread on the given cpu
 * if there is any issues it returns NULL */
__EXTERN MCoreThread_t *ThreadingGetCurrentThread(UUId_t Cpu);

/* ThreadingGetCurrentThreadId
 * Retrives the current thread id on the current cpu
 * from the callers perspective */
__EXTERN UUId_t ThreadingGetCurrentThreadId(void);

/* ThreadingGetThread
 * Lookup thread by the given thread-id, 
 * returns NULL if invalid */
__EXTERN MCoreThread_t *ThreadingGetThread(UUId_t ThreadId);

/* ThreadingWakeCpu
 * Wake's the target cpu from an idle thread
 * by sending it an yield IPI */
__EXTERN void ThreadingWakeCpu(UUId_t Cpu);

/* ThreadingSwitch
 * This is the thread-switch function and must be 
 * be called from the below architecture to get the
 * next thread to run */
__EXTERN MCoreThread_t *ThreadingSwitch(UUId_t Cpu, 
	MCoreThread_t *Current, int PreEmptive);

/* ThreadingDebugPrint
 * Prints out debugging information about each thread
 * in the system, only active threads */
__EXTERN void ThreadingDebugPrint(void);

#endif