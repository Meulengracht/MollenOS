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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
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
#include <os/types/async.h>
#include <os/types/signal.h>
#include <ds/list.h>
#include <semaphore.h>
#include <mutex.h>
#include <signal.h>
#include <time.h>

// Forward some structures we need
DECL_STRUCT(MemorySpace);
DECL_STRUCT(SystemCpuCore);
DECL_STRUCT(SchedulerObject);
DECL_STRUCT(Thread);
DECL_STRUCT(PlatformThreadBlock);

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
#if defined(__aarch64__)
// Address-space setup uses page arrays on the kernel stack. Leave room for
// those arrays, nested C calls and the complete AArch64 exception frame.
#define THREADING_KERNEL_STACK_SIZE  0x8000
#else
#define THREADING_KERNEL_STACK_SIZE  0x1000
#endif

/* Flags Bit Definitions 
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
#define THREADING_RUNMODE(Flags)        ((Flags) & THREADING_MODEMASK)

// Additional Thread::Flags values.
#define THREADING_KERNELENTRY           0x00000004U  // Run the entry function in kernel mode first
#define THREADING_IDLE                  0x00000008U  // Mark this thread as an idle thread
#define THREADING_INHERIT               0x00000010U  // Inherit from creator
#define THREADING_FORKED                0x00000020U  // Continue a copied kernel system call
#define THREADING_TRANSITION_USERMODE   0x10000000U

/**
 * Thread pointers and references:
 * A reference keeps a thread record allocated while the caller uses it. Functions
 * that accept a Thread_t pointer require the caller to keep that thread valid.
 * The running thread has a scheduler reference. For another thread, use
 * AcquireGlobalHandle and release it with ReleaseGlobalHandle after use.
 *
 * Accessors add no references. Pointers into the record must not be used after
 * the reference is released. An ID alone does not keep a thread allocated.
 */

/**
 * @brief Builds a thread and queues it to run the supplied entry function.
 *
 * Call from thread context with a current thread and working memory allocation.
 * THREADING_INHERIT copies the creator's cookie and adds the new thread to its
 * child list. User threads also share the creator's process memory through a new
 * memory space. Kernel threads without an explicit space use the domain's kernel
 * space. A memory space is the set of addresses and mappings a thread can use.
 *
 * With THREADING_INHERIT clear, memorySpaceHandle may select an existing space.
 * The caller must have a grant, meaning permission in its process handle table,
 * for that space. The thread then keeps its own reference until cleanup.
 *
 * The returned ID adds no process grant or extra reference. The new thread can
 * run on another core, finish and become unavailable before this call returns.
 *
 * @param name Optional name to copy. NULL generates a name from the thread ID.
 * @param entry Function to run. Must be valid for the selected mode and space.
 * @param arguments Value passed to entry. The pointed-to data is not copied.
 * @param flags Run mode and THREADING_* options.
 * @param memorySpaceHandle Existing space, or UUID_INVALID to select one from flags.
 * @param kernelMaxStackSize Kernel stack size in bytes. Zero selects the default.
 * @param userMaxStackSize User stack size in bytes. Zero selects the default.
 * @param handle Receives the thread ID on success. Must be writable.
 * @return OS_EOK when queued, or a setup error. Failed setup releases the thread's
 *         resources. The output is only written on success.
 */
KERNELAPI oserr_t KERNELABI
ThreadCreate(
        _In_ const char*   name,
        _In_ ThreadEntry_t entry,
        _In_ void*         arguments,
        _In_ unsigned int  flags,
        _In_ uuid_t        memorySpaceHandle,
        _In_ size_t        kernelMaxStackSize,
        _In_ size_t        userMaxStackSize,
        _In_ uuid_t*       handle);

