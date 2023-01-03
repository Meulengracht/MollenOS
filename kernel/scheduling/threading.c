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
 * Threading Interface
 * - Common routines that are platform independant to provide
 *   a flexible and generic threading platfrom
 */

#define __MODULE "thread"
//#define __TRACE
//#define __OSCONFIG_DEBUG_SCHEDULER
#define __need_quantity

#include <arch/thread.h>
#include <arch/utils.h>
#include <assert.h>
#include <component/cpu.h>
#include <component/timer.h>
#include <debug.h>
#include <ds/streambuffer.h>
#include <handle.h>
#include <heap.h>
#include <memoryspace.h>
#include <string.h>
#include <stdio.h>
#include <threading.h>

#include "threading_private.h"

_Noreturn static void __ThreadStart(void);
static void    __DestroyThread(void* resource);
static oserr_t __CreateThreadContexts(Thread_t* thread);
static oserr_t __InitializeDefaultsForThread(Thread_t* thread, const char* name,
                                                 ThreadEntry_t threadEntry, void* arguments,
                                                 unsigned int flags, size_t kernelStackSize,
                                                 size_t userStackSize);
static size_t __GetDefaultStackSize(unsigned int threadFlags);
static uuid_t __CreateCookie(Thread_t* thread, Thread_t* parent);
static void   __AddChild(Thread_t* parent, Thread_t* child);
static void   __RemoveChild(Thread_t* parent, Thread_t* child);

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

    // Handle setup of memory space as that is not covered.
    thread->MemorySpace       = GetCurrentMemorySpace();
    thread->MemorySpaceHandle = GetCurrentMemorySpaceHandle();
    CpuCoreSetCurrentThread(CpuCoreCurrent(), thread);
}

