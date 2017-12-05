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
#include <mutex.h>
#include <pipe.h>

/* Includes 
 * - Library */
#include <os/osdefs.h>
#include <os/context.h>
#include <ds/collection.h>
#include <signal.h>
#include <time.h>

/* Define the thread entry point signature */
#ifndef __THREADING_ENTRY
#define __THREADING_ENTRY
typedef void(*ThreadEntry_t)(void*);
#endif

#define THREADING_CONTEXT_LEVEL0        0   // Kernel
#define THREADING_CONTEXT_LEVEL1        1   // Application
#define THREADING_CONTEXT_SIGNAL0       2   // Signal (Cpu)
#define THREADING_CONTEXT_SIGNAL1       3   // Signal (Application)
#define THREADING_NUMCONTEXTS           4

/* MCoreThread::Flags Bit Definitions 
 * The first two bits denode the thread
 * runtime mode, which is either:
 * 0 => Kernel
 * 1 => User
 * 2 => Driver
 * 3 => Reserved 
 * Bit 3: If it's currently in switch-mode */
#define THREADING_KERNELMODE            0x00000000
#define THREADING_USERMODE              0x00000001
#define THREADING_DRIVERMODE            0x00000002
#define THREADING_SWITCHMODE            0x00000004
#define THREADING_MODEMASK              0x00000003
#define THREADING_RUNMODE(Flags)        (Flags & THREADING_MODEMASK)

/* MCoreThread::Flags Bit Definitions 
 * The next two bits determine the current state of the thread
 * 0 = Inactive
 * 1 = Active
 * 2 = Reserved
 * 3 = Reserved */
#define THREADING_INACTIVE              0x00000000
#define THREADING_ACTIVE                0x00000001
#define THREADING_STATEMASK             0x00000018
#define THREADING_STATE(Flags)          ((Flags & THREADING_STATEMASK) >> 3)
#define THREADING_SETSTATE(Flags, State) (Flags |= (State << 3))
#define THREADING_CLEARSTATE(Flags)     (Flags &= ~(THREADING_STATEMASK))

/* MCoreThread::Flags Bit Definitions 
 * The rest of the bits denode special other run-modes */
#define THREADING_CPUBOUND              0x00000020
#define THREADING_SYSTEMTHREAD          0x00000040
#define THREADING_IDLE                  0x00000080
#define THREADING_INHERIT               0x00000100
#define THREADING_FINISHED              0x00000200
#define THREADING_IMPERSONATION         0x00000400

#define THREADING_TRANSITION_USERMODE   0x10000000
#define THREADING_TRANSITION_SLEEP      0x20000000

/* A Signal Entry 
 * This is used to describe a signal 
 * that is waiting for execution */
typedef struct _MCoreSignal {
    int                 Ignorable;
    int                 Signal;
    Context_t          *Context;
} MCoreSignal_t;

/* The different possible threading priorities 
 * Normal is the default thread-priority, and Critical
 * should only be used by the system */
typedef enum _MCoreThreadPriority {
    PriorityLow,
    PriorityNormal,
    PriorityCritical
} MCoreThreadPriority_t;

/* The shared Thread structure used in MCore
 * it contains a <ThreadData> which points to 
 * architecture specific thread data, but rest
 * of the information here is shared in MCore */
typedef struct _MCoreThread {
    UUId_t                           Id;
    __CONST char                    *Name;
    UUId_t                           ParentId;
    UUId_t                           AshId;
    Flags_t                          Flags;
    Context_t                       *Contexts[THREADING_NUMCONTEXTS];
    Context_t                       *ContextActive;

    MCorePipe_t                     *Pipe;
    AddressSpace_t                  *AddressSpace;

    ThreadEntry_t                    Function;
    void                            *Arguments;
    int                              RetCode;

    // Signal Support
    int                              SignalInformation[NUMSIGNALS];
    MCoreSignal_t                    ActiveSignal;
    Collection_t                    *SignalQueue;

    // Scheduler Information
    UUId_t                           CpuId;
    MCoreThreadPriority_t            Priority;
    size_t                           TimeSlice;
    int                              Queue;
    struct {
        uintptr_t                   *Handle;
        int                          Timeout;
        size_t                       TimeLeft;
        clock_t                      InterruptedAt;
    }                                Sleep;
    struct _MCoreThread             *Link;
} MCoreThread_t;

