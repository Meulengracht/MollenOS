/**
 * MollenOS
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
 * Threading Interface
 * - Handles all common threading across architectures
 *   and implements systems like signaling, synchronization and rpc
 */

#ifndef __THREADING_H__
#define __THREADING_H__

#include <os/osdefs.h>
#include <os/context.h>
#include <ds/list.h>
#include <semaphore.h>
#include <mutex.h>
#include <signal.h>
#include <time.h>

// Forward some structures we need
DECL_STRUCT(MemorySpace);
DECL_STRUCT(SystemProcess);
DECL_STRUCT(SchedulerObject);
DECL_STRUCT(Thread);

#ifndef __THREADING_ENTRY
#define __THREADING_ENTRY
typedef void(*ThreadEntry_t)(void*);
#endif

#define THREADING_CONTEXT_LEVEL0 0   // Kernel
#define THREADING_CONTEXT_LEVEL1 1   // Application
#define THREADING_CONTEXT_SIGNAL 2   // Signal
#define THREADING_NUMCONTEXTS    3

#define THREADING_CONFIGDATA_COUNT   4
#define THREADING_MAX_QUEUED_SIGNALS 32
#define THREADING_KERNEL_STACK_SIZE  0x1000

/* MCoreThread::Flags Bit Definitions 
 * The first two bits denode the thread
 * runtime mode, which is either:
 * 0 => Kernel
 * 1 => User
 * 2 => Reserved
 * 3 => Reserved 
 * Bit 3: If it's currently in switch-mode */
#define THREADING_KERNELMODE            0x00000000U
#define THREADING_USERMODE              0x00000001U
#define THREADING_MODEMASK              0x00000001U
#define THREADING_RUNMODE(Flags)        (Flags & THREADING_MODEMASK)

/* MCoreThread::Flags Bit Definitions 
 * The rest of the bits denode special other run-modes */
#define THREADING_KERNELENTRY           0x00000004U  // Mark this thread as requiring transition mode
#define THREADING_IDLE                  0x00000008U  // Mark this thread as an idle thread
#define THREADING_INHERIT               0x00000010U  // Inherit from creator
#define THREADING_TRANSITION_USERMODE   0x10000000U

#define THREAD_GET(Handle) (Thread_t*)LookupHandleOfType(Handle, HandleTypeThread)

/**
 * ThreadCreate
 * Creates a new thread that will execute the given function as soon as possible. The 
 * thread can be supplied with arguments, mode and a custom memory space.
 */
KERNELAPI OsStatus_t KERNELABI
ThreadCreate(
        _In_ const char*   name,
        _In_ ThreadEntry_t entry,
        _In_ void*         arguments,
        _In_ unsigned int  flags,
        _In_ UUId_t        memorySpaceHandle,
        _In_ size_t        kernelMaxStackSize,
        _In_ size_t        userMaxStackSize,
        _In_ UUId_t*       handle);

/**
 * ThreadTerminate
 * Marks the thread with the given id for finished, and it will be cleaned up
 * on next switch unless specified. The given exitcode will be stored.
 */
KERNELAPI OsStatus_t KERNELABI
ThreadTerminate(
    _In_ UUId_t ThreadId,
    _In_ int    ExitCode,
    _In_ int    TerminateChildren);

/**
 * ThreadJoin
 * Can be used to wait for a thread
 * @param ThreadId The thread handle
 * @return         Exit code of the joined thread
 */
KERNELAPI int KERNELABI
ThreadJoin(
    _In_ UUId_t ThreadId);

/**
 * ThreadDetach
 * Detaches a running thread by marking it without parent, this will make
 * sure it runs untill it kills itself.
 */
KERNELAPI OsStatus_t KERNELABI
ThreadDetach(
    _In_  UUId_t ThreadId);

/**
 * ThreadIsCurrentIdle
 * Is the given cpu running it's idle task?
 */
KERNELAPI int KERNELABI
ThreadIsCurrentIdle(
    _In_ UUId_t CoreId);

/**
 * ThreadCurrentMode
 * Returns the current run-mode for the current thread on the current cpu
 */
KERNELAPI unsigned int KERNELABI
ThreadCurrentMode(void);

/**
 * ThreadCurrentHandle
 * Retrives the current thread handle on the current cpu */
KERNELAPI UUId_t KERNELABI
ThreadCurrentHandle(void);

/**
 * ThreadCurrentForCore
 * Retrieves the current thread on the given cpu if there is any issues it returns NULL
 * @TODO Move this to cpu file
 */
KERNELAPI Thread_t* KERNELABI
ThreadCurrentForCore(
    _In_ UUId_t CoreId);

/**
 * ThreadIsRelated
 * Returns whether or not the threads are running in same address space context.
 */
KERNELAPI OsStatus_t KERNELABI
ThreadIsRelated(
    _In_ UUId_t Thread1,
    _In_ UUId_t Thread2);

/**
 * ThreadIsRoot
 * @param Thread A pointer to a thread structure
 * @return       1 If the thread is a root thread
 */
