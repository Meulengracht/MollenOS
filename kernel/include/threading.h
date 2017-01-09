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
 * 3 => Reserved */
#define THREADING_KERNELMODE		0x0
#define THREADING_USERMODE			0x1
#define THREADING_DRIVERMODE		0x2
#define THREADING_MODEMASK			0x3

/* The rest of the bits denode special
 * other run-modes */
#define THREADING_CPUBOUND			0x4
#define THREADING_SYSTEMTHREAD		0x8
#define THREADING_IDLE				0x10
#define THREADING_ENTER_SLEEP		0x20
#define THREADING_FINISHED			0x40
#define THREADING_INHERIT			0x80
#define THREADING_TRANSITION		0x100
#define THREADING_TRANSITIONFINISH	0x200

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

/* Exits the current thread by marking it finished
 * and yielding control to scheduler */
_CRT_EXPORT void ThreadingExitThread(int ExitCode);

/* Kills a thread with the given id 
 * this force-kills the thread, thread
 * might not be killed immediately */
_CRT_EXPORT void ThreadingKillThread(ThreadId_t ThreadId);

/* Can be used to wait for a thread 
 * the return value of this function
 * is the ret-code of the thread */
_CRT_EXPORT int ThreadingJoinThread(ThreadId_t ThreadId);

/* These below functions are helper functions 
 * for ashes, and include transition to user-mode
 * and cleanup */
__CRT_EXTERN void ThreadingEnterUserMode(void *AshInfo);
__CRT_EXTERN void ThreadingTerminateAshThreads(PhxId_t AshId);
__CRT_EXTERN void ThreadingReapZombies(void);

/* Sleep, Wake, etc */
__CRT_EXTERN void ThreadingWakeCpu(Cpu_t Cpu);

/* This is the thread-switch function and must be 
 * be called from the below architecture to get the
 * next thread to run */
__CRT_EXTERN MCoreThread_t *ThreadingSwitch(Cpu_t Cpu, MCoreThread_t *Current, int PreEmptive);

/* Getter functions */
__CRT_EXTERN ThreadId_t ThreadingGetCurrentThreadId(void);
__CRT_EXTERN Flags_t ThreadingGetCurrentMode(void);
__CRT_EXTERN MCoreThread_t *ThreadingGetThread(ThreadId_t ThreadId);
__CRT_EXTERN MCoreThread_t *ThreadingGetCurrentThread(Cpu_t Cpu);
__CRT_EXTERN ListNode_t *ThreadingGetCurrentNode(Cpu_t Cpu);

/* Utility functions, primarily used to check if stuff is 
 * ok, enabled etc etc. Random stuff as well! */
__CRT_EXTERN int ThreadingIsCurrentTaskIdle(Cpu_t Cpu);
__CRT_EXTERN int ThreadingIsEnabled(void);
_CRT_EXPORT void ThreadingDebugPrint(void);

#endif