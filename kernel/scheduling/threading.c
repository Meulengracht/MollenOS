/**
 * MollenOS
 *
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Threading Interface
 * - Common routines that are platform independant to provide
 *   a flexible and generic threading platfrom
 */

#define __MODULE "thread"
//#define __TRACE

#include <arch/thread.h>
#include <arch/utils.h>
#include <assert.h>
#include <component/cpu.h>
#include <debug.h>
#include <ds/streambuffer.h>
#include <handle.h>
#include <heap.h>
#include <machine.h>
#include <memoryspace.h>
#include <string.h>
#include <stdio.h>
#include <threading.h>
#include <timers.h>

#include "threading_private.h"

// Common entry point for everything
static void
ThreadingEntryPoint(void)
{
    Thread_t* thread;
    UUId_t    coreId;

    TRACE("ThreadingEntryPoint()");

    coreId = ArchGetProcessorCoreId();
    thread = ThreadCurrentForCore(coreId);
    
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
    _In_ void* Resource)
{
    Thread_t* Thread = Resource;
    int            References;
    clock_t        Unused;

    assert(Thread != NULL);
    assert(atomic_load_explicit(&Thread->Cleanup, memory_order_relaxed) == 1);
    TRACE("DestroyThread(%s)", Thread->Name);

    // Make sure we are completely removed as reference
    // from the entire system. We also signal all waiters for this
    // thread again before continueing just in case
    References = atomic_load(&Thread->References);
    if (References != 0) {
        int Timeout = 200;
        SemaphoreSignal(&Thread->EventObject, References + 1);
        while (Timeout > 0) {
            SchedulerSleep(10, &Unused);
            Timeout -= 10;
            
            References = atomic_load(&Thread->References);
            if (!References) {
                break;
            }
        }
    }
    
    // Cleanup resources now that we have no external dependancies
    SchedulerDestroyObject(Thread->SchedulerObject);
    ThreadingUnregister(Thread);
    
    // Detroy the thread-contexts
    ContextDestroy(Thread->Contexts[THREADING_CONTEXT_LEVEL0], THREADING_CONTEXT_LEVEL0, THREADING_KERNEL_STACK_SIZE);    
    ContextDestroy(Thread->Contexts[THREADING_CONTEXT_LEVEL1], THREADING_CONTEXT_LEVEL1, GetMemorySpacePageSize());
    ContextDestroy(Thread->Contexts[THREADING_CONTEXT_SIGNAL], THREADING_CONTEXT_SIGNAL, GetMemorySpacePageSize());

    // Remove a reference to the memory space if not root, and remove the
    // kernel mapping of the threads' ipc area
    if (Thread->MemorySpaceHandle != UUID_INVALID) {
        DestroyHandle(Thread->MemorySpaceHandle);
    }
    
    kfree((void*)Thread->Name);
    kfree(Thread);
}

static void
CreateDefaultThreadContexts(
    _In_ Thread_t* Thread)
{
    Thread->Contexts[THREADING_CONTEXT_LEVEL0] = ContextCreate(THREADING_CONTEXT_LEVEL0,
        THREADING_KERNEL_STACK_SIZE);
    assert(Thread->Contexts[THREADING_CONTEXT_LEVEL0] != NULL);
    
    ContextReset(Thread->Contexts[THREADING_CONTEXT_LEVEL0], THREADING_CONTEXT_LEVEL0, 
        (uintptr_t)&ThreadingEntryPoint, 0);

    // TODO: Should we create user stacks immediately?
}