static oserr_t
__InheritAddressSpace(
        _In_ Thread_t* parent,
        _In_ Thread_t* thread)
{
    MemorySpace_t* memorySpace;
    oserr_t        oserr;

    // Idle threads have no memory space handle, just the root (domain) memory
    // space. Instead of acquiring it (which we don't need), we just proxy it
    // and use the same setup
    if (parent->MemorySpaceHandle == UUID_INVALID) {
        thread->MemorySpace       = GetDomainMemorySpace();
        thread->MemorySpaceHandle = UUID_INVALID;
        return OS_EOK;
    }

    oserr = AcquireHandleOfType(
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

    // Set the return code to non-zero, so when the thread resumes from
    // GetContext, we catch this
    CONTEXT_SC_RET0(&returnContext) = 1;

    // Create new thread structure
    thread = (Thread_t*)kmalloc(sizeof(Thread_t));
    if (!thread) {
        return OS_EOOM;
    }
    memset(thread, 0, sizeof(Thread_t));
    MutexConstruct(&thread->SyncObject, MUTEX_FLAG_PLAIN);
    SemaphoreConstruct(&thread->EventObject, 0, 1);
    thread->References = 0;

    // Get current stack pointer, get top of stack, clone it.
    parent = ThreadCurrentForCore(ArchGetProcessorCoreId());
    oserr = ArchThreadContextFork(
            parent->Contexts[THREADING_CONTEXT_LEVEL0],
            &returnContext,
            THREADING_CONTEXT_LEVEL0,
            parent->KernelStackSize,
            &thread->Contexts[THREADING_CONTEXT_LEVEL0],
            &thread->ContextActive
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    // Create new resources for some things
    thread->Handle = CreateHandle(HandleTypeThread, __DestroyThread, thread);
    if (thread->Handle == UUID_INVALID) {
        __DestroyThread(thread);
        return OS_EOOM;
    }

    oserr = streambuffer_create(
            sizeof(ThreadSignal_t) * THREADING_MAX_QUEUED_SIGNALS,
            STREAMBUFFER_MULTIPLE_WRITERS | STREAMBUFFER_GLOBAL,
            &thread->Signaling.Signals
    );
    if (oserr != OS_EOK) {
        __DestroyThread(thread);
        return OS_EOOM;
    }

    __SetDefaultName(thread);
    if (!thread->Name) {
        __DestroyThread(thread);
        return OS_EOOM;
    }

    thread->SchedulerObject = SchedulerCreateObject(thread, parent->Flags | THREADING_FORKED);
    oserr = ArchThreadInitialize(thread);
    if (oserr != OS_EOK) {
        __DestroyThread(thread);
        return oserr;
    }

    // Inherit all the following
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
        __DestroyThread(thread);
        return oserr;
    }

    // Schedule the thread
    __AddChild(parent, thread);
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
    Thread_t* thread;
    Thread_t* parent;
    uuid_t    coreId;
    oserr_t   oserr;

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
        __DestroyThread(thread);
        return oserr;
    }

    oserr = __CreateThreadContexts(thread);
    if (oserr != OS_EOK) {
        __DestroyThread(thread);
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

    // Is a memory space given to us that we should run in? Determine run mode automatically
    if (!(flags & THREADING_INHERIT) && memorySpaceHandle != UUID_INVALID) {
        MemorySpace_t* memorySpace;

        oserr = AcquireHandleOfType(
                memorySpaceHandle,
                HandleTypeMemorySpace,
                (void**)&memorySpace
        );
        if (oserr != OS_EOK) {
            __DestroyThread(thread);
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
            unsigned int memorySpaceFlags = 0;

            if (THREADING_RUNMODE(flags) == THREADING_USERMODE) {
                memorySpaceFlags |= MEMORY_SPACE_APPLICATION;
            }
            if (flags & THREADING_INHERIT) {
                memorySpaceFlags |= MEMORY_SPACE_INHERIT;
            }

            oserr = CreateMemorySpace(memorySpaceFlags, &thread->MemorySpaceHandle);
            if (oserr != OS_EOK) {
                __DestroyThread(thread);
                return OS_EUNKNOWN;
            }
            thread->MemorySpace = MEMORYSPACE_GET(thread->MemorySpaceHandle);
        }
    }

    if (flags & THREADING_INHERIT) {
        __AddChild(parent, thread);
    }

    TRACE("[ThreadCreate] new thread %s on core %u", thread->Name, SchedulerObjectGetAffinity(thread->SchedulerObject));
    SchedulerQueueObject(thread->SchedulerObject);
    *handle = thread->Handle;
    return OS_EOK;
}

oserr_t
ThreadDetach(
        _In_ uuid_t ThreadId)
{
    Thread_t* thread = ThreadCurrentForCore(ArchGetProcessorCoreId());
    Thread_t* target = THREAD_GET(ThreadId);
    oserr_t   oserr = OS_ENOENT;

    if (target == NULL) {
        return oserr;
    }
    
    // Detach is allowed if the caller is the spawner or the caller is in same process
    oserr = AreMemorySpacesRelated(thread->MemorySpace, target->MemorySpace);
    if (target->ParentHandle == thread->Handle || oserr == OS_EOK) {
        Thread_t* Parent = THREAD_GET(target->ParentHandle);
        if (Parent) {
            __RemoveChild(Parent, target);
        }
        target->ParentHandle = UUID_INVALID;
        oserr = OS_EOK;
    }
    return oserr;
}

static oserr_t
__TerminateWithChildren(
        _In_ Thread_t* thread,
        _In_ int       exitCode)
{
    Thread_t* child;
    int       value;

    TRACE("__TerminateWithChildren(%s, %i)", thread->Name, exitCode);

    // Never, ever kill system idle threads
    if (thread->Flags & THREADING_IDLE) {
        return OS_EPERMISSIONS;
    }

    MutexLock(&thread->SyncObject);
    value = atomic_load(&thread->Cleanup);
    if (value) {
        MutexUnlock(&thread->SyncObject);
        return OS_EOK;
    }

    child = thread->Children;
    while (child) {
        oserr_t Status = __TerminateWithChildren(child, exitCode);
        if (Status != OS_EOK) {
            ERROR("__TerminateWithChildren failed to terminate child %s of %s", child->Name, thread->Name);
        }
        child = child->Sibling;
    }

    thread->RetCode = exitCode;
    atomic_store(&thread->Cleanup, 1);
    MutexUnlock(&thread->SyncObject);

    // If the thread we are trying to kill is not this one, and is sleeping
    // we must wake it up, it will be cleaned on next schedule
    if (thread->Handle != ThreadCurrentHandle()) {
        SchedulerExpediteObject(thread->SchedulerObject);
    }
    return OS_EOK;
}

oserr_t
ThreadTerminate(
        _In_ uuid_t ThreadId,
        _In_ int    ExitCode,
        _In_ int    TerminateChildren)
{
    Thread_t* thread = THREAD_GET(ThreadId);
    int       value;
    
    if (!thread) {
        return OS_ENOENT;
    }
    
    // Never, ever kill system idle threads
    if (thread->Flags & THREADING_IDLE) {
        return OS_EPERMISSIONS;
    }
    
    TRACE("ThreadTerminate(%s, %i, %i)", thread->Name, ExitCode, TerminateChildren);
    
    MutexLock(&thread->SyncObject);
    value = atomic_load(&thread->Cleanup);
    if (value) {
        MutexUnlock(&thread->SyncObject);
        return OS_EOK;
    }

    if (thread->ParentHandle != UUID_INVALID) {
        Thread_t* parent = THREAD_GET(thread->ParentHandle);
        if (!parent) {
            // Parent does not exist anymore, it was terminated without terminating children
            // which is very unusal. Log this.
            WARNING("[terminate_thread] orphaned child terminating %s", thread->Name);
        }
        else {
            __RemoveChild(parent, thread);
        }
    }

    if (TerminateChildren) {
        Thread_t* child = thread->Children;
        while (child) {
            oserr_t osStatus = __TerminateWithChildren(child, ExitCode);
            if (osStatus != OS_EOK) {
                ERROR("[terminate_thread] failed to terminate child %s of %s", child->Name, thread->Name);
            }
            child = child->Sibling;
        }
    }
    thread->RetCode = ExitCode;
    atomic_store(&thread->Cleanup, 1);
    MutexUnlock(&thread->SyncObject);

    // If the thread we are trying to kill is not this one, and is sleeping
    // we must wake it up, it will be cleaned on next schedule
    if (ThreadId != ThreadCurrentHandle()) {
        SchedulerExpediteObject(thread->SchedulerObject);
    }
    return OS_EOK;
}

int
ThreadJoin(
        _In_ uuid_t ThreadId)
{
    Thread_t* target = THREAD_GET(ThreadId);
    int       value;
    
    if (target != NULL && target->ParentHandle != UUID_INVALID) {
        MutexLock(&target->SyncObject);
        value = atomic_load(&target->Cleanup);
        if (value == 1) {
            value = target->RetCode;
            MutexUnlock(&target->SyncObject);
            return value;
        }

        value = atomic_fetch_add(&target->References, 1);
        MutexUnlock(&target->SyncObject);

        SemaphoreWait(&target->EventObject, NULL);
        value = atomic_fetch_sub(&target->References, 1);
        return target->RetCode;
    }
    return -1;
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

    // Allocate the TLS segment (1 page) (x86 only, should be another place)
    osStatus = MemorySpaceMap(
            GetCurrentMemorySpace(),
            &tlsAddress,
            &tlsPhysicalAddress,
            GetMemorySpacePageSize(),
            0,
            MAPPING_DOMAIN | MAPPING_USERSPACE | MAPPING_COMMIT,
            MAPPING_VIRTUAL_THREAD
    );
    if (osStatus != OS_EOK) {
        FATAL(FATAL_SCOPE_THREAD, "__EnterUsermode failed to map TLS page");
    }

    // Create the userspace stack(s) now that we will need it
    thread->Contexts[THREADING_CONTEXT_LEVEL1] = ArchThreadContextCreate(THREADING_CONTEXT_LEVEL1,
                                                                         thread->UserStackSize);
    thread->Contexts[THREADING_CONTEXT_SIGNAL] = ArchThreadContextCreate(THREADING_CONTEXT_SIGNAL,
                                                                         thread->UserStackSize);
    if (!thread->Contexts[THREADING_CONTEXT_LEVEL1] || !thread->Contexts[THREADING_CONTEXT_SIGNAL]) {
        assert(0);
    }

    // initiate the userspace stack with entry point and argument
    ArchThreadContextReset(
            thread->Contexts[THREADING_CONTEXT_LEVEL1], THREADING_CONTEXT_LEVEL1,
            (uintptr_t) threadEntry, (uintptr_t) argument);

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
        _In_ uuid_t Thread1,
        _In_ uuid_t Thread2)
{
    Thread_t* First  = THREAD_GET(Thread1);
    Thread_t* Second = THREAD_GET(Thread2);
    if (First == NULL || Second == NULL) {
        return OS_ENOENT;
    }
    return AreMemorySpacesRelated(First->MemorySpace, Second->MemorySpace);
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
    int              signalsPending;
    int              cleanup;

    // Is threading disabled?
    if (!currentThread) {
        return OS_EUNKNOWN;
    }

    cleanup = atomic_load(&currentThread->Cleanup);
    currentThread->ContextActive = CpuCoreInterruptContext(core);
    
    // Perform pre-liminary actions only if we are not going to clean up and destroy the thread
    if (!cleanup) {
        ArchThreadLeave(currentThread);
        
        // Handle any received signals during runtime in system calls, this must be handled
        // here after any blocking operations has been queued, so we can cancel it.
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
    if ((currentThread->Flags & THREADING_IDLE) || cleanup == 1) {
        // If the thread is finished then add it to garbagecollector
        if (cleanup == 1) {
            DestroyHandle(currentThread->Handle);
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
    
    // Sanitize if we need to activate our idle thread, otherwise
    // do a final check that we haven't just gotten ahold of a thread
    // marked for finish
    if (nextThread == NULL) {
#ifdef __OSCONFIG_DEBUG_SCHEDULER
        TRACE("[threading] [switch] selecting idle");
#endif
        nextThread = CpuCoreIdleThread(core);
    }
    else {
        cleanup = atomic_load(&nextThread->Cleanup);
        if (cleanup == 1) {
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
    return OS_EOK;
}

// Common entry point for everything
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
    }
    else if (THREADING_RUNMODE(thread->Flags) == THREADING_KERNELMODE ||
             (thread->Flags & THREADING_KERNELENTRY)) {
        thread->Flags &= ~(THREADING_KERNELENTRY);
        thread->Function(thread->Arguments);
        ThreadTerminate(thread->Handle, 0, 1);
    }
    else {
        __EnterUsermode(thread->Function, thread->Arguments);
    }
    for (;;);
}

static void
__WaitForReferences(
        _In_ Thread_t* thread)
{
    int           references;
    OSTimestamp_t wakeUp;

    references = atomic_load(&thread->References);
    if (references != 0) {
        int timeout = 200;
        SemaphoreSignal(&thread->EventObject, references + 1);
        SystemTimerGetWallClockTime(&wakeUp);
        while (timeout > 0) {
            OSTimestampAddNsec(&wakeUp, &wakeUp, 10 * NSEC_PER_MSEC);
            timeout -= 10;

            references = atomic_load(&thread->References);
            if (!references) {
                break;
            }
        }
    }
}

static void
__DestroyThread(
        _In_ void* resource)
{
    Thread_t* thread = resource;

    TRACE("__DestroyThread(resource=0x%" PRIxIN ")", resource);

    if (!thread) {
        return;
    }

    // Make sure we are completely removed as reference
    // from the entire system. We also signal all waiters for this
    // thread again before continueing just in case
    __WaitForReferences(thread);

    // Cleanup resources now that we have no external dependancies
    if (thread->SchedulerObject) {
        SchedulerDestroyObject(thread->SchedulerObject);
    }

    // Detroy the thread-contexts
    ArchThreadContextDestroy(thread->Contexts[THREADING_CONTEXT_LEVEL0], THREADING_CONTEXT_LEVEL0,
                             thread->KernelStackSize);
    ArchThreadContextDestroy(thread->Contexts[THREADING_CONTEXT_LEVEL1], THREADING_CONTEXT_LEVEL1,
                             thread->UserStackSize);
    ArchThreadContextDestroy(thread->Contexts[THREADING_CONTEXT_SIGNAL], THREADING_CONTEXT_SIGNAL,
                             thread->UserStackSize);

    // Remove a reference to the memory space if not root, and remove the
    // kernel mapping of the threads' ipc area
    if (thread->MemorySpaceHandle != UUID_INVALID) {
        DestroyHandle(thread->MemorySpaceHandle);
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

    // Create the kernel context, for an userspace thread this is always the default
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

// Setup defaults for a new thread and creates appropriate resources
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
    SemaphoreConstruct(&thread->EventObject, 0, 1);

    handle = CreateHandle(HandleTypeThread, __DestroyThread, thread);
    thread->Handle          = handle;
    thread->References      = 0;
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
            &thread->Signaling.Signals);
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
    }

    thread->SchedulerObject = SchedulerCreateObject(thread, flags);
    return ArchThreadInitialize(thread);
}

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
