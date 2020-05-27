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

OsStatus_t ThreadingReap(void *Context);

// Common entry point for everything
static void
ThreadingEntryPoint(void)
{
    MCoreThread_t*  Thread;
    UUId_t          CoreId;

    TRACE("ThreadingEntryPoint()");

    CoreId = ArchGetProcessorCoreId();
    Thread = GetCurrentThreadForCore(CoreId);
    
    if (Thread->Flags & THREADING_IDLE) {
        while (1) {
            ArchProcessorIdle();
        }
    }
    else if (THREADING_RUNMODE(Thread->Flags) == THREADING_KERNELMODE || 
                (Thread->Flags & THREADING_KERNELENTRY)) {
        Thread->Flags &= ~(THREADING_KERNELENTRY);
        Thread->Function(Thread->Arguments);
        TerminateThread(Thread->Handle, 0, 1);
    }
    else {
        EnterProtectedThreadLevel();
    }
    for (;;);
}

static void
DestroyThread(
    _In_ void* Resource)
{
    MCoreThread_t* Thread = Resource;
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
    _In_ MCoreThread_t* Thread)
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
    _In_ MCoreThread_t* Thread,
    _In_ const char*    Name,
    _In_ ThreadEntry_t  Function,
    _In_ void*          Arguments,
    _In_ unsigned int        Flags)
{
    OsStatus_t Status;
    UUId_t     Handle;
    char       NameBuffer[16];

    // Reset thread structure
    memset(Thread, 0, sizeof(MCoreThread_t));
    MutexConstruct(&Thread->SyncObject, MUTEX_PLAIN);
    SemaphoreConstruct(&Thread->EventObject, 0, 1);
    SemaphoreConstruct(&Thread->WaitObject, 0, 1);
    
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
    _In_ MCoreThread_t* Thread,
    _In_ MCoreThread_t* Parent)
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
    _In_ MCoreThread_t* Parent,
    _In_ MCoreThread_t* Child)
{
    MCoreThread_t* Previous;
    MCoreThread_t* Link;
    
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
    _In_ MCoreThread_t* Parent,
    _In_ MCoreThread_t* Child)
{
    MCoreThread_t* Previous = NULL;
    MCoreThread_t* Link;
    
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
    MCoreThread_t* Thread = &GetCurrentProcessorCore()->IdleThread;
    InitializeDefaultThread(Thread, "idle", NULL, NULL, 
        THREADING_KERNELMODE | THREADING_IDLE);
    
    // Handle setup of memory space as that is not covered.
    Thread->MemorySpace       = GetCurrentMemorySpace();
    Thread->MemorySpaceHandle = GetCurrentMemorySpaceHandle();
    GetCurrentProcessorCore()->CurrentThread = Thread;
}