/**
 * @brief Copies the current kernel execution state to continue a blocked system call.
 *
 * Call from a system call that supports asynchronous completion. The new thread
 * shares the parent's memory space and is added to its child list. It resumes at
 * this call on a copied kernel stack, so both callers must check the return value.
 * This does not copy the whole process or start a new userspace entry function.
 *
 * @param asyncContext Completion data for the copied system call. It must remain
 *                     valid until that call reports completion to userspace.
 * @return OS_EOK in the original thread once the copy is queued, OS_EFORKED in
 *         the new thread, or an allocation or setup error in the original thread.
 */
KERNELAPI oserr_t KERNELABI
ThreadFork(
        _In_ OSAsyncContext_t* asyncContext);

/**
 * @brief Waits for another thread in the same process memory group to finish.
 *
 * Call from thread context with a writable exitCode. The target must still have
 * an ID and must be attached to a parent. The call keeps the target allocated
 * while waiting. If the waiting thread is terminated, ThreadingFinish releases
 * that reference because execution will never return through this function.
 *
 * Waits for FINISHED. A request to terminate the target is not enough to return
 * its exit code, since the target may still be using its kernel stack. A finished
 * thread whose last reference was released is no longer available for joining.
 *
 * @param threadId Target thread ID. Must differ from the calling thread's ID.
 * @param exitCode Receives the target's exit code on success. Unchanged on error.
 * @return OS_EOK on completion, OS_EINVALPARAMS for self-join or a detached target,
 *         OS_ENOENT for a missing or INITIALIZING target, OS_EUNKNOWN for a different
 *         memory group, or an error from the handle lookup or wait.
 */
KERNELAPI oserr_t KERNELABI
ThreadJoin(
    _In_  uuid_t threadId,
    _Out_ int*   exitCode);

/**
 * @brief Removes a thread from its parent's child list and clears its parent ID.
 *
 * Call from thread context. The caller must be the parent or belong to the same
 * process memory group. Detach before the parent requests exit to keep the child
 * running independently. Detaching does not undo an existing termination request.
 * New joins fail after the parent ID is cleared. A join already waiting continues.
 *
 * @param ThreadId Thread to detach. An already detached thread in the same group
 *                 is accepted.
 * @return OS_EOK on success, OS_ENOENT for a missing or INITIALIZING target,
 *         OS_EUNKNOWN if the caller is unrelated, or a handle lookup error.
 */
KERNELAPI oserr_t KERNELABI
ThreadDetach(
        _In_  uuid_t ThreadId);

/**
 * @brief Checks whether a registered core is running its idle thread.
 *
 * @param CoreId ID of a core with threading enabled.
 * @return Nonzero when the core's current thread is its idle thread, otherwise zero.
 */
KERNELAPI int KERNELABI
ThreadIsCurrentIdle(
        _In_ uuid_t CoreId);

/**
 * @brief Reads the calling thread's kernel or user run mode.
 *
 * @return THREADING_KERNELMODE or THREADING_USERMODE. Before a current thread is
 *         installed, returns THREADING_KERNELMODE.
 */
KERNELAPI unsigned int KERNELABI
ThreadCurrentMode(void);

/**
 * @brief Reads the current thread ID on the calling core.
 *
 * @return The current thread ID, or UUID_INVALID before threading is enabled.
 *         No reference is added.
 */
KERNELAPI uuid_t KERNELABI
ThreadCurrentHandle(void);

/**
 * @brief Reads the thread currently selected by a registered core.
 *
 * The result can change when that core schedules another thread. Callers reading
 * another core must arrange for the returned thread to stay valid during use.
 *
 * @param CoreId Core ID within the processor core table.
 * @return The selected thread, or NULL if the core or its thread is not set up.
 *         No reference is added.
 */
KERNELAPI Thread_t* KERNELABI
ThreadCurrentForCore(
        _In_ uuid_t CoreId);

/**
 * @brief Checks whether two threads use the same process memory group.
 *
 * The comparison uses GroupGlobalHandleID in their memory spaces. This checks
 * shared process memory, regardless of the threads' parent-child relationship.
 * Temporary references keep both records valid during the check.
 *
 * @param Thread1 First thread ID.
 * @param Thread2 Second thread ID.
 * @return OS_EOK for matching groups, OS_EUNKNOWN for different groups, OS_ENOENT
 *         for a missing or INITIALIZING thread, or a handle lookup error.
 */
