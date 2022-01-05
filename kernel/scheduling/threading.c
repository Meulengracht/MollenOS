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

_Noreturn static void ThreadingEntryPoint(void);
static void           DestroyThread(void* resource);
static OsStatus_t     CreateDefaultThreadContexts(Thread_t* thread);
static OsStatus_t     InitializeDefaultThread(Thread_t* thread, const char* name,
                                              ThreadEntry_t threadEntry, void* arguments,
                                              unsigned int flags, size_t kernelStackSize,
                                              size_t userStackSize);
static size_t         GetDefaultStackSize(unsigned int threadFlags);
static UUId_t         CreateThreadCookie(Thread_t* Thread, Thread_t* Parent);
static void           AddChild(Thread_t* Parent, Thread_t* Child);
static void           RemoveChild(Thread_t* Parent, Thread_t* Child);

void
ThreadingEnable(void)
{
    Thread_t* thread = CpuCoreIdleThread(CpuCoreCurrent());
    InitializeDefaultThread(thread, "idle", NULL, NULL,
        THREADING_KERNELMODE | THREADING_IDLE, THREADING_KERNEL_STACK_SIZE, 0);
    
    // Handle setup of memory space as that is not covered.
    thread->MemorySpace       = GetCurrentMemorySpace();
    thread->MemorySpaceHandle = GetCurrentMemorySpaceHandle();
    CpuCoreSetCurrentThread(CpuCoreCurrent(), thread);
}

OsStatus_t
ThreadCreate(
        _In_ const char*   name,
        _In_ ThreadEntry_t entry,
        _In_ void*         arguments,
        _In_ unsigned int  flags,
        _In_ UUId_t        memorySpaceHandle,
        _In_ size_t        kernelMaxStackSize,
        _In_ size_t        userMaxStackSize,
        _In_ UUId_t*       handle)
{
    Thread_t* thread;
    Thread_t* parent;
    UUId_t    coreId;

    TRACE("ThreadCreate(name=%s, entry=0x%" PRIxIN ", argments=0x%" PRIxIN ", flags=0x%x, memorySpaceHandle=%u"
          "kernelMaxStackSize=%" PRIuIN ", userMaxStackSize=%" PRIuIN ")",
          name, entry, arguments, flags, memorySpaceHandle, kernelMaxStackSize, userMaxStackSize);

    coreId = ArchGetProcessorCoreId();
    parent = ThreadCurrentForCore(coreId);

    thread = (Thread_t*)kmalloc(sizeof(Thread_t));
    if (!thread) {
        return OsOutOfMemory;
    }

    OsStatus_t status = InitializeDefaultThread(thread, name, entry, arguments,
                                                flags, kernelMaxStackSize, userMaxStackSize);
    if (status != OsSuccess) {
        DestroyThread(thread);
        return status;
    }
    
    // Setup parent and cookie information
    thread->ParentHandle = parent->Handle;
    if (flags & THREADING_INHERIT) {
        thread->Cookie = parent->Cookie;
    }
    else {
        thread->Cookie = CreateThreadCookie(thread, parent);
    }

    // Is a memory space given to us that we should run in? Determine run mode automatically
    if (!(flags & THREADING_INHERIT) && memorySpaceHandle != UUID_INVALID) {
        MemorySpace_t* memorySpace;

        status = AcquireHandle(memorySpaceHandle, (void**)&memorySpace);
        if (status != OsSuccess) {
            DestroyThread(thread);
            return status;
        }

        if (memorySpace->Flags & MEMORY_SPACE_APPLICATION) {
            thread->Flags |= THREADING_USERMODE;
        }
        thread->MemorySpace        = memorySpace;
        thread->MemorySpaceHandle  = memorySpaceHandle;
    }
    else {
        if (THREADING_RUNMODE(flags) == THREADING_KERNELMODE) {
            thread->MemorySpace       = GetDomainMemorySpace();
            thread->MemorySpaceHandle = UUID_INVALID;
        }
        else {
            unsigned int memorySpaceFlags = 0;

            if (THREADING_RUNMODE(flags) == THREADING_USERMODE) {
                memorySpaceFlags |= MEMORY_SPACE_APPLICATION;
            }
            if (flags & THREADING_INHERIT) {
                memorySpaceFlags |= MEMORY_SPACE_INHERIT;
            }

            status = CreateMemorySpace(memorySpaceFlags, &thread->MemorySpaceHandle);
            if (status != OsSuccess) {
                DestroyThread(thread);
                return OsError;
            }
            thread->MemorySpace = MEMORYSPACE_GET(thread->MemorySpaceHandle);
        }
    }

    if (flags & THREADING_INHERIT) {
        AddChild(parent, thread);
    }

    TRACE("[ThreadCreate] new thread %s on core %u", thread->Name, SchedulerObjectGetAffinity(thread->SchedulerObject));
    SchedulerQueueObject(thread->SchedulerObject);
    *handle = thread->Handle;
    return OsSuccess;
}