KERNELAPI int KERNELABI
ThreadIsRoot(
        _In_ Thread_t* Thread);

/**
 * ThreadHandle
 * @param Thread A pointer to a thread structure
 * @return       The handle for the thread structure
 */
KERNELAPI UUId_t KERNELABI
ThreadHandle(
        _In_ Thread_t* Thread);

/**
 * ThreadStartTime
 * @param Thread A pointer to a thread structure
 * @return       The start time for the thread
 */
KERNELAPI clock_t KERNELABI
ThreadStartTime(
        _In_ Thread_t* Thread);

/**
 * ThreadCookie
 * @param Thread A pointer to a thread structure
 * @return       The cookie for the thread
 */
KERNELAPI UUId_t KERNELABI
ThreadCookie(
        _In_ Thread_t* Thread);

/**
 * ThreadSetName
 * @param Thread A pointer to a thread structure
 * @return       Status of the operation
 */
KERNELAPI OsStatus_t KERNELABI
ThreadSetName(
        _In_ Thread_t*   Thread,
        _In_ const char* Name);

/**
 * ThreadName
 * @param Thread A pointer to a thread structure
 * @return       A pointer to a null terminated string of the threads name
 */
KERNELAPI const char* KERNELABI
ThreadName(
        _In_ Thread_t* Thread);

/**
 * ThreadFlags
 * @param Thread A pointer to a thread structure
 * @return       The current configuration flags for the Thread instance
 */
KERNELAPI unsigned int KERNELABI
ThreadFlags(
        _In_ Thread_t* Thread);

/**
 * ThreadMemorySpace
 * @param Thread A pointer to a thread structure
 * @return       A pointer to the threads memory space
 */
KERNELAPI MemorySpace_t* KERNELABI
ThreadMemorySpace(
        _In_ Thread_t* Thread);

/**
 * ThreadMemorySpaceHandle
 * @param Thread A pointer to a thread structure
 * @return       The handle for the threads memory space
 */
KERNELAPI UUId_t KERNELABI
ThreadMemorySpaceHandle(
        _In_ Thread_t* Thread);

/**
 * ThreadSchedulerHandle
 * @param Thread A pointer to a thread structure
 * @return       A pointer to the threads scheduler instance
 */
KERNELAPI SchedulerObject_t* KERNELABI
ThreadSchedulerHandle(
        _In_ Thread_t* Thread);

/**
 * ThreadData
 * @param Thread A pointer to a thread structure
 * @return       A pointer to the thread configuration data of size THREADING_CONFIGDATA_COUNT
 */
KERNELAPI uintptr_t* KERNELABI
ThreadData(
        _In_ Thread_t* Thread);

/**
 * ThreadContext
 * @param Thread A pointer to a thread structure
 * @param Context The context that should be requested
 * @return       A pointer to the thread context
 */
KERNELAPI Context_t* KERNELABI
ThreadContext(
        _In_ Thread_t* Thread,
        _In_ int       Context);

/**
 * SignalSend
 * * Dispatches a signal to a thread in the system from an external 
 * * source (i.e another thread).
 */
KERNELAPI OsStatus_t KERNELABI
SignalSend(
    _In_ UUId_t ThreadId,
    _In_ int    Signal,
    _In_ void*  Argument);

/**
 * Dispatches a signal to the current thread. This immediately loads the signal
 * context and does not return from this function (i.e an exception). This function does unconditionally
 * use the seperate signal stack to avoid issues with current stack.
 * @param context   [In] The faulting context
 * @param signal    [In] The signal handler to execute in image
 * @param argument0 [In] The first argument
 * @param argument1 [In] The second argument
 */
KERNELAPI void KERNELABI
SignalExecuteLocalThreadTrap(
        _In_ Context_t* context,
        _In_ int        signal,
        _In_ void*      argument0,
        _In_ void*      argument1);

/**
 * SignalProcessQueued
 * * Description
 */
KERNELAPI void KERNELABI
SignalProcessQueued(
    _In_ Thread_t* thread,
    _In_ Context_t*     context);

/**
 * ThreadingEnable
 * Enables the threading system for the given cpu calling the function.
 */
KERNELAPI void KERNELABI ThreadingEnable(void);

/**
 * ThreadingEnterUsermode
 * Initializes non-kernel mode and marks the thread
 * for transitioning, there is no return from this function
 * @param EntryPoint The entry point from where to execute code in usermode
 * @param Argument A pointer or value that should be passed to entry point as argument
 */
KERNELAPI _Noreturn void KERNELABI
ThreadingEnterUsermode(
        _In_ ThreadEntry_t EntryPoint,
        _In_ void*         Argument);

/**
 * ThreadingAdvance
 * This is the thread-switch function and must be be called from the below architecture
 * to get the next thread to run
 */
KERNELAPI OsStatus_t KERNELABI
ThreadingAdvance(
        _In_  int     Preemptive,
        _In_  size_t  MillisecondsPassed,
        _Out_ size_t* NextDeadlineOut);

#endif //!__THREADING_H__