KERNELAPI oserr_t KERNELABI
ThreadIsRelated(
        _In_ uuid_t Thread1,
        _In_ uuid_t Thread2);

/**
 * @brief Reads the ID stored in a thread record.
 *
 * @param Thread Thread to inspect, or NULL.
 * @return The thread ID, or UUID_INVALID for NULL. No reference is added.
 */
KERNELAPI uuid_t KERNELABI
ThreadHandle(
        _In_ Thread_t* Thread);

/**
 * @brief Reads the clock tick recorded when the thread was created.
 *
 * @param Thread Thread to inspect, or NULL.
 * @return Pointer to the stored tick, or NULL. Valid while the thread record exists.
 */
KERNELAPI UInteger64_t* KERNELABI
ThreadStartTime(
        _In_ Thread_t* Thread);

/**
 * @brief Reads the cookie assigned at creation or copied from the parent.
 *
 * @param Thread Thread to inspect, or NULL.
 * @return The stored cookie, or zero for NULL. This value is not a permission check.
 */
KERNELAPI uuid_t KERNELABI
ThreadCookie(
        _In_ Thread_t* Thread);

/**
 * @brief Replaces a thread's name with a separately allocated copy.
 *
 * The caller must prevent concurrent reads or writes of the name during this
 * call. A successful change frees the previous string, including pointers
 * previously returned by ThreadName.
 *
 * @param Thread Thread to rename.
 * @param Name Null-terminated name to copy.
 * @return OS_EOK, OS_EINVALPARAMS for NULL input, or OS_EOOM if copying fails.
 *         The previous name is left unchanged on failure.
 */
KERNELAPI oserr_t KERNELABI
ThreadSetName(
        _In_ Thread_t*   Thread,
        _In_ const char* Name);

/**
 * @brief Reads the current name without copying it.
 *
 * @param Thread Thread to inspect, or NULL.
 * @return Null-terminated name, or NULL. Valid until the next successful name
 *         change or thread cleanup. The caller must not free the string.
 */
KERNELAPI const char* KERNELABI
ThreadName(
        _In_ Thread_t* Thread);

/**
 * @brief Reads the run mode and THREADING_* options stored in a thread.
 *
 * @param Thread Thread to inspect, or NULL.
 * @return The current flags, or zero for NULL.
 */
KERNELAPI unsigned int KERNELABI
ThreadFlags(
        _In_ Thread_t* Thread);

/**
 * @brief Reads the creator's ID, unless the thread has been detached.
 *
 * @param Thread Thread to inspect, or NULL.
 * @return The stored parent ID, or UUID_INVALID for NULL or no parent. No reference
 *         is added, and the parent may already have finished.
 */
KERNELAPI uuid_t KERNELABI
ThreadParent(
        _In_ Thread_t* Thread);

/**
 * @brief Reads a thread's memory space once it is available to callers.
 *
 * Keep the thread referenced while using the returned space. This function checks
 * for INITIALIZING but does not check process permissions or add a space reference.
 * TERMINATING and FINISHED threads can still have a valid space until cleanup.
 *
 * @param Thread Thread to inspect, or NULL.
 * @return The memory space, or NULL for NULL input or an INITIALIZING thread.
 */
KERNELAPI MemorySpace_t* KERNELABI
ThreadMemorySpace(
        _In_ Thread_t* Thread);

/**
 * @brief Reads the memory space ID stored in a thread.
 *
 * Call after checking ThreadMemorySpace when looking up another thread by ID.
 * This accessor does not check the thread state or grant access to the space.
 *
 * @param Thread Thread to inspect, or NULL.
 * @return The stored space ID, or UUID_INVALID for NULL or a kernel thread using
 *         the domain space directly. No reference is added.
 */
