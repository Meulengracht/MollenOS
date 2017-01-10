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
#include <arch.h>
#include <mollenos.h>
#include <mutex.h>

/* Includes 
 * - C-Library */
#include <os/osdefs.h>
#include <ds/list.h>

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
	ThreadId_t Id;
	ThreadId_t ParentId;
	PhxId_t AshId;
	Cpu_t CpuId;
	
	/* Information related to this 
	 * thread for it's usage */
	AddressSpace_t *AddressSpace;
	const char *Name;
	Flags_t Flags;
	int RetCode;

	/* Scheduler Information 
	 * Used by the scheduler to keep
	 * track of time, priority and it's queue */
	MCoreThreadPriority_t Priority;
	size_t TimeSlice;
	int Queue;
	
	/* Synchronization 
	 * Used by semaphores and sleep */
	Addr_t *SleepResource;
	size_t Sleep;

	/* Architecture specific extra data
	 * this involves it's contexts and other task
	 * only data */
	void *ThreadData;

	/* Entry point */
	ThreadEntry_t Function;
	void *Args;

} MCoreThread_t;

/* ThreadingInitialize
 * Initializes threading on the given cpu-core
 * and initializes the current 'context' as the
 * idle-thread, first time it's called it also
 * does initialization of threading system */
__CRT_EXTERN void ThreadingInitialize(Cpu_t Cpu);

/* Create a new thread with the given name,
 * entry point, arguments and flags, if name 
 * is NULL, a generic name will be generated 
 * Thread is started as soon as possible */
__CRT_EXTERN ThreadId_t ThreadingCreateThread(const char *Name,
	ThreadEntry_t Function, void *Arguments, Flags_t Flags);

/* ThreadingExitThread
 * Exits the current thread by marking it finished
 * and yielding control to scheduler */
__CRT_EXTERN void ThreadingExitThread(int ExitCode);

/* ThreadingKillThread
 * Kills a thread with the given id, the thread
 * might not be killed immediately */
__CRT_EXTERN void ThreadingKillThread(ThreadId_t ThreadId);

/* ThreadingJoinThread
 * Can be used to wait for a thread the return 
 * value of this function is the ret-code of the thread */
__CRT_EXTERN int ThreadingJoinThread(ThreadId_t ThreadId);

/* ThreadingEnterUserMode
 * Initializes non-kernel mode and marks the thread
 * for transitioning, there is no return from this function */
__CRT_EXTERN void ThreadingEnterUserMode(void *AshInfo);

/* ThreadingTerminateAshThreads
 * Marks all threads belonging to the given ashid
 * as finished and they will be cleaned up on next switch */
__CRT_EXTERN void ThreadingTerminateAshThreads(PhxId_t AshId);

/* ThreadingIsEnabled
 * Returns 1 if the threading system has been
 * initialized, otherwise it returns 0 */
__CRT_EXTERN int ThreadingIsEnabled(void);

/* ThreadingIsCurrentTaskIdle
 * Is the given cpu running it's idle task? */
__CRT_EXTERN int ThreadingIsCurrentTaskIdle(Cpu_t Cpu);

/* ThreadingGetCurrentMode
 * Returns the current run-mode for the current
 * thread on the current cpu */
__CRT_EXTERN Flags_t ThreadingGetCurrentMode(void);

/* ThreadingGetCurrentThread
 * Retrieves the current thread on the given cpu
 * if there is any issues it returns NULL */
__CRT_EXTERN MCoreThread_t *ThreadingGetCurrentThread(Cpu_t Cpu);

/* ThreadingGetCurrentThreadId
 * Retrives the current thread id on the current cpu
 * from the callers perspective */
__CRT_EXTERN ThreadId_t ThreadingGetCurrentThreadId(void);

/* ThreadingGetThread
 * Lookup thread by the given thread-id, 
 * returns NULL if invalid */
__CRT_EXTERN MCoreThread_t *ThreadingGetThread(ThreadId_t ThreadId);

/* ThreadingWakeCpu
 * Wake's the target cpu from an idle thread
 * by sending it an yield IPI */
__CRT_EXTERN void ThreadingWakeCpu(Cpu_t Cpu);

/* ThreadingReapZombies
 * Garbage-Collector function, it reaps and
 * cleans up all threads */
__CRT_EXTERN void ThreadingReapZombies(void);

/* ThreadingSwitch
 * This is the thread-switch function and must be 
 * be called from the below architecture to get the
 * next thread to run */
__CRT_EXTERN MCoreThread_t *ThreadingSwitch(Cpu_t Cpu, 
	MCoreThread_t *Current, int PreEmptive);

/* ThreadingDebugPrint
 * Prints out debugging information about each thread
 * in the system, only active threads */
__CRT_EXTERN void ThreadingDebugPrint(void);

#endif