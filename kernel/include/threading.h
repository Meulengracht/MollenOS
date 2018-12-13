/* MollenOS
 *
 * Copyright 2015, Philip Meulengracht
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
 * 
 * - Threads are only ever cleaned up if they are in the Cleanup State, to reach this state
 *   someone must have either called Join or Detach (+ reach exit) on the thread
 */

#ifndef _MCORE_THREADING_H_
#define _MCORE_THREADING_H_

#include <os/osdefs.h>
#include <os/context.h>
#include <ds/collection.h>
#include <signal.h>
#include <time.h>

// Forward some structures we need
typedef struct _SystemMemorySpace SystemMemorySpace_t;
typedef struct _SystemProcess SystemProcess_t;
typedef struct _SystemPipe SystemPipe_t;

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
#define THREADING_CONFIGDATA_COUNT      4

/* MCoreThread::Flags Bit Definitions 
 * The first two bits denode the thread
 * runtime mode, which is either:
 * 0 => Kernel
 * 1 => User
 * 2 => Reserved 
 * 3 => Reserved 
 * Bit 3: If it's currently in switch-mode */
#define THREADING_KERNELMODE            0x00000000
#define THREADING_USERMODE              0x00000001
#define THREADING_SWITCHMODE            0x00000004
#define THREADING_MODEMASK              0x00000003
#define THREADING_RUNMODE(Flags)        (Flags & THREADING_MODEMASK)

/* MCoreThread::Flags Bit Definitions 
 * The rest of the bits denode special other run-modes */
#define THREADING_SYSTEMTHREAD          0x00000010
#define THREADING_IDLE                  0x00000020
#define THREADING_INHERIT               0x00000040
#define THREADING_IMPERSONATION         0x00000080
#define THREADING_TRANSITION_USERMODE   0x10000000

typedef struct _SystemSignal {
    int         Ignorable;
    int         Signal;
    Context_t*  Context;
} SystemSignal_t;

/* MCoreThread
 * The representation of a thread in the system. Contains scheduling information,
 * thread information and data for contexts and signals. */
typedef struct _MCoreThread {
    CollectionItem_t        CollectionHeader;

    UUId_t                  ParentThreadId;
    UUId_t                  Id;
    const char*             Name;
    Flags_t                 Flags;
    atomic_int              Cleanup;

    Context_t*              Contexts[THREADING_NUMCONTEXTS];
    Context_t*              ContextActive;
    uintptr_t               Data[THREADING_CONFIGDATA_COUNT];
    clock_t                 StartedAt;

    SystemPipe_t*           Pipe;
    SystemMemorySpace_t*    MemorySpace;
    UUId_t                  MemorySpaceHandle;

    ThreadEntry_t           Function;
    void*                   Arguments;
    int                     RetCode;

    // Signal Support
    int                     SignalInformation[NUMSIGNALS];
    SystemSignal_t          ActiveSignal;
    Collection_t*           SignalQueue;

    // Scheduler Information
    Flags_t                 SchedulerFlags;
    UUId_t                  CoreId;
    size_t                  TimeSlice;
    int                     Queue;
    uintptr_t               LastInstructionPointer;
    struct {
        uintptr_t*          Handle;
        int                 Timeout;
        size_t              TimeLeft;
        clock_t             InterruptedAt;
    }                       Sleep;
    struct _MCoreThread*    Link;
} MCoreThread_t;

/* ThreadingInitialize
 * Initializes static data and allocates resources. */
KERNELAPI OsStatus_t KERNELABI
ThreadingInitialize(void);

/* ThreadingEnable
 * Enables the threading system for the given cpu calling the function. */
KERNELAPI OsStatus_t KERNELABI
ThreadingEnable(void);

/* CreateThread
 * Creates a new thread that will execute the given function as soon as possible. The 
 * thread can be supplied with arguments, mode and a custom memory space. */