OsStatus_t
ThreadDetach(
    _In_ UUId_t ThreadId)
{
    Thread_t*  Thread = ThreadCurrentForCore(ArchGetProcessorCoreId());
    Thread_t*  Target = THREAD_GET(ThreadId);
    OsStatus_t Status = OsDoesNotExist;
    
    // Detach is allowed if the caller is the spawner or the caller is in same process
    if (Target != NULL) {
        Status = AreMemorySpacesRelated(Thread->MemorySpace, Target->MemorySpace);
        if (Target->ParentHandle == Thread->Handle || Status == OsSuccess) {
            Thread_t* Parent = THREAD_GET(Target->ParentHandle);
            if (Parent) {
                RemoveChild(Parent, Target);
            }
            Target->ParentHandle = UUID_INVALID;
            Status               = OsSuccess;
        }
    }
    return Status;
}

static OsStatus_t
TerminateThreadWithChildren(
        _In_ Thread_t* thread,
        _In_ int       exitCode)
{
    Thread_t* child;
    int       value;

    TRACE("TerminateThreadWithChildren(%s, %i)", thread->Name, exitCode);

    // Never, ever kill system idle threads
    if (thread->Flags & THREADING_IDLE) {
        return OsInvalidPermissions;
    }

    MutexLock(&thread->SyncObject);
    value = atomic_load(&thread->Cleanup);
    if (value) {
        MutexUnlock(&thread->SyncObject);
        return OsSuccess;
    }

    child = thread->Children;
    while (child) {
        OsStatus_t Status = TerminateThreadWithChildren(child, exitCode);
        if (Status != OsSuccess) {
            ERROR("TerminateThreadWithChildren failed to terminate child %s of %s", child->Name, thread->Name);
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
    return OsSuccess;
}

OsStatus_t
ThreadTerminate(
    _In_ UUId_t ThreadId,
    _In_ int    ExitCode,
    _In_ int    TerminateChildren)
{
    Thread_t* thread = THREAD_GET(ThreadId);
    int       value;
    
    if (!thread) {
        return OsDoesNotExist;
    }
    
    // Never, ever kill system idle threads
    if (thread->Flags & THREADING_IDLE) {
        return OsInvalidPermissions;
    }
    
    TRACE("ThreadTerminate(%s, %i, %i)", thread->Name, ExitCode, TerminateChildren);
    
    MutexLock(&thread->SyncObject);
    value = atomic_load(&thread->Cleanup);
    if (value) {
        MutexUnlock(&thread->SyncObject);
        return OsSuccess;
    }

    if (thread->ParentHandle != UUID_INVALID) {
        Thread_t* parent = THREAD_GET(thread->ParentHandle);
        if (!parent) {
            // Parent does not exist anymore, it was terminated without terminating children
            // which is very unusal. Log this.
            WARNING("[terminate_thread] orphaned child terminating %s", thread->Name);
        }
        else {
            RemoveChild(parent, thread);
        }
    }

    if (TerminateChildren) {
        Thread_t* child = thread->Children;
        while (child) {
            OsStatus_t osStatus = TerminateThreadWithChildren(child, ExitCode);
            if (osStatus != OsSuccess) {
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
    return OsSuccess;
}

int
ThreadJoin(
    _In_ UUId_t ThreadId)
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

        SemaphoreWait(&target->EventObject, 0);
        value = atomic_fetch_sub(&target->References, 1);
        return target->RetCode;
    }
    return -1;
}

_Noreturn void
ThreadingEnterUsermode(
    _In_ ThreadEntry_t EntryPoint,
    _In_ void*         Argument)
{
    Thread_t* thread = ThreadCurrentForCore(ArchGetProcessorCoreId());
    vaddr_t   tlsAddress;
    paddr_t   tlsPhysicalAddress;

    // Allocate the TLS segment (1 page) (x86 only, should be another place)
    MemorySpaceMap(
            GetCurrentMemorySpace(),
            &tlsAddress,
            &tlsPhysicalAddress,
            GetMemorySpacePageSize(),
            0,
            MAPPING_DOMAIN | MAPPING_USERSPACE | MAPPING_COMMIT,
            MAPPING_VIRTUAL_THREAD
    );

    // Create the userspace stack(s) now that we will need it
    thread->Contexts[THREADING_CONTEXT_LEVEL1] = ContextCreate(THREADING_CONTEXT_LEVEL1, thread->UserStackSize);
    thread->Contexts[THREADING_CONTEXT_SIGNAL] = ContextCreate(THREADING_CONTEXT_SIGNAL, thread->UserStackSize);
    if (!thread->Contexts[THREADING_CONTEXT_LEVEL1] || !thread->Contexts[THREADING_CONTEXT_SIGNAL]) {
        assert(0);
    }

    // initiate the userspace stack with entry point and argument
    ContextReset(
            thread->Contexts[THREADING_CONTEXT_LEVEL1], THREADING_CONTEXT_LEVEL1,
            (uintptr_t)EntryPoint, (uintptr_t)Argument);

    // initiate switch to userspace
    thread->Flags |= THREADING_TRANSITION_USERMODE;
    ThreadingYield();
    for (;;);
}

Thread_t*
ThreadCurrentForCore(
    _In_ UUId_t CoreId)
{
    return CpuCoreCurrentThread(GetProcessorCore(CoreId));
}

UUId_t
ThreadCurrentHandle(void)
{
    Thread_t* thread = CpuCoreCurrentThread(CpuCoreCurrent());
    if (!thread) {
        return UUID_INVALID;
    }

    return thread->Handle;
}

OsStatus_t
ThreadIsRelated(
    _In_ UUId_t Thread1,
    _In_ UUId_t Thread2)
{
    Thread_t* First  = THREAD_GET(Thread1);
    Thread_t* Second = THREAD_GET(Thread2);
    if (First == NULL || Second == NULL) {
        return OsDoesNotExist;
    }
    return AreMemorySpacesRelated(First->MemorySpace, Second->MemorySpace);
}

int
ThreadIsCurrentIdle(
    _In_ UUId_t CoreId)
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

int
ThreadIsRoot(
        _In_ Thread_t* Thread)
{
    if (!Thread) {
        return 0;
    }
    return Thread->ParentHandle == UUID_INVALID;
}

UUId_t
ThreadHandle(
        _In_ Thread_t* Thread)
{
    if (!Thread) {
        return UUID_INVALID;
    }
    return Thread->Handle;
}

clock_t
ThreadStartTime(
        _In_ Thread_t* Thread)
{
    if (!Thread) {
        return 0;
    }
    return Thread->StartedAt;
}

UUId_t
ThreadCookie(
        _In_ Thread_t* Thread)
{
    if (!Thread) {
        return 0;
    }
    return Thread->Cookie;
}

OsStatus_t
ThreadSetName(
        _In_ Thread_t*   Thread,
        _In_ const char* Name)
{
    const char* previousName;
    char*       nameCopy;

    if (!Thread || !Name) {
        return OsInvalidParameters;
    }

    previousName = Thread->Name;
    nameCopy = strdup(Name);
    if (!nameCopy) {
        return OsOutOfMemory;
    }

    Thread->Name = (const char*)nameCopy;
    kfree((void*)previousName);
    return OsSuccess;
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

MemorySpace_t*
ThreadMemorySpace(
        _In_ Thread_t* Thread)
{
    if (!Thread) {
        return NULL;
    }
    return Thread->MemorySpace;
}

UUId_t
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

uintptr_t*
ThreadData(
        _In_ Thread_t* Thread)
{
    if (!Thread) {
        return NULL;
    }
    return &Thread->Data[0];
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

OsStatus_t
ThreadingAdvance(
    _In_  int     preemptive,
    _In_  size_t  nanosecondsPassed,
    _Out_ size_t* nextDeadlineOut)
{
    SystemCpuCore_t* core          = CpuCoreCurrent();
    Thread_t*        currentThread = CpuCoreCurrentThread(core);
    Thread_t*        nextThread;
    int              signalsPending;
    int              cleanup;

    // Is threading disabled?
    if (!currentThread) {
        return OsError;
    }

    cleanup = atomic_load(&currentThread->Cleanup);
    currentThread->ContextActive = CpuCoreInterruptContext(core);
    
    // Perform pre-liminary actions only if we are not going to clean up and destroy the thread
    if (!cleanup) {
        SaveThreadState(currentThread);
        
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
        RestoreThreadState(nextThread);
    }

    CpuCoreSetInterruptContext(core, nextThread->ContextActive);
    return OsSuccess;
}

// Common entry point for everything
_Noreturn static void ThreadingEntryPoint(void)
{
    UUId_t    coreId = ArchGetProcessorCoreId();
    Thread_t* thread = ThreadCurrentForCore(coreId);

    TRACE("ThreadingEntryPoint(void)");

    if (thread->Flags & THREADING_IDLE) {
        while (1) {
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
        ThreadingEnterUsermode(thread->Function, thread->Arguments);
    }
    for (;;);
}

static void
DestroyThread(
        _In_ void* resource)
{
    Thread_t* thread = resource;
    int       references;
    clock_t   unused;

    TRACE("DestroyThread(resource=0x%" PRIxIN ")", resource);

    if (!thread) {
        return;
    }

    // Make sure we are completely removed as reference
    // from the entire system. We also signal all waiters for this
    // thread again before continueing just in case
    references = atomic_load(&thread->References);
    if (references != 0) {
        int Timeout = 200;
        SemaphoreSignal(&thread->EventObject, references + 1);
        while (Timeout > 0) {
            SchedulerSleep(10 * NSEC_PER_MSEC, &unused);
            Timeout -= 10;

            references = atomic_load(&thread->References);
            if (!references) {
                break;
            }
        }
    }

    // Cleanup resources now that we have no external dependancies
    if (thread->SchedulerObject) {
        SchedulerDestroyObject(thread->SchedulerObject);
    }

    // Detroy the thread-contexts
    ContextDestroy(thread->Contexts[THREADING_CONTEXT_LEVEL0], THREADING_CONTEXT_LEVEL0, thread->KernelStackSize);
    ContextDestroy(thread->Contexts[THREADING_CONTEXT_LEVEL1], THREADING_CONTEXT_LEVEL1, thread->UserStackSize);
    ContextDestroy(thread->Contexts[THREADING_CONTEXT_SIGNAL], THREADING_CONTEXT_SIGNAL, thread->UserStackSize);

    // Remove a reference to the memory space if not root, and remove the
    // kernel mapping of the threads' ipc area
    if (thread->MemorySpaceHandle != UUID_INVALID) {
        DestroyHandle(thread->MemorySpaceHandle);
    }

    if (thread->Name) {
        kfree((void*)thread->Name);
    }

    if (thread->Signaling.Signals) {
        kfree(thread->Signaling.Signals);
    }

    ThreadingUnregister(thread);
    kfree(thread);
}

static OsStatus_t
CreateDefaultThreadContexts(
        _In_ Thread_t* thread)
{
    OsStatus_t status = OsSuccess;
    TRACE("CreateDefaultThreadContexts(thread=0x%" PRIxIN ")", thread);

    // Create the kernel context, for an userspace thread this is always the default
    thread->Contexts[THREADING_CONTEXT_LEVEL0] = ContextCreate(THREADING_CONTEXT_LEVEL0, thread->KernelStackSize);
    if (!thread->Contexts[THREADING_CONTEXT_LEVEL0]) {
        status = OsOutOfMemory;
        goto exit;
    }

    ContextReset(
            thread->Contexts[THREADING_CONTEXT_LEVEL0], THREADING_CONTEXT_LEVEL0,
            (uintptr_t)&ThreadingEntryPoint, 0);

    // We cannot at this time allocate userspace contexts as they need to be located inside
    // the local thread memory. So they are created as a part of thread startup.'
exit:
    TRACE("CreateDefaultThreadContexts returns=%u", status);
    return status;
}

// Setup defaults for a new thread and creates appropriate resources
static OsStatus_t
InitializeDefaultThread(
        _In_ Thread_t*     thread,
        _In_ const char*   name,
        _In_ ThreadEntry_t threadEntry,
        _In_ void*         arguments,
        _In_ unsigned int  flags,
        _In_ size_t        kernelStackSize,
        _In_ size_t        userStackSize)
{
    OsStatus_t osStatus;
    UUId_t     handle;
    char       buffer[16];

    if (!kernelStackSize) {
        kernelStackSize = GetDefaultStackSize(THREADING_KERNELMODE);
    }
    if (!userStackSize) {
        userStackSize = GetDefaultStackSize(THREADING_USERMODE);
    }

    // Reset thread structure
    memset(thread, 0, sizeof(Thread_t));
    MutexConstruct(&thread->SyncObject, MUTEX_FLAG_PLAIN);
    SemaphoreConstruct(&thread->EventObject, 0, 1);

    handle = CreateHandle(HandleTypeThread, DestroyThread, thread);
    thread->Handle          = handle;
    thread->References      = ATOMIC_VAR_INIT(0);
    thread->Function        = threadEntry;
    thread->Arguments       = arguments;
    thread->Flags           = flags;
    thread->ParentHandle    = UUID_INVALID;
    thread->KernelStackSize = kernelStackSize;
    thread->UserStackSize   = userStackSize;
    SystemTimerGetTimestamp(&thread->StartedAt);

    osStatus = streambuffer_create(
            sizeof(ThreadSignal_t) * THREADING_MAX_QUEUED_SIGNALS,
            STREAMBUFFER_MULTIPLE_WRITERS | STREAMBUFFER_GLOBAL,
            &thread->Signaling.Signals);
    if (osStatus != OsSuccess) {
        return OsOutOfMemory;
    }

    // Sanitize name, if NULL generate a new thread name of format 'thread x'
    if (name == NULL) {
        memset(&buffer[0], 0, sizeof(buffer));
        sprintf(&buffer[0], "thread %" PRIuIN, thread->Handle);
        thread->Name = strdup(&buffer[0]);
    }
    else {
        thread->Name = strdup(name);
    }

    if (!thread->Name) {
        return OsOutOfMemory;
    }

    thread->SchedulerObject = SchedulerCreateObject(thread, flags);
    osStatus = ThreadingRegister(thread);
    if (osStatus != OsSuccess) {
        return osStatus;
    }

    return CreateDefaultThreadContexts(thread);
}

static UUId_t
CreateThreadCookie(
        _In_ Thread_t* Thread,
        _In_ Thread_t* Parent)
{
    UUId_t Cookie = Thread->StartedAt ^ Parent->StartedAt;
    for (int i = 0; i < 5; i++) {
        Cookie >>= i;
        Cookie += Thread->StartedAt;
        Cookie *= Parent->StartedAt;
    }
    return Cookie;
}

static void AddChild(
        _In_ Thread_t* Parent,
        _In_ Thread_t* Child)
{
    Thread_t* Previous;
    Thread_t* Link;

    MutexLock(&Parent->SyncObject);
    Link = Parent->Children;
    if (!Link) {
        Parent->Children = Child;
    }
    else {
        while (Link) {
            Previous = Link;
            Link     = Link->Sibling;
        }

        Previous->Sibling = Child;
        Child->Sibling    = NULL;
    }
    MutexUnlock(&Parent->SyncObject);
}

static void RemoveChild(
        _In_ Thread_t* Parent,
        _In_ Thread_t* Child)
{
    Thread_t* Previous = NULL;
    Thread_t* Link;

    MutexLock(&Parent->SyncObject);
    Link = Parent->Children;

    while (Link) {
        if (Link == Child) {
            if (!Previous) {
                Parent->Children = Child->Sibling;
            }
            else {
                Previous->Sibling = Child->Sibling;
            }
            break;
        }
        Previous = Link;
        Link     = Link->Sibling;
    }
    MutexUnlock(&Parent->SyncObject);
}

static size_t GetDefaultStackSize(unsigned int threadFlags)
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