OsStatus_t
CreateThread(
    _In_  const char*    Name,
    _In_  ThreadEntry_t  Function,
    _In_  void*          Arguments,
    _In_  unsigned int        Flags,
    _In_  UUId_t         MemorySpaceHandle,
    _Out_ UUId_t*        Handle)
{
    MCoreThread_t* Thread;
    MCoreThread_t* Parent;
    UUId_t         CoreId;

    TRACE("CreateThread(%s, 0x%" PRIxIN ")", Name, Flags);

    CoreId = ArchGetProcessorCoreId();
    Parent = GetCurrentThreadForCore(CoreId);

    Thread = (MCoreThread_t*)kmalloc(sizeof(MCoreThread_t));
    if (!Thread) {
        return OsOutOfMemory;
    }
    
    InitializeDefaultThread(Thread, Name, Function, Arguments, Flags);
    
    // Setup parent and cookie information
    Thread->ParentHandle = Parent->Handle;
    if (Flags & THREADING_INHERIT) {
        Thread->Cookie = Parent->Cookie;
    }
    else {
        Thread->Cookie = CreateThreadCookie(Thread, Parent);
    }

    // Is a memory space given to us that we should run in? Determine run mode automatically
    if (MemorySpaceHandle != UUID_INVALID) {
        SystemMemorySpace_t* MemorySpace = (SystemMemorySpace_t*)LookupHandleOfType(
            MemorySpaceHandle, HandleTypeMemorySpace);
        if (MemorySpace == NULL) {
            // TODO: cleanup
            return OsDoesNotExist;
        }

        if (MemorySpace->Flags & MEMORY_SPACE_APPLICATION) {
            Thread->Flags |= THREADING_USERMODE;
        }
        Thread->MemorySpace       = MemorySpace;
        Thread->MemorySpaceHandle = MemorySpaceHandle;
        Thread->Cookie            = Parent->Cookie;
        AcquireHandle(MemorySpaceHandle, NULL);
    }
    else {
        if (THREADING_RUNMODE(Flags) == THREADING_KERNELMODE) {
            Thread->MemorySpace       = GetDomainMemorySpace();
            Thread->MemorySpaceHandle = UUID_INVALID;
        }
        else {
            unsigned int MemorySpaceFlags = 0;
            if (THREADING_RUNMODE(Flags) == THREADING_USERMODE) {
                MemorySpaceFlags |= MEMORY_SPACE_APPLICATION;
            }
            if (Flags & THREADING_INHERIT) {
                MemorySpaceFlags |= MEMORY_SPACE_INHERIT;
            }
            if (CreateMemorySpace(MemorySpaceFlags, &Thread->MemorySpaceHandle) != OsSuccess) {
                ERROR("Failed to create memory space for thread");
                // TODO: cleanup
                return OsError;
            }
            Thread->MemorySpace = (SystemMemorySpace_t*)LookupHandleOfType(
                Thread->MemorySpaceHandle, HandleTypeMemorySpace);
        }
    }
    
    // Create pre-mapped tls region for userspace threads, the area will be reserved
    // but not physically allocated
    if (THREADING_RUNMODE(Flags) == THREADING_USERMODE) {
        uintptr_t ThreadRegionStart = GetMachine()->MemoryMap.ThreadRegion.Start;
        size_t    ThreadRegionSize  = GetMachine()->MemoryMap.ThreadRegion.Length;
        OsStatus_t Status           = MemorySpaceMapReserved(Thread->MemorySpace,
            &ThreadRegionStart, ThreadRegionSize, 
            MAPPING_DOMAIN | MAPPING_USERSPACE, MAPPING_VIRTUAL_FIXED);
        if (Status != OsSuccess) {
            ERROR("[thread_create] failed to premap TLS area");
        }
    }
    AddChild(Parent, Thread);

    WARNING("[thread_create] new thread %s on core %u", 
        Thread->Name, SchedulerObjectGetAffinity(Thread->SchedulerObject));
    SchedulerQueueObject(Thread->SchedulerObject);
    *Handle = Thread->Handle;
    return OsSuccess;
}