/* ThreadingInitialize
 * Initializes static data and allocates resources. */
KERNELAPI
OsStatus_t
KERNELABI
ThreadingInitialize(void);

/* ThreadingEnable
 * Enables the threading system for the given cpu calling the function. */
KERNELAPI
OsStatus_t
KERNELABI
ThreadingEnable(
    _In_ UUId_t Cpu);

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

/* ThreadingSwitchLevel
 * Initializes non-kernel mode and marks the thread
 * for transitioning, there is no return from this function */
KERNELAPI
void
KERNELABI
ThreadingSwitchLevel(
    _In_ void *AshInfo);

/* ThreadingTerminateAshThreads
 * Marks all threads belonging to the given ashid
 * as finished and they will be cleaned up on next switch */
KERNELAPI
void
KERNELABI
ThreadingTerminateAshThreads(
    _In_ UUId_t AshId);

/* ThreadingIsEnabled
 * Returns 1 if the threading system has been
 * initialized, otherwise it returns 0 */
KERNELAPI
int 
KERNELABI
ThreadingIsEnabled(void);

/* ThreadingIsCurrentTaskIdle
 * Is the given cpu running it's idle task? */
KERNELAPI
int
KERNELABI
ThreadingIsCurrentTaskIdle(
    _In_ UUId_t Cpu);

/* ThreadingGetCurrentMode
 * Returns the current run-mode for the current
 * thread on the current cpu */
KERNELAPI
Flags_t
KERNELABI
ThreadingGetCurrentMode(void);

/* ThreadingGetCurrentThread
 * Retrieves the current thread on the given cpu
 * if there is any issues it returns NULL */
KERNELAPI
MCoreThread_t*
KERNELABI
ThreadingGetCurrentThread(
    _In_ UUId_t Cpu);

/* ThreadingGetCurrentThreadId
 * Retrives the current thread id on the current cpu
 * from the callers perspective */
KERNELAPI
UUId_t
KERNELABI
ThreadingGetCurrentThreadId(void);

/* ThreadingGetThread
 * Lookup thread by the given thread-id, returns NULL if invalid */
KERNELAPI
MCoreThread_t*
KERNELABI
ThreadingGetThread(
    _In_ UUId_t ThreadId);

/* ThreadingWakeCpu
 * Wake's the target cpu from an idle thread
 * by sending it an yield IPI */
__EXTERN void ThreadingWakeCpu(UUId_t Cpu);

/* ThreadingSwitch
 * This is the thread-switch function and must be 
 * be called from the below architecture to get the
 * next thread to run */
KERNELAPI
MCoreThread_t*
KERNELABI
ThreadingSwitch(
    _In_ UUId_t         Cpu, 
    _In_ MCoreThread_t *Current, 
    _In_ int            PreEmptive,
    _InOut_ Context_t **Context);

/* ThreadingDebugPrint
 * Prints out debugging information about each thread
 * in the system, only active threads */
__EXTERN void ThreadingDebugPrint(void);

/* SignalReturn
 * Call upon returning from a signal, this will finish
 * the signal-call and enter a new signal if any is queued up */
KERNELAPI
OsStatus_t
KERNELABI
SignalReturn(void);

/* Handle Signal 
 * This checks if the process has any waiting signals
 * and if it has, it executes the first in list */
KERNELAPI
OsStatus_t
KERNELABI
SignalHandle(
	_In_ UUId_t ThreadId);

/* Create Signal 
 * Dispatches a signal to a thread in the system. If the thread is sleeping
 * and the signal is not masked, then it will be woken up. */
KERNELAPI
OsStatus_t
KERNELABI
SignalCreate(
    _In_ UUId_t     ThreadId,
    _In_ int        Signal);

/* SignalExecute
 * This function does preliminary checks before actually
 * dispatching the signal to the process */
KERNELAPI
void
KERNELABI
SignalExecute(
    _In_ MCoreThread_t *Thread,
    _In_ MCoreSignal_t *Signal);

#endif
