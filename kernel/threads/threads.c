/**
 * Copyright 2017, Philip Meulengracht
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
 * Thread creation, scheduling and exit shared by the supported architectures.
 * Public calling rules are in threading.h. State and reference handling are
 * described in lifecycle.md beside this file.
 */

#define __MODULE "thread"
//#define __TRACE
//#define __OSCONFIG_DEBUG_SCHEDULER
#define __need_quantity

#include <arch/interrupts.h>
#include <arch/thread.h>
#include <arch/utils.h>
#include <assert.h>
#include <component/cpu.h>
#include <component/timer.h>
#include <debug.h>
#include <ds/streambuffer.h>
#include <futex.h>
#include <handles/global.h>
#include <handles/handle.h>
#include <heap.h>
#include <limits.h>
#include <memoryspace.h>
#include <string.h>
#include <stdio.h>
#include <threading.h>

#include "private.h"

_Noreturn static void __ThreadStart(void);
static void    __DestroyThread(void* resource);
static void    __AbortThreadCreation(Thread_t* thread);
static oserr_t __CreateThreadContexts(Thread_t* thread);
static oserr_t __InitializeDefaultsForThread(Thread_t* thread, const char* name,
                                                 ThreadEntry_t threadEntry, void* arguments,
                                                 unsigned int flags, size_t kernelStackSize,
                                                 size_t userStackSize);
static size_t __GetDefaultStackSize(unsigned int threadFlags);
static uuid_t __CreateCookie(Thread_t* thread, Thread_t* parent);
static void   __AddChild(Thread_t* parent, Thread_t* child);
static void   __RemoveChild(Thread_t* parent, Thread_t* child);
static void   __MarkThreadReady(Thread_t* thread);
static int    __RequestTermination(Thread_t* thread);

void
ThreadingEnable(
    _In_ SystemCpuCore_t* cpuCore)
{
    Thread_t* thread;
    oserr_t   oserr;

    assert(cpuCore != NULL);

    thread = CpuCoreIdleThread(cpuCore);
    oserr = __InitializeDefaultsForThread(
        thread,
        "idle",
        NULL,
        NULL,
        THREADING_KERNELMODE | THREADING_IDLE,
        THREADING_KERNEL_STACK_SIZE,
        0
    );
    if (oserr != OS_EOK) {
        assert(0);
    }

    // The idle threads have pre-allocated stacks, and must manually
    // be setup according to the current stack.
    thread->Contexts[THREADING_CONTEXT_LEVEL0] = ArchThreadContextIdle();

    // Startup already installed this core's memory space. The idle thread uses
    // it directly and keeps the boot stack rather than allocating a new one.
    thread->MemorySpace       = GetCurrentMemorySpace();
    thread->MemorySpaceHandle = GetCurrentMemorySpaceHandle();
    __MarkThreadReady(thread);
    CpuCoreSetCurrentThread(CpuCoreCurrent(), thread);
}