// Setup defaults for a new thread and creates appropriate resources
static void
InitializeDefaultThread(
    _In_ Thread_t* Thread,
    _In_ const char*    Name,
    _In_ ThreadEntry_t  Function,
    _In_ void*          Arguments,
    _In_ unsigned int        Flags)
{
    OsStatus_t Status;
    UUId_t     Handle;
    char       NameBuffer[16];

    // Reset thread structure
    memset(Thread, 0, sizeof(Thread_t));
    MutexConstruct(&Thread->SyncObject, MUTEX_PLAIN);
    SemaphoreConstruct(&Thread->EventObject, 0, 1);
    
    Handle = CreateHandle(HandleTypeThread, DestroyThread, Thread);
    Thread->Handle       = Handle;
    Thread->References   = ATOMIC_VAR_INIT(0);
    Thread->Function     = Function;
    Thread->Arguments    = Arguments;
    Thread->Flags        = Flags;
    Thread->ParentHandle = UUID_INVALID;
    
    Status = streambuffer_create(sizeof(ThreadSignal_t) * THREADING_MAX_QUEUED_SIGNALS,
        STREAMBUFFER_MULTIPLE_WRITERS | STREAMBUFFER_GLOBAL,
        &Thread->Signaling.Signals);
    if (Status != OsSuccess) {
        FATAL(FATAL_SCOPE_KERNEL, "Failed to allocate space for signal buffer");
    }
    
    // Get the startup time
    TimersGetSystemTick(&Thread->StartedAt);
    
    // Sanitize name, if NULL generate a new thread name of format 'thread x'
    if (Name == NULL) {
        memset(&NameBuffer[0], 0, sizeof(NameBuffer));
        sprintf(&NameBuffer[0], "thread %" PRIuIN, Thread->Handle);
        Thread->Name = strdup(&NameBuffer[0]);
    }
    else {
        Thread->Name = strdup(Name);
    }
    
    Thread->SchedulerObject = SchedulerCreateObject(Thread, Flags);
    Status = ThreadingRegister(Thread);
    if (Status != OsSuccess) {
        FATAL(FATAL_SCOPE_KERNEL, "Failed to register a new thread with system.");
    }
    
    CreateDefaultThreadContexts(Thread);
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

void
ThreadingEnable(void)
{
    Thread_t* thread = CpuCoreIdleThread(CpuCoreCurrent());
    InitializeDefaultThread(thread, "idle", NULL, NULL,
        THREADING_KERNELMODE | THREADING_IDLE);
    
    // Handle setup of memory space as that is not covered.
    thread->MemorySpace       = GetCurrentMemorySpace();
    thread->MemorySpaceHandle = GetCurrentMemorySpaceHandle();
    CpuCoreSetCurrentThread(CpuCoreCurrent(), thread);
}

OsStatus_t
ThreadCreate(
    _In_  const char*    name,
    _In_  ThreadEntry_t  entry,
    _In_  void*          arguments,
    _In_  unsigned int   flags,
    _In_  UUId_t         memorySpaceHandle,
    _Out_ UUId_t*        handle)
{
    Thread_t* thread;
    Thread_t* parent;
    UUId_t    coreId;

    TRACE("ThreadCreate(%s, 0x%" PRIxIN ")", name, flags);

    coreId = ArchGetProcessorCoreId();
    parent = ThreadCurrentForCore(coreId);

    thread = (Thread_t*)kmalloc(sizeof(Thread_t));
    if (!thread) {
        return OsOutOfMemory;
    }
    
    InitializeDefaultThread(thread, name, entry, arguments, flags);
    
    // Setup parent and cookie information
    thread->ParentHandle = parent->Handle;
    if (flags & THREADING_INHERIT) {
        thread->Cookie = parent->Cookie;
    }
    else {
        thread->Cookie = CreateThreadCookie(thread, parent);
    }

    // Is a memory space given to us that we should run in? Determine run mode automatically
    if (memorySpaceHandle != UUID_INVALID) {
        MemorySpace_t* memorySpace = (MemorySpace_t*)LookupHandleOfType(memorySpaceHandle, HandleTypeMemorySpace);
        if (memorySpace == NULL) {
            // TODO: cleanup
            return OsDoesNotExist;
        }

        if (memorySpace->Flags & MEMORY_SPACE_APPLICATION) {
            thread->Flags |= THREADING_USERMODE;
        }
        thread->MemorySpace        = memorySpace;
        thread->MemorySpaceHandle  = memorySpaceHandle;
        thread->Cookie             = parent->Cookie;
        AcquireHandle(memorySpaceHandle, NULL);
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
            if (CreateMemorySpace(memorySpaceFlags, &thread->MemorySpaceHandle) != OsSuccess) {
                ERROR("Failed to create memory space for thread");
                // TODO: cleanup
                return OsError;
            }
            thread->MemorySpace = (MemorySpace_t*)LookupHandleOfType(
                    thread->MemorySpaceHandle, HandleTypeMemorySpace);
        }
    }
    
    // Create pre-mapped tls region for userspace threads, the area will be reserved
    // but not physically allocated
    if (THREADING_RUNMODE(flags) == THREADING_USERMODE) {
        uintptr_t  threadRegionStart = GetMachine()->MemoryMap.ThreadRegion.Start;
        size_t     threadRegionSize  = GetMachine()->MemoryMap.ThreadRegion.Length;
        OsStatus_t status            = MemorySpaceMapReserved(
                thread->MemorySpace,
                &threadRegionStart, threadRegionSize,
                MAPPING_DOMAIN | MAPPING_USERSPACE, MAPPING_VIRTUAL_FIXED);
        if (status != OsSuccess) {
            ERROR("[thread_create] failed to premap TLS area");
        }
    }
    AddChild(parent, thread);

    WARNING("[thread_create] new thread %s on core %u",
            thread->Name, SchedulerObjectGetAffinity(thread->SchedulerObject));
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
    if (value == 0) {
        if (thread->ParentHandle != UUID_INVALID) {
            Thread_t* Parent = THREAD_GET(thread->ParentHandle);
            if (!Parent) {
                // Parent does not exist anymore, it was terminated without terminating children
                // which is very unusal. Log this. 
                WARNING("[terminate_thread] orphaned child terminating %s", thread->Name);
            }
            else {
                RemoveChild(Parent, thread);
            }
        }
        
        if (TerminateChildren) {
            Thread_t* ChildItr = thread->Children;
            while (ChildItr) {
                OsStatus_t Status = ThreadTerminate(ChildItr->Handle, ExitCode, TerminateChildren);
                if (Status != OsSuccess) {
                    ERROR("[terminate_thread] failed to terminate child %s of %s", ChildItr->Name, thread->Name);
                }
                ChildItr = ChildItr->Sibling;
            }
        }
        thread->RetCode = ExitCode;
        atomic_store(&thread->Cleanup, 1);
    
        // If the thread we are trying to kill is not this one, and is sleeping
        // we must wake it up, it will be cleaned on next schedule
        if (ThreadId != ThreadCurrentHandle()) {
            SchedulerExpediteObject(thread->SchedulerObject);
        }
    }
    MutexUnlock(&thread->SyncObject);
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
        if (value != 1) {
            value = atomic_fetch_add(&target->References, 1);
            MutexUnlock(&target->SyncObject);
            SemaphoreWait(&target->EventObject, 0);
            value = atomic_fetch_sub(&target->References, 1);
        }
        return target->RetCode;
    }
    return -1;
}