KERNELAPI uuid_t KERNELABI
ThreadMemorySpaceHandle(
        _In_ Thread_t* Thread);

/**
 * @brief Reads the scheduler data used to queue, block and wake a thread.
 *
 * @param Thread Thread to inspect, or NULL.
 * @return The scheduler object, or NULL. Valid while the thread record exists.
 */
KERNELAPI SchedulerObject_t* KERNELABI
ThreadSchedulerHandle(
        _In_ Thread_t* Thread);

/**
 * @brief Reads the architecture-specific data stored inside a thread.
 *
 * @param thread Thread to inspect, or NULL.
 * @return Pointer to the platform data, or NULL. Valid while the thread record exists.
 */
KERNELAPI PlatformThreadBlock_t* KERNELABI
ThreadPlatformBlock(
        _In_ Thread_t* thread);

/**
 * @brief Reads the completion data for a forked system call.
 *
 * @param thread Thread to inspect, or NULL.
 * @return The supplied asynchronous context, or NULL if none was assigned.
 *         The structure is managed by the system call, not copied into the thread.
 */
KERNELAPI OSAsyncContext_t* KERNELABI
ThreadSyscallContext(
        _In_ Thread_t* thread);

/**
 * @brief Reads a saved register context and stack location for a thread.
 *
 * @param Thread Thread to inspect, or NULL.
 * @param Context THREADING_CONTEXT_LEVEL0, THREADING_CONTEXT_LEVEL1 or
 *                THREADING_CONTEXT_SIGNAL.
 * @return The requested context, or NULL for an invalid input or a context that
 *         has not been created yet. No reference is added.
 */
KERNELAPI Context_t* KERNELABI
ThreadContext(
        _In_ Thread_t* Thread,
        _In_ int       Context);

/**
 * @brief Queues a signal and asks the target core to process it.
 *
 * The kernel caller must check permission to signal this thread before calling.
 * The target must be RUNNING, which includes queued or blocked threads. The CPU
 * callback keeps a reference until it has processed the request, even if the
 * thread begins terminating before that callback runs.
 *
 * OS_EOK reports submission. For a remote core, the callback is queued before
 * its interrupt is sent. SignalSend leaves its delivery reference with that
 * callback even on an interrupt error. Retrying can submit the signal twice.
 *
 * @param ThreadId Target thread ID.
 * @param Signal Signal number in the range zero through NUMSIGNALS - 1.
 * @param Argument Value passed to the handler. Pointed-to data is not copied.
 * @return OS_EOK after submitting the signal, OS_EINVALPARAMS for an invalid signal,
 *         OS_ENOENT for a missing or non-RUNNING thread, OS_EBLOCKED for a masked
 *         signal, or a handle lookup or interrupt delivery error.
 */
KERNELAPI oserr_t KERNELABI
SignalSend(
        _In_ uuid_t ThreadId,
        _In_ int    Signal,
        _In_ void*  Argument);

/**
 * @brief Changes the faulting userspace context to enter its signal handler.
 *
 * Call while handling a trap for the current thread, with a signal handler and
 * signal stack already installed. The saved context is changed here. Execution
 * enters the handler when the trap returns, using the separate signal stack.
 * A fault at a nonzero kernel instruction address causes a kernel panic.
 *
 * @param context Saved registers for the faulting instruction.
 * @param signal Signal number packed into the handler's first argument.
 * @param flags Extra SIGNAL_FLAG_* values to include with the trap flags.
 * @param argument0 First trap-specific value passed to the handler.
 * @param argument1 Second trap-specific value passed to the handler.
 */
KERNELAPI void KERNELABI
SignalExecuteLocalThreadTrap(
        _In_ Context_t*   context,
        _In_ int          signal,
        _In_ unsigned int flags,
        _In_ void*        argument0,
        _In_ void*        argument1);