KERNELAPI OsStatus_t KERNELABI
CreateThread(
    _In_  const char*    Name,
    _In_  ThreadEntry_t  Function,
    _In_  void*          Arguments,
    _In_  Flags_t        Flags,
    _In_  UUId_t         MemorySpaceHandle,
    _Out_ UUId_t*        Handle);

/* ThreadingTerminateThread
 * Marks the thread with the given id for finished, and it will be cleaned up
 * on next switch unless specified. The given exitcode will be stored. */
KERNELAPI OsStatus_t KERNELABI
ThreadingTerminateThread(
    _In_ UUId_t         ThreadId,
    _In_ int            ExitCode,
    _In_ int            TerminateChildren);

/* ThreadingJoinThread
 * Can be used to wait for a thread the return 
 * value of this function is the ret-code of the thread */
KERNELAPI int KERNELABI
ThreadingJoinThread(
    _In_ UUId_t         ThreadId);

/* ThreadingDetachThread
 * Detaches a running thread by marking it without parent, this will make
 * sure it runs untill it kills itself. */
KERNELAPI OsStatus_t KERNELABI
ThreadingDetachThread(
    _In_  UUId_t        ThreadId);

/* ThreadingSwitchLevel
 * Initializes non-kernel mode and marks the thread
 * for transitioning, there is no return from this function */
KERNELAPI void KERNELABI
ThreadingSwitchLevel(void);

/* ThreadingIsCurrentTaskIdle
 * Is the given cpu running it's idle task? */
KERNELAPI int KERNELABI
ThreadingIsCurrentTaskIdle(
    _In_ UUId_t         CoreId);

/* ThreadingGetCurrentMode
 * Returns the current run-mode for the current thread on the current cpu */
KERNELAPI Flags_t KERNELABI
ThreadingGetCurrentMode(void);

/* ThreadingGetCurrentThread
 * Retrieves the current thread on the given cpu if there is any issues it returns NULL */
KERNELAPI MCoreThread_t* KERNELABI
ThreadingGetCurrentThread(
    _In_ UUId_t         CoreId);

/* ThreadingGetCurrentThreadId
 * Retrives the current thread id on the current cpu from the callers perspective */
KERNELAPI UUId_t KERNELABI
ThreadingGetCurrentThreadId(void);

/* ThreadingGetThread
 * Lookup thread by the given thread-id, returns NULL if invalid */
KERNELAPI MCoreThread_t* KERNELABI
ThreadingGetThread(
    _In_ UUId_t         ThreadId);

/* ThreadingSwitch
 * This is the thread-switch function and must be be called from the below architecture 
 * to get the next thread to run */
KERNELAPI MCoreThread_t* KERNELABI
ThreadingSwitch(
    _In_ MCoreThread_t* Current, 
    _In_ int            PreEmptive,
    _InOut_ Context_t** Context);

/* ThreadingDebugPrint
 * Prints out debugging information about each thread in the system, only active threads */
KERNELAPI void KERNELABI
ThreadingDebugPrint(void);

/* SignalReturn
 * Call upon returning from a signal, this will finish the signal-call and 
 * enter a new signal if any is queued up */
KERNELAPI OsStatus_t KERNELABI
SignalReturn(void);

/* SignalProcess
 * This checks if the process has any waiting signals and if it has, 
 * it executes the first in list */
KERNELAPI OsStatus_t KERNELABI
SignalProcess(
    _In_ UUId_t             ThreadId);

/* Create Signal 
 * Dispatches a signal to a thread in the system. If the thread is sleeping
 * and the signal is not masked, then it will be woken up. */
KERNELAPI OsStatus_t KERNELABI
SignalCreate(
    _In_ UUId_t             ThreadId,
    _In_ int                Signal);

/* SignalExecute
 * This function does preliminary checks before actually dispatching the signal 
 * to the process */
KERNELAPI void KERNELABI
SignalExecute(
    _In_ MCoreThread_t*     Thread,
    _In_ SystemSignal_t*    Signal);

#endif