// The parent must stay referenced throughout this call. A successful copy of a
// valid space handle adds a reference that __DestroyThread releases later.
static oserr_t
__InheritAddressSpace(
    _In_ Thread_t* parent,
    _In_ Thread_t* thread)
{
    MemorySpace_t* memorySpace;
    oserr_t        oserr;

    // A parent with no space handle uses the domain's kernel memory space.
    // That space stays allocated independently of individual threads.
    if (parent->MemorySpaceHandle == UUID_INVALID) {
        thread->MemorySpace       = GetDomainMemorySpace();
        thread->MemorySpaceHandle = UUID_INVALID;
        return OS_EOK;
    }

    // The parent already keeps this space allocated. Add the child's reference
    // directly, without depending on permission in the current process handle table.
    oserr = AcquireGlobalHandle(
        parent->MemorySpaceHandle,
        HandleTypeMemorySpace,
        (void**)&memorySpace
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    thread->MemorySpace       = memorySpace;
    thread->MemorySpaceHandle = parent->MemorySpaceHandle;
    return OS_EOK;
}

// The thread handle must already exist. Failure leaves Name as NULL so creation
// can use the same cleanup path as a failed copy of a caller-supplied name.
static void
__SetDefaultName(
    _In_ Thread_t* thread)
{
    char buffer[16];
    memset(&buffer[0], 0, sizeof(buffer));
    sprintf(&buffer[0], "thread %" PRIuIN, thread->Handle);
    thread->Name = strdup(&buffer[0]);
}

// __InheritThreadFlags only returns flags that we want to include from
// the parent thread when forking. This will not include any run-mode as
// all forked threads are currently kernel threads.
static unsigned int __InheritThreadFlags(unsigned int flags)
{
    return flags & (THREADING_INHERIT);
}

oserr_t
ThreadFork(
    _In_ OSAsyncContext_t* asyncContext)
{
    Thread_t* parent;
    Thread_t* thread;
    Context_t returnContext;
    oserr_t   oserr;
    TRACE("ThreadFork()");

    // Clone the current kernel context
    if (GetContext(&returnContext)) {
        // We are the forked thread
        return OS_EFORKED;
    }

    // The copied stack resumes at GetContext. Give that return a nonzero result
    // so the new thread returns OS_EFORKED instead of creating another copy.
    CONTEXT_SC_RET0(&returnContext) = 1;

    // Create new thread structure
    thread = (Thread_t*)kmalloc(sizeof(Thread_t));
    if (!thread) {
        return OS_EOOM;
    }
    memset(thread, 0, sizeof(Thread_t));
    MutexConstruct(&thread->SyncObject, MUTEX_FLAG_PLAIN);
    thread->Handle = UUID_INVALID;
    thread->ParentHandle = UUID_INVALID;
    thread->MemorySpaceHandle = UUID_INVALID;
    thread->JoinTarget = UUID_INVALID;
    thread->State = ThreadState_INITIALIZING;

    // Get current stack pointer, get top of stack, clone it.
    parent = ThreadCurrentForCore(ArchGetProcessorCoreId());
    thread->KernelStackSize = parent->KernelStackSize;
    
    oserr = ArchThreadContextFork(
        parent->Contexts[THREADING_CONTEXT_LEVEL0],
        &returnContext,
        THREADING_CONTEXT_LEVEL0,
        parent->KernelStackSize,
        &thread->Contexts[THREADING_CONTEXT_LEVEL0],
        &thread->ContextActive
    );
    if (oserr != OS_EOK) {
        __AbortThreadCreation(thread);
        return oserr;
    }

    // This first reference becomes the scheduler reference when the thread is
    // queued. Until then, __AbortThreadCreation releases it if setup fails.
    thread->Handle = CreateGlobalHandle(
        HandleTypeThread,
        __DestroyThread,
        thread
    );
    if (thread->Handle == UUID_INVALID) {
        __AbortThreadCreation(thread);
        return OS_EOOM;
    }

    oserr = streambuffer_create(
        sizeof(ThreadSignal_t) * THREADING_MAX_QUEUED_SIGNALS,
        STREAMBUFFER_MULTIPLE_WRITERS | STREAMBUFFER_GLOBAL,
        &thread->Signaling.Signals
    );
    if (oserr != OS_EOK) {
        __AbortThreadCreation(thread);
        return OS_EOOM;
    }

    __SetDefaultName(thread);
    if (!thread->Name) {
        __AbortThreadCreation(thread);
        return OS_EOOM;
    }

    thread->SchedulerObject = SchedulerCreateObject(thread, parent->Flags | THREADING_FORKED);
    if (thread->SchedulerObject == NULL) {
        __AbortThreadCreation(thread);
        return OS_EOOM;
    }

    oserr = ArchThreadInitialize(thread);
    if (oserr != OS_EOK) {
        __AbortThreadCreation(thread);
        return oserr;
    }

    // Keep the copied system call's entry data and completion pointer. The new
    // thread continues in kernel mode, even when its parent started in userspace.
    thread->Function        = parent->Function;
    thread->Arguments       = parent->Arguments;
    thread->Flags           = __InheritThreadFlags(parent->Flags) | THREADING_FORKED;
    thread->ParentHandle    = parent->Handle;
    thread->KernelStackSize = parent->KernelStackSize;
    thread->UserStackSize   = 0; // Forked threads are kernel only
    thread->SyscallContext  = asyncContext;
    SystemTimerGetClockTick(&thread->StartedAt);

    // Inherit the same address space, even if an application.
    oserr = __InheritAddressSpace(parent, thread);
    if (oserr != OS_EOK) {
        __AbortThreadCreation(thread);
        return oserr;
    }

    // Adding the child lets a parent termination request find it. Keep a request
    // made during that window when marking the thread ready, then queue it anyway.
    __AddChild(parent, thread);
    __MarkThreadReady(thread);
    TRACE("ThreadFork new thread %s on core %u",
        thread->Name,
        SchedulerObjectGetAffinity(thread->SchedulerObject)
    );
    SchedulerQueueObject(thread->SchedulerObject);
    return OS_EOK;
}

oserr_t
ThreadCreate(
    _In_ const char*   name,
    _In_ ThreadEntry_t entry,
    _In_ void*         arguments,
    _In_ unsigned int  flags,
    _In_ uuid_t        memorySpaceHandle,
    _In_ size_t        kernelMaxStackSize,
    _In_ size_t        userMaxStackSize,
    _In_ uuid_t*       handle)
{
    Thread_t*      thread;
    Thread_t*      parent;
    MemorySpace_t* memorySpace;
    uuid_t         coreId;
    uuid_t         securityGroupID;
    unsigned int   memorySpaceFlags = 0;
    oserr_t        oserr;

    TRACE("ThreadCreate(name=%s, entry=0x%" PRIxIN ", argments=0x%" PRIxIN ", flags=0x%x, memorySpaceHandle=%u"
          "kernelMaxStackSize=%" PRIuIN ", userMaxStackSize=%" PRIuIN ")",
          name, entry, arguments, flags, memorySpaceHandle, kernelMaxStackSize, userMaxStackSize);

    coreId = ArchGetProcessorCoreId();
    parent = ThreadCurrentForCore(coreId);

    thread = (Thread_t*)kmalloc(sizeof(Thread_t));
    if (!thread) {
        return OS_EOOM;
    }

    oserr = __InitializeDefaultsForThread(
        thread,
        name,
        entry,
        arguments,
        flags,
        kernelMaxStackSize,
        userMaxStackSize
    );
    if (oserr != OS_EOK) {
        __AbortThreadCreation(thread);
        return oserr;
    }

    oserr = __CreateThreadContexts(thread);
    if (oserr != OS_EOK) {
        __AbortThreadCreation(thread);
        return oserr;
    }
    
    // Setup parent and cookie information
    thread->ParentHandle = parent->Handle;
    if (flags & THREADING_INHERIT) {
        thread->Cookie = parent->Cookie;
    }
    else {
        thread->Cookie = __CreateCookie(thread, parent);
    }

    // An explicit space is used only when inheritance is disabled. Application
    // spaces also set the thread's user-mode flag.
    if (!(flags & THREADING_INHERIT) && memorySpaceHandle != UUID_INVALID) {
        // Check the caller's permission, then add a kernel reference to the space.
        // The janitor releases it later while running in a different process.
        oserr = AcquireHandleReference(
            memorySpaceHandle,
            HandleTypeMemorySpace,
            (void**)&memorySpace
        );
        if (oserr != OS_EOK) {
            __AbortThreadCreation(thread);
            return oserr;
        }

        if (memorySpace->Flags & MEMORY_SPACE_APPLICATION) {
            thread->Flags |= THREADING_USERMODE;
        }
        thread->MemorySpace        = memorySpace;
        thread->MemorySpaceHandle  = memorySpaceHandle;
    } else {
        if (THREADING_RUNMODE(flags) == THREADING_KERNELMODE) {
            thread->MemorySpace       = GetDomainMemorySpace();
            thread->MemorySpaceHandle = UUID_INVALID;
        } else {
            if (THREADING_RUNMODE(flags) == THREADING_USERMODE) {
                memorySpaceFlags |= MEMORY_SPACE_APPLICATION;
            }
            if (flags & THREADING_INHERIT) {
                memorySpaceFlags |= MEMORY_SPACE_INHERIT;
            }

            oserr = CreateMemorySpace(
                memorySpaceFlags,
                &memorySpaceHandle,
                &securityGroupID
            );
            if (oserr != OS_EOK) {
                __AbortThreadCreation(thread);
                return OS_EUNKNOWN;
            }
            // Keep a kernel reference before dropping the handle created for the
            // caller. The thread must still have its space after the caller exits.
            oserr = AcquireHandleReference(
                memorySpaceHandle,
                HandleTypeMemorySpace,
                (void**)&thread->MemorySpace
            );
            DestroyHandle(memorySpaceHandle);
            if (oserr != OS_EOK) {
                __AbortThreadCreation(thread);
                return oserr;
            }
            thread->MemorySpaceHandle = memorySpaceHandle;
        }
    }

    if (flags & THREADING_INHERIT) {
        __AddChild(parent, thread);
    }

    TRACE("[ThreadCreate] new thread %s on core %u", thread->Name, SchedulerObjectGetAffinity(thread->SchedulerObject));
    // The ID is published only after construction. Once queued on another CPU,
    // the thread may finish and disappear before this function returns.
    *handle = thread->Handle;
    __MarkThreadReady(thread);
    SchedulerQueueObject(thread->SchedulerObject);
    return OS_EOK;
}

oserr_t
ThreadDetach(
    _In_ uuid_t threadId)
{
    Thread_t* current = ThreadCurrentForCore(ArchGetProcessorCoreId());
    Thread_t* target;
    Thread_t* parent;
    uuid_t    parentId;
    oserr_t   oserr;

    oserr = AcquireGlobalHandle(threadId, HandleTypeThread, (void**)&target);
    if (oserr != OS_EOK) {
        return oserr;
    }
    if (atomic_load(&target->State) == ThreadState_INITIALIZING) {
        oserr = OS_ENOENT;
        goto exit;
    }

    // Detach is allowed for the creator or a thread sharing the target's process
    // memory group. Holding a reference only keeps the record allocated.
    parentId = atomic_load(&target->ParentHandle);
    oserr = AreMemorySpacesRelated(current->MemorySpace, target->MemorySpace);
    if (parentId != current->Handle && oserr != OS_EOK) {
        goto exit;
    }

    if (AcquireGlobalHandle(parentId, HandleTypeThread, (void**)&parent) == OS_EOK) {
        __RemoveChild(parent, target);
        ReleaseGlobalHandle(parentId);
    }
    atomic_store(&target->ParentHandle, UUID_INVALID);
    oserr = OS_EOK;

exit:
    ReleaseGlobalHandle(threadId);
    return oserr;
}

// The thread must stay allocated throughout the walk. Call from thread context,
// since child mutexes may block. Each parent mutex keeps its child pointers valid
// while recursive calls take the child mutexes. Existing requests return early,
// leaving their exit code and any later additions to their child list unchanged.
static oserr_t
__TerminateWithChildren(
    _In_ Thread_t* thread,
    _In_ int       exitCode)
{
    Thread_t*  child;
    irqstate_t irqState;
    oserr_t    status;
    int        state;

    TRACE("__TerminateWithChildren(%s, %i)", thread->Name, exitCode);

    // Never, ever kill system idle threads
    if (thread->Flags & THREADING_IDLE) {
        return OS_EPERMISSIONS;
    }

    // Only a thread that has not been flagged yet is processed. This includes
    // a thread that is still initializing, as its creator is about to queue it.
    MutexLock(&thread->SyncObject);
    state = atomic_load(&thread->State);
    if (state == ThreadState_TERMINATING || state == ThreadState_FINISHED) {
        MutexUnlock(&thread->SyncObject);
        return OS_EOK;
    }

    child = thread->Children;
    while (child) {
        status = __TerminateWithChildren(child, exitCode);
        if (status != OS_EOK) {
            ERROR("__TerminateWithChildren failed to terminate child %s of %s", child->Name, thread->Name);
        }
        child = child->Sibling;
    }

    // Once TERMINATING is set, a timer can finish the calling thread. Disable
    // interrupts while setting that state and releasing the mutex. Child processing
    // above may block, so it needs interrupts enabled. See lifecycle.md for the
    // remaining case where another thread has already requested our termination.
    irqState = InterruptDisable();
    thread->RetCode = exitCode;
    state = __RequestTermination(thread);
    MutexUnlock(&thread->SyncObject);
    InterruptRestoreState(irqState);

    // A blocked target needs to become ready so the scheduler can finish it.
    // The caller yields through ThreadExit or reaches a scheduling interrupt.
    // An INITIALIZING child still needs its creator to queue it after setup.
    if (state == ThreadState_RUNNING && thread->Handle != ThreadCurrentHandle()) {
        SchedulerExpediteObject(thread->SchedulerObject);
    }
    return OS_EOK;
}

oserr_t
ThreadTerminate(
    _In_ uuid_t threadId,
    _In_ int    exitCode)
{
    Thread_t* current = ThreadCurrentForCore(ArchGetProcessorCoreId());
    Thread_t* thread;
    oserr_t   oserr;

    // The calling thread must use ThreadExit, which also leaves the CPU. It
    // already has the scheduler reference, so it never takes the one below.
    if (threadId == current->Handle) {
        return OS_EINVALPARAMS;
    }

    oserr = AcquireGlobalHandle(threadId, HandleTypeThread, (void**)&thread);
    if (oserr != OS_EOK) {
        return oserr;
    }

    // A thread that is still being created is not visible by id yet.
    if (atomic_load(&thread->State) == ThreadState_INITIALIZING) {
        oserr = OS_ENOENT;
    } else {
        oserr = __TerminateWithChildren(thread, exitCode);
    }

    ReleaseGlobalHandle(threadId);
    return oserr;
}

_Noreturn void
ThreadExit(
    _In_ int exitCode)
{
    Thread_t* current = ThreadCurrentForCore(ArchGetProcessorCoreId());

    // The running thread already has the scheduler reference. Taking another
    // one would leak, as the scheduler can finish this thread before we return.
    assert((current->Flags & THREADING_IDLE) == 0);
    (void)__TerminateWithChildren(current, exitCode);

    // The thread is only flagged here, and keeps running until the scheduler
    // switches away from it.
    ArchThreadYield();

    // Catch all, the thread must not escape. If the yield returns without
    // switching, the next timer tick moves this thread off the CPU.
    for (;;) { }
}

oserr_t
ThreadJoin(
    _In_  uuid_t threadId,
    _Out_ int*   exitCode)
{
    Thread_t*  current = ThreadCurrentForCore(ArchGetProcessorCoreId());
    Thread_t*  target;
    irqstate_t irqState;
    oserr_t    oserr;
    int        state;

    if (threadId == current->Handle) {
        return OS_EINVALPARAMS;
    }

    // Record the reference before a scheduling interrupt can finish this thread.
    // If that happens during the wait, ThreadingFinish releases JoinTarget because
    // execution will never reach the cleanup at the end of this function.
    irqState = InterruptDisable();
    oserr = AcquireGlobalHandle(threadId, HandleTypeThread, (void**)&target);
    if (oserr == OS_EOK) {
        assert(atomic_load(&current->JoinTarget) == UUID_INVALID);
        atomic_store(&current->JoinTarget, threadId);
    }
    InterruptRestoreState(irqState);
    if (oserr != OS_EOK) {
        return oserr;
    }

    if (atomic_load(&target->State) == ThreadState_INITIALIZING) {
        oserr = OS_ENOENT;
        goto exit;
    }

    oserr = AreMemorySpacesRelated(current->MemorySpace, target->MemorySpace);
    if (oserr != OS_EOK) {
        goto exit;
    }

    if (atomic_load(&target->ParentHandle) == UUID_INVALID) {
        oserr = OS_EINVALPARAMS;
        goto exit;
    }

    // FutexWait only sleeps if State still matches the value read here. If the
    // target finishes between the read and the wait, the changed value prevents
    // sleeping past the wake. TERMINATING still requires a wait for stack cleanup.
    for (;;) {
        state = atomic_load(&target->State);
        if (state == ThreadState_FINISHED) {
            *exitCode = target->RetCode;
            oserr = OS_EOK;
            break;
        }

        oserr = FutexWait(NULL, &target->State, state, 0, NULL, 0, 0, NULL);
        if (oserr != OS_EOK && oserr != OS_EINTERRUPTED) {
            break;
        }
    }

exit:
    // Clearing the recorded target and releasing it must complete before a
    // scheduling interrupt can make ThreadingFinish inspect this thread.
    irqState = InterruptDisable();
    threadId = atomic_exchange(&current->JoinTarget, UUID_INVALID);
    ReleaseGlobalHandle(threadId);
    InterruptRestoreState(irqState);
    return oserr;
}

_Noreturn static void
__EnterUsermode(
    _In_ ThreadEntry_t threadEntry,
    _In_ void*         argument)
{
    Thread_t* thread = ThreadCurrentForCore(ArchGetProcessorCoreId());
    vaddr_t   tlsAddress;
    paddr_t   tlsPhysicalAddress;
    oserr_t   osStatus;

    // Allocate one page for thread-local storage (TLS), the per-thread data used
    // by the runtime. This must happen after the thread's memory space is active.
    osStatus = MemorySpaceMap(
        GetCurrentMemorySpace(),
        &(struct MemorySpaceMapOptions) {
            .Pages = &tlsPhysicalAddress,
            .Length = GetMemorySpacePageSize(),
            .Mask = __MASK,
            .Flags = MAPPING_DOMAIN | MAPPING_USERSPACE | MAPPING_COMMIT,
            .PlacementFlags = MAPPING_VIRTUAL_THREAD
        },
        &tlsAddress
    );
    if (osStatus != OS_EOK) {
        FATAL(FATAL_SCOPE_THREAD, "__EnterUsermode failed to map TLS page");
    }

#if defined(__aarch64__)
    // Setup the TLS area and address for aarch64 here, before we enter
    // userspace for the new thread.
    ThreadPlatformBlock(thread)->UserTls = tlsAddress;
    memset((void*)tlsAddress, 0, GetMemorySpacePageSize());
#endif

    // Create the userspace stack(s) now that we will need it
    thread->Contexts[THREADING_CONTEXT_LEVEL1] = ArchThreadContextCreate(
        THREADING_CONTEXT_LEVEL1,
        thread->UserStackSize
    );
    thread->Contexts[THREADING_CONTEXT_SIGNAL] = ArchThreadContextCreate(
        THREADING_CONTEXT_SIGNAL,
        thread->UserStackSize
    );
    
    if (!thread->Contexts[THREADING_CONTEXT_LEVEL1] || 
            !thread->Contexts[THREADING_CONTEXT_SIGNAL]) {
        assert(0);
    }

    // initiate the userspace stack with entry point and argument
    ArchThreadContextReset(
        thread->Contexts[THREADING_CONTEXT_LEVEL1],
        THREADING_CONTEXT_LEVEL1,
        (uintptr_t)threadEntry,
        (uintptr_t)argument
    );

    // initiate switch to userspace
    thread->Flags |= THREADING_TRANSITION_USERMODE;
    ArchThreadYield();
    for (;;);
}

Thread_t*
ThreadCurrentForCore(
    _In_ uuid_t CoreId)
{
    return CpuCoreCurrentThread(GetProcessorCore(CoreId));
}

uuid_t
ThreadCurrentHandle(void)
{
    Thread_t* thread = CpuCoreCurrentThread(CpuCoreCurrent());
    if (!thread) {
        return UUID_INVALID;
    }

    return thread->Handle;
}

oserr_t
ThreadIsRelated(
    _In_ uuid_t firstId,
    _In_ uuid_t secondId)
{
    Thread_t* first;
    Thread_t* second;
    oserr_t   oserr;

    oserr = AcquireGlobalHandle(firstId, HandleTypeThread, (void**)&first);
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = AcquireGlobalHandle(secondId, HandleTypeThread, (void**)&second);
    if (oserr == OS_EOK) {
        if (atomic_load(&first->State) == ThreadState_INITIALIZING ||
            atomic_load(&second->State) == ThreadState_INITIALIZING) {
            oserr = OS_ENOENT;
        } else {
            oserr = AreMemorySpacesRelated(first->MemorySpace, second->MemorySpace);
        }
        ReleaseGlobalHandle(secondId);
    }
    ReleaseGlobalHandle(firstId);
    return oserr;
}

int
ThreadIsCurrentIdle(
    _In_ uuid_t CoreId)
{
    SystemCpuCore_t* core = GetProcessorCore(CoreId);
    return (CpuCoreCurrentThread(core) == CpuCoreIdleThread(core)) ? 1 : 0;
}

unsigned int
ThreadCurrentMode(void)
{
    if (ThreadCurrentForCore(ArchGetProcessorCoreId()) == NULL) {
        return THREADING_KERNELMODE;
    }
    return ThreadCurrentForCore(ArchGetProcessorCoreId())->Flags & THREADING_MODEMASK;
}

uuid_t
ThreadHandle(
    _In_ Thread_t* Thread)
{
    if (!Thread) {
        return UUID_INVALID;
    }
    return Thread->Handle;
}

uuid_t
ThreadParent(
    _In_ Thread_t* Thread)
{
    if (!Thread) {
        return UUID_INVALID;
    }
    return Thread->ParentHandle;
}

UInteger64_t*
ThreadStartTime(
    _In_ Thread_t* Thread)
{
    if (!Thread) {
        return 0;
    }
    return &Thread->StartedAt;
}

uuid_t
ThreadCookie(
    _In_ Thread_t* Thread)
{
    if (!Thread) {
        return 0;
    }
    return Thread->Cookie;
}

oserr_t
ThreadSetName(
    _In_ Thread_t*   Thread,
    _In_ const char* Name)
{
    const char* previousName;
    char*       nameCopy;

    if (!Thread || !Name) {
        return OS_EINVALPARAMS;
    }

    previousName = Thread->Name;
    nameCopy = strdup(Name);
    if (!nameCopy) {
        return OS_EOOM;
    }

    Thread->Name = (const char*)nameCopy;
    kfree((void*)previousName);
    return OS_EOK;
}

const char*
ThreadName(
    _In_ Thread_t* Thread)
{
    if (!Thread) {
        return NULL;
    }
    return Thread->Name;
}

unsigned int
ThreadFlags(
    _In_ Thread_t* Thread)
{
    if (!Thread) {
        return 0;
    }
    return Thread->Flags;
}

OSAsyncContext_t*
ThreadSyscallContext(
    _In_ Thread_t* thread)
{
    if (!thread) {
        return NULL;
    }
    return thread->SyscallContext;
}

MemorySpace_t*
ThreadMemorySpace(
    _In_ Thread_t* Thread)
{
    if (!Thread) {
        return NULL;
    }

    // A global ID exists during construction, but its memory space must not
    // be exposed until the remaining thread resources are ready as well.
    if (atomic_load(&Thread->State) == ThreadState_INITIALIZING) {
        return NULL;
    }
    return Thread->MemorySpace;
}

uuid_t
ThreadMemorySpaceHandle(
    _In_ Thread_t* Thread)
{
    if (!Thread) {
        return UUID_INVALID;
    }
    return Thread->MemorySpaceHandle;
}

SchedulerObject_t*
ThreadSchedulerHandle(
    _In_ Thread_t* Thread)
{
    if (!Thread) {
        return NULL;
    }
    return Thread->SchedulerObject;
}

PlatformThreadBlock_t*
ThreadPlatformBlock(
    _In_ Thread_t* thread)
{
    if (!thread) {
        return NULL;
    }
    return &thread->PlatformData;
}

Context_t*
ThreadContext(
    _In_ Thread_t* Thread,
    _In_ int       Context)
{
    if (!Thread || Context < 0 || Context >= THREADING_NUMCONTEXTS) {
        return NULL;
    }
    return Thread->Contexts[Context];
}

oserr_t
ThreadingAdvance(
    _In_  int      preemptive,
    _In_  clock_t  nanosecondsPassed,
    _Out_ clock_t* nextDeadlineOut)
{
    SystemCpuCore_t* core          = CpuCoreCurrent();
    Thread_t*        currentThread = CpuCoreCurrentThread(core);
    Thread_t*        nextThread;
    uuid_t           pendingFinishHandle = UUID_INVALID;
    int              signalsPending;
    int              state;

    // Is threading disabled?
    if (!currentThread) {
        return OS_EUNKNOWN;
    }

    state = atomic_load(&currentThread->State);
    currentThread->ContextActive = CpuCoreInterruptContext(core);
    
    // Save architecture-specific state only for a thread that can run again.
    if (state == ThreadState_RUNNING) {
        ArchThreadLeave(currentThread);
        
        // A system call may have queued a wait before receiving a signal. Make
        // that thread ready again so it can return and process its pending signals.
        signalsPending = atomic_load(&currentThread->Signaling.Pending);
        if (signalsPending) {
            SchedulerExpediteObject(currentThread->SchedulerObject);
        }
    }

#ifdef __OSCONFIG_DEBUG_SCHEDULER
    TRACE("%u: current thread: %s (Context 0x%" PRIxIN ", IP 0x%" PRIxIN ", PreEmptive %i)",
          CpuCoreId(core), currentThread->Name,
          currentThread->ContextActive, CONTEXT_IP(currentThread->ContextActive), preemptive);
#endif

GetNextThread:
    if ((currentThread->Flags & THREADING_IDLE) || state == ThreadState_TERMINATING) {
        if (state == ThreadState_TERMINATING) {
            if (currentThread == CpuCoreCurrentThread(core)) {
                // Interrupt return still runs on this thread's kernel stack
                // after ArchThreadEnter installs the next address space, so
                // the core finishes the thread at its next interrupt entry.
                pendingFinishHandle = currentThread->Handle;
            } else {
                // The thread was popped from a run queue and never ran again,
                // so its stack is not in use and it can be finished now.
                ThreadingFinish(currentThread->Handle);
            }
        }
#ifdef __OSCONFIG_DEBUG_SCHEDULER
        TRACE(" > (null-schedule) initial next thread: %s", (nextThread) ? nextThread->Name : "null");
#endif
        currentThread = NULL;
    }
    
    // Advance the scheduler
    nextThread = (Thread_t*)SchedulerAdvance(
            (currentThread != NULL) ? currentThread->SchedulerObject : NULL,
            preemptive,
            nanosecondsPassed,
            nextDeadlineOut
    );
    
    // An empty run queue selects idle. Check every selected thread's state before
    // restoring its context, since a termination request may have arrived in queue.
    if (nextThread == NULL) {
#ifdef __OSCONFIG_DEBUG_SCHEDULER
        TRACE("[threading] [switch] selecting idle");
#endif
        nextThread = CpuCoreIdleThread(core);
    }
    else {
        state = atomic_load(&nextThread->State);
        if (state == ThreadState_TERMINATING) {
            currentThread = nextThread;
            goto GetNextThread;
        }
    }

    // Handle level switch, thread startup
    if (nextThread->Flags & THREADING_TRANSITION_USERMODE) {
        nextThread->Flags &= ~(THREADING_TRANSITION_USERMODE);
        nextThread->ContextActive = nextThread->Contexts[THREADING_CONTEXT_LEVEL1];
    }
    
    // Newly started threads have no active context, except for idle threads
    if (nextThread->ContextActive == NULL) {
        nextThread->ContextActive = nextThread->Contexts[THREADING_CONTEXT_LEVEL0];
    }

#ifdef __OSCONFIG_DEBUG_SCHEDULER
    TRACE("%u: next thread: %s (Context 0x%" PRIxIN ", IP 0x%" PRIxIN ")",
          CpuCoreId(core), nextThread->Name, nextThread->ContextActive,
          CONTEXT_IP(nextThread->ContextActive));
#endif
    
    // Set next active thread
    if (currentThread != nextThread) {
        CpuCoreSetCurrentThread(core, nextThread);
        ArchThreadEnter(core, nextThread);
    }

    CpuCoreSetInterruptContext(core, nextThread->ContextActive);
    if (pendingFinishHandle != UUID_INVALID) {
        CpuCoreDeferFinish(core, pendingFinishHandle);
    }
    return OS_EOK;
}

// Newly created threads start here on their kernel stack. A user thread creates
// its user contexts before switching modes. Returning from a kernel entry ends
// the thread with exit code zero, using the same path as an explicit ThreadExit.
_Noreturn static void
__ThreadStart(void)
{
    uuid_t    coreId = ArchGetProcessorCoreId();
    Thread_t* thread = ThreadCurrentForCore(coreId);

    TRACE("__ThreadStart(void)");

    if (thread->Flags & THREADING_IDLE) {
        for (;;) {
            ArchProcessorIdle();
        }
    } else if (THREADING_RUNMODE(thread->Flags) == THREADING_KERNELMODE ||
             (thread->Flags & THREADING_KERNELENTRY)) {
        thread->Flags &= ~(THREADING_KERNELENTRY);
        thread->Function(thread->Arguments);
        ThreadExit(0);
    } else {
        __EnterUsermode(thread->Function, thread->Arguments);
    }
    for (;;);
}

void
ThreadingFinish(
    _In_ uuid_t threadId)
{
    Thread_t* thread;
    uuid_t    joinTarget;

    // The scheduler reference keeps the record allocated during this lookup.
    // The caller has already established that no core is using the thread's stack.
    thread = LookupGlobalHandle(threadId, HandleTypeThread);
    assert(thread != NULL);

    // Only a flagged thread may be finished. A thread that is still running
    // or initializing would lose its stack under it.
    assert(atomic_load(&thread->State) == ThreadState_TERMINATING);
    atomic_store(&thread->State, ThreadState_FINISHED);
    FutexWake(&thread->State, INT_MAX, 0);

    // A thread ended while joining another one never reaches ThreadJoin's exit
    // label. Release that wait's reference before releasing this thread's own one.
    joinTarget = atomic_exchange(&thread->JoinTarget, UUID_INVALID);
    if (joinTarget != UUID_INVALID) {
        ReleaseGlobalHandle(joinTarget);
    }
    ReleaseGlobalHandle(threadId);
}

// Use only before the thread has been queued. The partially initialized record
// uses NULL pointers and UUID_INVALID handles for resources that were not created.
static void
__AbortThreadCreation(
    _In_ Thread_t* thread)
{
    // Before registration, cleanup is direct. After registration, the final
    // release removes the ID from lookup and queues __DestroyThread on the janitor.
    if (thread->Handle == UUID_INVALID) {
        __DestroyThread(thread);
    } else {
        ReleaseGlobalHandle(thread->Handle);
    }
}

// Called by the handle janitor after the last reference, or directly for a thread
// whose creation failed before handle registration. No core may still use its
// stacks. Unlike ThreadingFinish, this cleanup may wait for the parent's mutex.
static void
__DestroyThread(
    _In_ void* resource)
{
    Thread_t* thread = resource;
    Thread_t* parent;
    uuid_t    parentId;

    TRACE("__DestroyThread(resource=0x%" PRIxIN ")", resource);

    if (!thread) {
        return;
    }

    // A parent's list protects child pointers with its mutex. Remove this
    // entry before freeing it, without holding the child's mutex. Recursive
    // termination takes locks from parent to child in that same order.
    parentId = atomic_load(&thread->ParentHandle);
    if (AcquireGlobalHandle(parentId, HandleTypeThread, (void**)&parent) == OS_EOK) {
        __RemoveChild(parent, thread);
        ReleaseGlobalHandle(parentId);
    }

    // The scheduler and all temporary users have released their references.
    if (thread->SchedulerObject) {
        SchedulerDestroyObject(thread->SchedulerObject);
    }

    // Destroy the saved contexts and their stacks before releasing the memory
    // space used to locate them. A failed creation may leave some contexts NULL.
    ArchThreadContextDestroy(
        thread->Contexts[THREADING_CONTEXT_LEVEL0],
        THREADING_CONTEXT_LEVEL0,                     
        thread->KernelStackSize
    );
    ArchThreadContextDestroy(
        thread->Contexts[THREADING_CONTEXT_LEVEL1],
        THREADING_CONTEXT_LEVEL1,
        thread->UserStackSize
    );
    ArchThreadContextDestroy(
        thread->Contexts[THREADING_CONTEXT_SIGNAL],
        THREADING_CONTEXT_SIGNAL,
        thread->UserStackSize
    );

    // A valid handle records the thread's independent reference to its memory
    // space. The domain's kernel space is used directly without such a reference.
    if (thread->MemorySpaceHandle != UUID_INVALID) {
        ReleaseGlobalHandle(thread->MemorySpaceHandle);
    }

    kfree((void*)thread->Name);
    kfree(thread->Signaling.Signals);

    ArchThreadDestroy(thread);
    kfree(thread);
}

static oserr_t
__CreateThreadContexts(
    _In_ Thread_t* thread)
{
    oserr_t status = OS_EOK;
    TRACE("__CreateThreadContexts(thread=0x%" PRIxIN ")", thread);

    // Every new thread first runs __ThreadStart on a kernel stack, including
    // threads whose supplied entry function will later run in userspace.
    thread->Contexts[THREADING_CONTEXT_LEVEL0] = ArchThreadContextCreate(
        THREADING_CONTEXT_LEVEL0,
        thread->KernelStackSize
    );
    if (!thread->Contexts[THREADING_CONTEXT_LEVEL0]) {
        status = OS_EOOM;
        goto exit;
    }

    ArchThreadContextReset(
        thread->Contexts[THREADING_CONTEXT_LEVEL0],
        THREADING_CONTEXT_LEVEL0,
        (uintptr_t) &__ThreadStart,
        0
    );

    // We cannot at this time allocate userspace contexts as they need to be located inside
    // the local thread memory. So they are created as a part of thread startup.
exit:
    TRACE("__CreateThreadContexts returns=%u", status);
    return status;
}

// Initialize a fresh record and register its first reference. The caller still
// has to create contexts, select a memory space and mark it ready. On error, pass
// the partial record to __AbortThreadCreation so registered resources are freed.
static oserr_t
__InitializeDefaultsForThread(
    _In_ Thread_t*     thread,
    _In_ const char*   name,
    _In_ ThreadEntry_t threadEntry,
    _In_ void*         arguments,
    _In_ unsigned int  flags,
    _In_ size_t        kernelStackSize,
    _In_ size_t        userStackSize)
{
    oserr_t oserr;
    uuid_t  handle;

    if (!kernelStackSize) {
        kernelStackSize = __GetDefaultStackSize(THREADING_KERNELMODE);
    }
    if (!userStackSize) {
        userStackSize = __GetDefaultStackSize(THREADING_USERMODE);
    }

    // Reset thread structure
    memset(thread, 0, sizeof(Thread_t));
    MutexConstruct(&thread->SyncObject, MUTEX_FLAG_PLAIN);
    thread->Handle = UUID_INVALID;
    thread->ParentHandle = UUID_INVALID;
    thread->JoinTarget = UUID_INVALID;
    thread->State = ThreadState_INITIALIZING;
    thread->MemorySpaceHandle = UUID_INVALID;

    handle = CreateGlobalHandle(HandleTypeThread, __DestroyThread, thread);
    thread->Handle          = handle;
    if (handle == UUID_INVALID) {
        return OS_EOOM;
    }
    thread->Function        = threadEntry;
    thread->Arguments       = arguments;
    thread->Flags           = flags;
    thread->ParentHandle    = UUID_INVALID;
    thread->KernelStackSize = kernelStackSize;
    thread->UserStackSize   = userStackSize;
    SystemTimerGetClockTick(&thread->StartedAt);

    oserr = streambuffer_create(
        sizeof(ThreadSignal_t) * THREADING_MAX_QUEUED_SIGNALS,
        STREAMBUFFER_MULTIPLE_WRITERS | STREAMBUFFER_GLOBAL,
        &thread->Signaling.Signals
    );
    if (oserr != OS_EOK) {
        return OS_EOOM;
    }

    // Sanitize name, if NULL generate a new thread name of format 'thread x'
    if (name == NULL) {
        __SetDefaultName(thread);
        if (thread->Name == NULL) {
            return OS_EOOM;
        }
    } else {
        thread->Name = strdup(name);
        if (thread->Name == NULL) {
            return OS_EOOM;
        }
    }

    thread->SchedulerObject = SchedulerCreateObject(thread, flags);
    if (thread->SchedulerObject == NULL) {
        return OS_EOOM;
    }
    return ArchThreadInitialize(thread);
}

// Build the default cookie from creation ticks. Threads created with inheritance
// copy their parent's cookie instead. The value is not a secret or access check.
static uuid_t
__CreateCookie(
    _In_ Thread_t* thread,
    _In_ Thread_t* parent)
{
    uuid_t cookie = thread->StartedAt.u.LowPart ^ parent->StartedAt.u.LowPart;
    for (int i = 0; i < 5; i++) {
        cookie >>= i;
        cookie += thread->StartedAt.u.LowPart;
        cookie *= parent->StartedAt.u.LowPart;
    }
    return cookie;
}

// All resources must be ready before this call. Only replace INITIALIZING, since
// the parent may have requested termination after the child was added to its list.
// The creator queues a TERMINATING thread too, so the scheduler can finish it.
static void
__MarkThreadReady(
    _In_ Thread_t* thread)
{
    int expected = ThreadState_INITIALIZING;

    (void)atomic_compare_exchange_strong(
        &thread->State,
        &expected,
        ThreadState_RUNNING
    );
}

// __RequestTermination flags a RUNNING or INITIALIZING thread as TERMINATING
// and returns the state it replaced. A thread that is already TERMINATING or
// FINISHED is left alone, and its state is returned. The thread's mutex must be
// held, which leaves only the creator able to race with this by moving the
// thread from INITIALIZING to RUNNING.
static int
__RequestTermination(
    _In_ Thread_t* thread)
{
    int state = atomic_load(&thread->State);

    while (state == ThreadState_RUNNING || state == ThreadState_INITIALIZING) {
        if (atomic_compare_exchange_weak(&thread->State, &state, ThreadState_TERMINATING)) {
            break;
        }
    }
    return state;
}

// ParentHandle must already name this parent. The parent's mutex also protects
// the walk used by termination and prevents cleanup from removing a child midway.
// Adding a child does not add a handle reference or check the parent's exit state.
static void
__AddChild(
    _In_ Thread_t* parent,
    _In_ Thread_t* child)
{
    Thread_t* previous;
    Thread_t* link;

    MutexLock(&parent->SyncObject);
    link = parent->Children;
    if (!link) {
        parent->Children = child;
    }
    else {
        while (link) {
            previous = link;
            link     = link->Sibling;
        }

        previous->Sibling = child;
        child->Sibling    = NULL;
    }
    MutexUnlock(&parent->SyncObject);
}

// Keep the parent referenced while removing the child. Do not hold the child's
// mutex here, since termination takes the parent mutex before the child mutex.
// It is safe for the child to be absent, as it can fail setup before being linked.
static void
__RemoveChild(
    _In_ Thread_t* parent,
    _In_ Thread_t* child)
{
    Thread_t* previous = NULL;
    Thread_t* link;

    MutexLock(&parent->SyncObject);
    link = parent->Children;

    while (link) {
        if (link == child) {
            if (!previous) {
                parent->Children = child->Sibling;
            }
            else {
                previous->Sibling = child->Sibling;
            }
            break;
        }
        previous = link;
        link     = link->Sibling;
    }
    MutexUnlock(&parent->SyncObject);
}

static size_t
__GetDefaultStackSize(
    _In_ unsigned int threadFlags)
{
    if (THREADING_RUNMODE(threadFlags) == THREADING_KERNELMODE) {
        return THREADING_KERNEL_STACK_SIZE;
    }
    else if (THREADING_RUNMODE(threadFlags) == THREADING_USERMODE) {
        return BYTES_PER_MB * 8;
    }
    assert(0);
    return 0;
}