/**
 * @brief Adds queued signal handlers to a saved userspace context.
 *
 * Call on the target core while the context cannot be changed by scheduling.
 * Signals stay queued until the memory space has a handler installed. Each
 * removed queue entry reduces Pending after its handler frame has been added.
 *
 * @param thread Thread whose signal queue is processed. NULL does nothing.
 * @param context Saved context to update. NULL does nothing.
 */
KERNELAPI void KERNELABI
SignalProcessQueued(
    _In_ Thread_t*  thread,
    _In_ Context_t* context);

/**
 * @brief Sets up the calling core's idle thread as its current thread.
 *
 * Call once during core startup with the core's memory space and boot stack
 * already installed. Memory allocation must work. The idle thread uses that
 * existing stack and cannot be terminated through ThreadExit or ThreadTerminate.
 *
 * @param cpuCore The calling core, with storage for its idle thread.
 */
KERNELAPI void KERNELABI
ThreadingEnable(
        _In_ SystemCpuCore_t* cpuCore);

/**
 * @brief Selects the next thread and installs its context for interrupt return.
 *
 * Call on the current core from the scheduling interrupt with interrupts disabled.
 * A terminating thread taken from a run queue can finish immediately. The outgoing
 * thread must wait for the next outer interrupt entry, because the current return
 * path still uses its kernel stack. CpuCoreDeferFinish records that pending work.
 *
 * @param preemptive Nonzero when a timer interrupted the running thread.
 * @param nanosecondsPassed Nanoseconds elapsed since the previous scheduling step.
 * @param nextDeadlineOut Receives the scheduler's delay until its next deadline.
 * @return OS_EOK, or OS_EUNKNOWN if this core has no current thread.
 */
KERNELAPI oserr_t KERNELABI
ThreadingAdvance(
        _In_  int     preemptive,
        _In_  clock_t nanosecondsPassed,
        _Out_ clock_t* nextDeadlineOut);

/**
 * @brief Requests exit of the calling thread and its attached children.
 *
 * Does not return. Call from a non-idle thread in thread context, where taking
 * mutexes may block. Children are processed recursively before the current thread
 * is marked TERMINATING. An earlier request keeps its exit code and is not repeated.
 *
 * The thread keeps its scheduler reference while yielding. ThreadingFinish marks
 * it FINISHED and wakes joiners once its stack is unused. If yielding returns
 * without a switch, this function waits for the next scheduling interrupt.
 *
 * @param exitCode Code stored for threads newly marked TERMINATING by this call.
 *                  Joiners can read it once the target reaches FINISHED.
 */
_Noreturn KERNELAPI void KERNELABI
ThreadExit(
    _In_ int exitCode);

/**
 * @brief Requests exit of another thread and its attached children.
 *
 * Call from thread context, where taking mutexes may block. This kernel operation
 * checks no process relationship. Its caller must decide whether ending the target
 * is allowed. Use ThreadExit to end the calling thread itself.
 *
 * Each newly marked thread gets exitCode and enters TERMINATING. A queued or
 * blocked thread is made ready for scheduling so it can be finished. A running
 * thread continues until the scheduler switches away from it. Existing termination
 * requests keep their exit codes and do not visit the child list again.
 *
 * An INITIALIZING target cannot be selected by ID. The child-list walk can mark
 * an INITIALIZING child, which its creator still queues for the scheduler to finish.
 * If the caller is below the target in that list, it can also be marked TERMINATING.
 * Detach a child before termination is requested if it must keep running.
 *
 * @param threadId Target ID. Must differ from the calling thread's ID.
 * @param exitCode Code stored for each thread newly marked by this call.
 * @return OS_EOK when the request was processed or the target was already ending,
 *         OS_EINVALPARAMS for the caller's ID, OS_ENOENT for a missing or INITIALIZING
 *         target, OS_EPERMISSIONS for an idle target, or a handle lookup error.
 *         Success does not wait for the target to reach FINISHED.
 */
KERNELAPI oserr_t KERNELABI
ThreadTerminate(
    _In_ uuid_t threadId,
    _In_ int    exitCode);

#endif //!__THREADING_H__