OsStatus_t
ThreadingDetachThread(
    _In_ UUId_t ThreadId)
{
    MCoreThread_t* Thread = GetCurrentThreadForCore(ArchGetProcessorCoreId());
    MCoreThread_t* Target = (MCoreThread_t*)LookupHandleOfType(ThreadId, HandleTypeThread);
    OsStatus_t     Status = OsDoesNotExist;
    
    // Detach is allowed if the caller is the spawner or the caller is in same process
    if (Target != NULL) {
        Status = AreMemorySpacesRelated(Thread->MemorySpace, Target->MemorySpace);
        if (Target->ParentHandle == Thread->Handle || Status == OsSuccess) {
            MCoreThread_t* Parent = LookupHandleOfType(Target->ParentHandle, HandleTypeThread);
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
TerminateThread(
    _In_ UUId_t ThreadId,
    _In_ int    ExitCode,
    _In_ int    TerminateChildren)
{
    MCoreThread_t* Thread = (MCoreThread_t*)LookupHandleOfType(ThreadId, HandleTypeThread);
    int            Value;
    
    if (!Thread) {
        return OsDoesNotExist;
    }
    
    // Never, ever kill system idle threads
    if (Thread->Flags & THREADING_IDLE) {
        return OsInvalidPermissions;
    }
    
    TRACE("TerminateThread(%s, %i, %i)", Thread->Name, ExitCode, TerminateChildren);
    
    MutexLock(&Thread->SyncObject);
    Value = atomic_load(&Thread->Cleanup);
    if (Value == 0) {
        if (Thread->ParentHandle != UUID_INVALID) {
            MCoreThread_t* Parent = LookupHandleOfType(Thread->ParentHandle, HandleTypeThread);
            if (!Parent) {
                // Parent does not exist anymore, it was terminated without terminating children
                // which is very unusal. Log this. 
                WARNING("[terminate_thread] orphaned child terminating %s", Thread->Name);
            }
            else {
                RemoveChild(Parent, Thread);
            }
        }
        
        if (TerminateChildren) {
            MCoreThread_t* ChildItr = Thread->Children;
            while (ChildItr) {
                OsStatus_t Status = TerminateThread(ChildItr->Handle, ExitCode, TerminateChildren);
                if (Status != OsSuccess) {
                    ERROR("[terminate_thread] failed to terminate child %s of %s", ChildItr->Name, Thread->Name);
                }
                ChildItr = ChildItr->Sibling;
            }
        }
        Thread->RetCode = ExitCode;
        atomic_store(&Thread->Cleanup, 1);
    
        // If the thread we are trying to kill is not this one, and is sleeping
        // we must wake it up, it will be cleaned on next schedule
        if (ThreadId != GetCurrentThreadId()) {
            SchedulerExpediteObject(Thread->SchedulerObject);
        }
    }
    MutexUnlock(&Thread->SyncObject);
    return OsSuccess;
}

int
ThreadingJoinThread(
    _In_ UUId_t ThreadId)
{
    MCoreThread_t* Target = (MCoreThread_t*)LookupHandleOfType(ThreadId, HandleTypeThread);
    int            Value;
    
    if (Target != NULL && Target->ParentHandle != UUID_INVALID) {
        MutexLock(&Target->SyncObject);
        Value = atomic_load(&Target->Cleanup);
        if (Value != 1) {
            Value = atomic_fetch_add(&Target->References, 1);
            MutexUnlock(&Target->SyncObject);
            SemaphoreWait(&Target->EventObject, 0);
            Value = atomic_fetch_sub(&Target->References, 1);
        }
        return Target->RetCode;
    }
    return -1;
}

void
EnterProtectedThreadLevel(void)
{
    MCoreThread_t* Thread = GetCurrentThreadForCore(ArchGetProcessorCoreId());

    // Create the userspace stack now that we need it 
    Thread->Contexts[THREADING_CONTEXT_LEVEL1] = ContextCreate(THREADING_CONTEXT_LEVEL1,
        GetMemorySpacePageSize());
    assert(Thread->Contexts[THREADING_CONTEXT_LEVEL1] != NULL);
    
    ContextReset(Thread->Contexts[THREADING_CONTEXT_LEVEL1], THREADING_CONTEXT_LEVEL1,
        (uintptr_t)Thread->Function, (uintptr_t)Thread->Arguments);
        
    // Create the signal stack in preparation.
    Thread->Contexts[THREADING_CONTEXT_SIGNAL] = ContextCreate(THREADING_CONTEXT_SIGNAL,
        GetMemorySpacePageSize());
    assert(Thread->Contexts[THREADING_CONTEXT_SIGNAL] != NULL);
    
    // Initiate switch to userspace
    Thread->Flags |= THREADING_TRANSITION_USERMODE;
    ThreadingYield();
    for (;;);
}

MCoreThread_t*
GetCurrentThreadForCore(
    _In_ UUId_t CoreId)
{
    return GetProcessorCore(CoreId)->CurrentThread;
}

UUId_t
GetCurrentThreadId(void)
{
    if (GetCurrentProcessorCore()->CurrentThread == NULL) {
        return 0;
    }
    return GetCurrentProcessorCore()->CurrentThread->Handle;
}

OsStatus_t
AreThreadsRelated(
    _In_ UUId_t Thread1,
    _In_ UUId_t Thread2)
{
    MCoreThread_t* First  = (MCoreThread_t*)LookupHandleOfType(Thread1, HandleTypeThread);
    MCoreThread_t* Second = (MCoreThread_t*)LookupHandleOfType(Thread2, HandleTypeThread);
    if (First == NULL || Second == NULL) {
        return OsDoesNotExist;
    }
    return AreMemorySpacesRelated(First->MemorySpace, Second->MemorySpace);
}

int
ThreadingIsCurrentTaskIdle(
    _In_ UUId_t CoreId)
{
    SystemCpuCore_t *Core = GetProcessorCore(CoreId);
    return (Core->CurrentThread == &Core->IdleThread) ? 1 : 0;
}

unsigned int
ThreadingGetCurrentMode(void)
{
    if (GetCurrentThreadForCore(ArchGetProcessorCoreId()) == NULL) {
        return THREADING_KERNELMODE;
    }
    return GetCurrentThreadForCore(ArchGetProcessorCoreId())->Flags & THREADING_MODEMASK;
}

static void
DumpActiceThread(
    _In_ void* Resource,
    _In_ void* Context)
{
    MCoreThread_t* Thread = Resource;
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
    SystemCpuCore_t* Core    = GetCurrentProcessorCore();
    MCoreThread_t*   Current = Core->CurrentThread;
    MCoreThread_t*   NextThread;
    int              SignalsPending;
    int              Cleanup;

    // Is threading disabled?
    if (!Current) {
        return OsError;
    }

    Cleanup = atomic_load(&Current->Cleanup);
    Current->ContextActive = Core->InterruptRegisters;
    
    // Perform pre-liminary actions only if the we are not going to 
    // cleanup and destroy the thread
    if (!Cleanup) {
        SaveThreadState(Current);
        
        // Handle any received signals during runtime in system calls, this must be handled
        // here after any blocking operations has been queued so we can cancel it.
        SignalsPending = atomic_load(&Current->Signaling.Pending);
        if (SignalsPending) {
            SchedulerExpediteObject(Current->SchedulerObject);
        }
    }
    
    TRACE("%u: current thread: %s (Context 0x%" PRIxIN ", IP 0x%" PRIxIN ", PreEmptive %i)",
        Core->Id, Current->Name, *Context, CONTEXT_IP((*Context)), PreEmptive);
GetNextThread:
    if ((Current->Flags & THREADING_IDLE) || Cleanup == 1) {
        // If the thread is finished then add it to garbagecollector
        if (Cleanup == 1) {
            DestroyHandle(Current->Handle);
        }
        TRACE(" > (null-schedule) initial next thread: %s", (NextThread) ? NextThread->Name : "null");
        Current = NULL;
    }
    
    // Advance the scheduler
    NextThread = (MCoreThread_t*)SchedulerAdvance((Current != NULL) ? 
        Current->SchedulerObject : NULL, Preemptive, 
        MillisecondsPassed, NextDeadlineOut);
    
    // Sanitize if we need to active our idle thread, otherwise
    // do a final check that we haven't just gotten ahold of a thread
    // marked for finish
    if (NextThread == NULL) {
        TRACE("[threading] [switch] selecting idle");
        NextThread = &Core->IdleThread;
    }
    else {
        Cleanup = atomic_load(&NextThread->Cleanup);
        if (Cleanup == 1) {
            Current = NextThread;
            goto GetNextThread;
        }
    }

    // Handle level switch, thread startup
    if (NextThread->Flags & THREADING_TRANSITION_USERMODE) {
        NextThread->Flags        &= ~(THREADING_TRANSITION_USERMODE);
        NextThread->ContextActive = NextThread->Contexts[THREADING_CONTEXT_LEVEL1];
    }
    
    // Newly started threads have no active context, except for idle threads
    if (NextThread->ContextActive == NULL) {
        NextThread->ContextActive = NextThread->Contexts[THREADING_CONTEXT_LEVEL0];
    }
    TRACE("%u: next thread: %s (Context 0x%" PRIxIN ", IP 0x%" PRIxIN ")", 
        Core->Id, NextThread->Name, NextThread->ContextActive, 
        CONTEXT_IP(NextThread->ContextActive));
    
    // Set next active thread
    if (Current != NextThread) {
        Core->CurrentThread = NextThread;
        RestoreThreadState(NextThread);
    }
    
    Core->InterruptRegisters = NextThread->ContextActive;
    return OsSuccess;
}
