/**
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Threading Interface
 * - Handles all common threading across architectures
 *   and implements systems like signaling, synchronization and rpc
 */

#ifndef __VALI_THREADING_PRIVATE_H__
#define __VALI_THREADING_PRIVATE_H__

#include "os/osdefs.h"
#include "os/types/async.h"
#include "arch/platform.h"

// Forward some structures we need
DECL_STRUCT(MemorySpace);
DECL_STRUCT(SystemProcess);
DECL_STRUCT(SchedulerObject);
DECL_STRUCT(streambuffer);

#ifndef __THREADING_ENTRY
#define __THREADING_ENTRY
typedef void(*ThreadEntry_t)(void*);
#endif

typedef struct ThreadSignal {
    int          Signal;
    void*        Argument;
    unsigned int Flags;
} ThreadSignal_t;

typedef struct ThreadSignals {
    streambuffer_t* Signals;
    _Atomic(int)    Pending;
    unsigned int    Mask;
} ThreadSignals_t;

/**
 * @brief Execution state stored in Thread_t::State. Creation starts at
 * INITIALIZING, then __MarkThreadReady sets RUNNING. A termination request can
 * replace either state with TERMINATING. ThreadingFinish sets FINISHED once the
 * stack is unused. The global handle's references separately control cleanup.
 * Compare named states explicitly. Their numeric values are not in time order,
 * and RUNNING is zero, so a boolean test cannot distinguish completion.
 */
enum ThreadState {
    // Normal execution, including time spent queued or blocked in a wait.
    ThreadState_RUNNING = 0,

    // Exit has been requested, and the thread may still be running. The
    // scheduler calls ThreadingFinish once it is no longer queued or using a stack.
    ThreadState_TERMINATING,

    // The thread cannot run again. Joiners can read RetCode. References may
    // still keep the record allocated after this state is reached.
    ThreadState_FINISHED,

    // The thread has a global handle but is not fully built, so lookups by
    // handle must treat it as missing. A parent can still flag it as
    // TERMINATING, and its creator must keep that when it queues the thread.
    ThreadState_INITIALIZING
};

typedef struct Thread {
    MemorySpace_t*          MemorySpace;
    uuid_t                  MemorySpaceHandle;
    SchedulerObject_t*      SchedulerObject;
    Context_t*              ContextActive;

    uuid_t                  Handle;
    // The parent handle ID only. No reference is kept.
    _Atomic(uuid_t)         ParentHandle;

    Mutex_t                 SyncObject;

    _Atomic(uuid_t)         JoinTarget;
    UInteger64_t            StartedAt;
    struct Thread*          Children;
    struct Thread*          Sibling;
    _Atomic(int)            State;

    const char*             Name;
    unsigned int            Flags;
    uuid_t                  Cookie;
    ThreadEntry_t           Function;
    void*                   Arguments;
    int                     RetCode;
    size_t                  KernelStackSize;
    size_t                  UserStackSize;

    Context_t*              Contexts[THREADING_NUMCONTEXTS];
    PlatformThreadBlock_t   PlatformData;

    // Completion data supplied to ThreadFork. The copied system call writes its
    // result here and wakes the userspace waiter before calling ThreadExit.
    OSAsyncContext_t* SyscallContext;

    // Pending signals are added to a saved userspace context on system call
    // return, or by a callback on the target core if it is already in userspace.
    ThreadSignals_t Signaling;
} Thread_t;

/**
 * @brief Marks a TERMINATING thread FINISHED, wakes joiners and releases its
 * scheduler reference. Called only by ThreadingAdvance and CpuCoreEnterInterrupt.
 *
 * Preconditions: runs in interrupt context and must not block. The thread is in
 * no run queue, and its stack is no longer in use on any core. The caller passes
 * an existing scheduler reference. Do not use the thread after this call without
 * a separate reference. A final release queues cleanup on the handle janitor.
 *
 * If this thread was waiting in ThreadJoin, its JoinTarget reference is released
 * here as well. That wait will never return to perform its normal cleanup.
 *
 * @param threadId The thread to finish.
 */
void
ThreadingFinish(
    _In_ uuid_t threadId);

#endif //__VALI_THREADING_PRIVATE_H__