void
ThreadingEnterUsermode(
    _In_ ThreadEntry_t EntryPoint,
    _In_ void*         Argument)
{
    Thread_t* thread = ThreadCurrentForCore(ArchGetProcessorCoreId());

    // Create the userspace stack now that we need it 
    thread->Contexts[THREADING_CONTEXT_LEVEL1] = ContextCreate(THREADING_CONTEXT_LEVEL1,
                                                               GetMemorySpacePageSize());
    assert(thread->Contexts[THREADING_CONTEXT_LEVEL1] != NULL);
    
    ContextReset(thread->Contexts[THREADING_CONTEXT_LEVEL1], THREADING_CONTEXT_LEVEL1,
                 (uintptr_t)EntryPoint, (uintptr_t)Argument);
        
    // Create the signal stack in preparation.
    thread->Contexts[THREADING_CONTEXT_SIGNAL] = ContextCreate(THREADING_CONTEXT_SIGNAL,
                                                               GetMemorySpacePageSize());
    assert(thread->Contexts[THREADING_CONTEXT_SIGNAL] != NULL);
    
    // Initiate switch to userspace
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

static void
DumpActiceThread(
    _In_ void* Resource,
    _In_ void* Context)
{
    Thread_t* Thread = Resource;
    int            Value;
    
    Value = atomic_load(&Thread->Cleanup);
    if (!Value) {
        WRITELINE("Thread %" PRIuIN " (%s) - Parent %" PRIuIN ", Instruction Pointer 0x%" PRIxIN "", 
            Thread->Handle, Thread->Name, Thread->ParentHandle, CONTEXT_IP(Thread->ContextActive));
    }
}

void
DisplayActiveThreads(void)
{
    // TODO
}

OsStatus_t
ThreadingAdvance(
    _In_  int     Preemptive,
    _In_  size_t  MillisecondsPassed,
    _Out_ size_t* NextDeadlineOut)
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
    
    // Perform pre-liminary actions only if the we are not going to 
    // cleanup and destroy the thread
    if (!cleanup) {
        SaveThreadState(currentThread);
        
        // Handle any received signals during runtime in system calls, this must be handled
        // here after any blocking operations has been queued so we can cancel it.
        signalsPending = atomic_load(&currentThread->Signaling.Pending);
        if (signalsPending) {
            SchedulerExpediteObject(currentThread->SchedulerObject);
        }
    }
    
    TRACE("%u: current thread: %s (Context 0x%" PRIxIN ", IP 0x%" PRIxIN ", PreEmptive %i)",
          core->Id, currentThread->Name, *Context, CONTEXT_IP((*Context)), PreEmptive);
GetNextThread:
    if ((currentThread->Flags & THREADING_IDLE) || cleanup == 1) {
        // If the thread is finished then add it to garbagecollector
        if (cleanup == 1) {
            DestroyHandle(currentThread->Handle);
        }
        TRACE(" > (null-schedule) initial next thread: %s", (nextThread) ? nextThread->Name : "null");
        currentThread = NULL;
    }
    
    // Advance the scheduler
    nextThread = (Thread_t*)SchedulerAdvance((currentThread != NULL) ?
                                             currentThread->SchedulerObject : NULL, Preemptive,
                                             MillisecondsPassed, NextDeadlineOut);
    
    // Sanitize if we need to active our idle thread, otherwise
    // do a final check that we haven't just gotten ahold of a thread
    // marked for finish
    if (nextThread == NULL) {
        TRACE("[threading] [switch] selecting idle");
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
        nextThread->Flags        &= ~(THREADING_TRANSITION_USERMODE);
        nextThread->ContextActive = nextThread->Contexts[THREADING_CONTEXT_LEVEL1];
    }
    
    // Newly started threads have no active context, except for idle threads
    if (nextThread->ContextActive == NULL) {
        nextThread->ContextActive = nextThread->Contexts[THREADING_CONTEXT_LEVEL0];
    }
    TRACE("%u: next thread: %s (Context 0x%" PRIxIN ", IP 0x%" PRIxIN ")",
          core->Id, nextThread->Name, nextThread->ContextActive,
          CONTEXT_IP(nextThread->ContextActive));
    
    // Set next active thread
    if (currentThread != nextThread) {
        CpuCoreSetCurrentThread(core, nextThread);
        RestoreThreadState(nextThread);
    }

    CpuCoreSetInterruptContext(core, nextThread->ContextActive);
    return OsSuccess;
}
