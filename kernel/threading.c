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
 * MollenOS Threading Interface
 * - Common routines that are platform independant to provide
 *   a flexible and generic threading platfrom
 */
#define __MODULE "MTIF"
//#define __TRACE

#include <garbagecollector.h>
#include <component/cpu.h>
#include <arch/thread.h>
#include <arch/utils.h>
#include <memoryspace.h>
#include <threading.h>
#include <machine.h>
#include <timers.h>
#include <handle.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <debug.h>
#include <heap.h>

OsStatus_t ThreadingReap(void *Context);

static Collection_t    Threads       = COLLECTION_INIT(KeyId);
static UUId_t          GlbThreadGcId = UUID_INVALID;
static _Atomic(UUId_t) GlbThreadId   = ATOMIC_VAR_INIT(1);

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
    else if (THREADING_RUNMODE(Thread->Flags) == THREADING_KERNELMODE || (Thread->Flags & THREADING_KERNELENTRY)) {
        Thread->Flags &= ~(THREADING_KERNELENTRY);
        Thread->Function(Thread->Arguments);
        TerminateThread(Thread->Header.Key.Value.Id, 0, 1);
    }
    else {
        EnterProtectedThreadLevel();
    }
    for (;;);
}

// Create default interrupt and signal stack that will be used
// in kernel space, and is mandatory for each thread to provide.
static void
CreateDefaultThreadContexts(
    _In_ MCoreThread_t* Thread)
{
    Thread->Contexts[THREADING_CONTEXT_LEVEL0] = ContextCreate(THREADING_CONTEXT_LEVEL0);
    ContextReset(Thread->Contexts[THREADING_CONTEXT_LEVEL0], THREADING_CONTEXT_LEVEL0, 
        (uintptr_t)&ThreadingEntryPoint, 0, 0, 0);

    // Should we create user stacks immediately?
}

// Setup defaults for a new thread and creates appropriate resources
static void
InitializeDefaultThread(
    _In_ MCoreThread_t* Thread,
    _In_ const char*    Name,
    _In_ ThreadEntry_t  Function,
    _In_ void*          Arguments,
    _In_ Flags_t        Flags)
{
    char NameBuffer[16];

    // Reset thread structure
    memset(Thread, 0, sizeof(MCoreThread_t));
    Thread->Header.Key.Value.Id = atomic_fetch_add(&GlbThreadId, 1);
    Thread->Function            = Function;
    Thread->Arguments           = Arguments;
    Thread->Flags               = Flags;
    Thread->ParentThreadId      = UUID_INVALID;
    
    // Get the startup time
    TimersGetSystemTick(&Thread->StartedAt);
    
    // Sanitize name, if NULL generate a new thread name of format 'Thread X'
    if (Name == NULL) {
        memset(&NameBuffer[0], 0, sizeof(NameBuffer));
        sprintf(&NameBuffer[0], "thread %" PRIuIN, Thread->Header.Key.Value.Id);
        Thread->Name = strdup(&NameBuffer[0]);
    }
    else {
        Thread->Name = strdup(Name);
    }
    
    // Create communication members
    Thread->Pipe        = CreateSystemPipe(0, 6); // 64 entries, 4kb
    Thread->SignalQueue = CollectionCreate(KeyInteger);
    
    // Setup initial scheduler information
    SchedulerThreadInitialize(Thread, Flags);

    // Register the thread with arch
    if (ThreadingRegister(Thread) != OsSuccess) {
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

OsStatus_t
ThreadingInitialize(void)
{
    GlbThreadGcId = GcRegister(ThreadingReap);
    return OsSuccess;
}

OsStatus_t
ThreadingEnable(void)
{
    MCoreThread_t* Thread = &GetCurrentProcessorCore()->IdleThread;
    InitializeDefaultThread(Thread, "idle", NULL, NULL, 
        THREADING_KERNELMODE | THREADING_IDLE);
    
    // Handle setup of memory space as that is not covered.
    Thread->MemorySpace       = GetCurrentMemorySpace();
    Thread->MemorySpaceHandle = GetCurrentMemorySpaceHandle();
    GetCurrentProcessorCore()->CurrentThread = Thread;
    return CollectionAppend(&Threads, &Thread->Header);
}

OsStatus_t
CreateThread(
    _In_  const char*    Name,
    _In_  ThreadEntry_t  Function,
    _In_  void*          Arguments,
    _In_  Flags_t        Flags,
    _In_  UUId_t         MemorySpaceHandle,
    _Out_ UUId_t*        Handle)
{
    MCoreThread_t*       Thread;
    MCoreThread_t*       Parent;
    UUId_t               CoreId;
    DataKey_t            Key;

    TRACE("CreateThread(%s, 0x%" PRIxIN ")", Name, Flags);

    Key.Value.Id = atomic_fetch_add(&GlbThreadId, 1);
    CoreId       = ArchGetProcessorCoreId();
    Parent       = GetCurrentThreadForCore(CoreId);

    Thread = (MCoreThread_t*)kmalloc(sizeof(MCoreThread_t));
    InitializeDefaultThread(Thread, Name, Function, Arguments, Flags);
    
    // Setup parent and cookie information
    Thread->ParentThreadId = Parent->Header.Key.Value.Id;
    if (Flags & THREADING_INHERIT) {
        Thread->Cookie = Parent->Cookie;
    }
    else {
        Thread->Cookie = CreateThreadCookie(Thread, Parent);
    }

    // Is a memory space given to us that we should run in? Determine run mode automatically
    if (MemorySpaceHandle != UUID_INVALID) {
        SystemMemorySpace_t* MemorySpace = (SystemMemorySpace_t*)LookupHandle(MemorySpaceHandle);
        if (MemorySpace == NULL) {
            return OsDoesNotExist;
        }

        if (MemorySpace->Flags & MEMORY_SPACE_APPLICATION) {
            Thread->Flags |= THREADING_USERMODE;
        }
        Thread->MemorySpace       = MemorySpace;
        Thread->MemorySpaceHandle = MemorySpaceHandle;
        Thread->Cookie            = Parent->Cookie;
        AcquireHandle(MemorySpaceHandle);
    }
    else {
        if (THREADING_RUNMODE(Flags) == THREADING_KERNELMODE) {
            Thread->MemorySpace       = GetDomainMemorySpace();
            Thread->MemorySpaceHandle = UUID_INVALID;
        }
        else {
            Flags_t MemorySpaceFlags = 0;
            if (THREADING_RUNMODE(Flags) == THREADING_USERMODE) {
                MemorySpaceFlags |= MEMORY_SPACE_APPLICATION;
            }
            if (Flags & THREADING_INHERIT) {
                MemorySpaceFlags |= MEMORY_SPACE_INHERIT;
            }
            if (CreateMemorySpace(MemorySpaceFlags, &Thread->MemorySpaceHandle) != OsSuccess) {
                ERROR("Failed to create memory space for thread");
                return OsError;
            }
            Thread->MemorySpace = (SystemMemorySpace_t*)LookupHandle(Thread->MemorySpaceHandle);
        }
    }
    
    // Create pre-mapped tls region for userspace threads
    if (THREADING_RUNMODE(Flags) == THREADING_USERMODE) {
        uintptr_t ThreadRegionStart = GetMachine()->MemoryMap.ThreadRegion.Start;
        size_t    ThreadRegionSize  = GetMachine()->MemoryMap.ThreadRegion.Length;
        CreateMemorySpaceMapping(Thread->MemorySpace, NULL, &ThreadRegionStart, ThreadRegionSize, 
            MAPPING_DOMAIN | MAPPING_USERSPACE, MAPPING_PHYSICAL_DEFAULT | MAPPING_VIRTUAL_FIXED, __MASK);
    }

    CollectionAppend(&Threads, &Thread->Header);
    SchedulerThreadQueue(Thread);
    *Handle = Thread->Header.Key.Value.Id;
    return OsSuccess;
}

void
ThreadingCleanupThread(
    _In_ MCoreThread_t* Thread)
{
    int i;

    assert(Thread != NULL);
    assert(atomic_load_explicit(&Thread->Cleanup, memory_order_relaxed) == 1);

    // Make sure we are completely removed as reference
    // from the entire system. We also signal all waiters for this
    // thread again before continueing just in case
    SchedulerThreadFinalize(Thread);
    SchedulerHandleSignalAll((uintptr_t*)&Thread->Cleanup);
    ThreadingUnregister(Thread);
    
    CollectionDestroy(Thread->SignalQueue);
    for (i = 0; i < THREADING_NUMCONTEXTS; i++) {
        if (Thread->Contexts[i] != NULL) {
            ContextDestroy(Thread->Contexts[i], i);
        }
    }
    DestroySystemPipe(Thread->Pipe);

    // Remove a reference to the memory space if not root
    if (Thread->MemorySpaceHandle != UUID_INVALID) {
        DestroyHandle(Thread->MemorySpaceHandle);
    }
    kfree((void*)Thread->Name);
    kfree(Thread);
}

OsStatus_t
ThreadingDetachThread(
    _In_ UUId_t ThreadId)
{
    MCoreThread_t* Thread = GetCurrentThreadForCore(ArchGetProcessorCoreId());
    MCoreThread_t* Target = GetThread(ThreadId);
    OsStatus_t     Status = OsDoesNotExist;
    
    // Detach is allowed if the caller is the spawner or the caller
    // is in same process
    if (Target != NULL) {
        Status = AreMemorySpacesRelated(Thread->MemorySpace, Target->MemorySpace);
        if (Target->ParentThreadId == Thread->Header.Key.Value.Id || Status == OsSuccess) {
            Target->ParentThreadId = UUID_INVALID;
            Status                 = OsSuccess;
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
    CollectionItem_t* Node;
    MCoreThread_t*    Target = GetThread(ThreadId);

    if (Target == NULL || (Target->Flags & THREADING_IDLE)) {
        return OsError; // Never, ever kill system idle threads
    }

    if (TerminateChildren) {
        _foreach(Node, &Threads) {
            MCoreThread_t *Child = (MCoreThread_t*)Node;
            if (Child->ParentThreadId == Target->Header.Key.Value.Id) {
                TerminateThread(Child->Header.Key.Value.Id, ExitCode, 1);
            }
        }
    }
    Target->RetCode = ExitCode;
    atomic_store(&Target->Cleanup, 1);

    // If the thread we are trying to kill is not this one, and is sleeping
    // we must wake it up, it will be cleaned on next schedule
    if (ThreadId != GetCurrentThreadId()) {
        SchedulerThreadSignal(Target);
    }
    SchedulerHandleSignalAll((uintptr_t*)&Target->Cleanup);
    return OsSuccess;
}

int
ThreadingJoinThread(
    _In_ UUId_t ThreadId)
{
    MCoreThread_t* Target = GetThread(ThreadId);
    if (Target != NULL && Target->ParentThreadId != UUID_INVALID) {
        int Finished = atomic_load(&Target->Cleanup);
        if (Finished != 1) {
            SchedulerAtomicThreadSleep((atomic_int*)&Target->Cleanup, &Finished, 0);
        }
        atomic_store(&Target->Cleanup, 1);
        return Target->RetCode;
    }
    return -1;
}

void
EnterProtectedThreadLevel(void)
{
    MCoreThread_t* Thread = GetCurrentThreadForCore(ArchGetProcessorCoreId());

    // Create the userspace stack now that we need it 
    Thread->Contexts[THREADING_CONTEXT_LEVEL1] = ContextCreate(THREADING_CONTEXT_LEVEL1);
    ContextReset(Thread->Contexts[THREADING_CONTEXT_LEVEL1], THREADING_CONTEXT_LEVEL1,
        (uintptr_t)Thread->Function, 0, (uintptr_t)Thread->Arguments, 0);
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
    return GetCurrentProcessorCore()->CurrentThread->Header.Key.Value.Id;
}

OsStatus_t
AreThreadsRelated(
    _In_ UUId_t Thread1,
    _In_ UUId_t Thread2)
{
    MCoreThread_t* First  = GetThread(Thread1);
    MCoreThread_t* Second = GetThread(Thread2);
    if (First == NULL || Second == NULL) {
        return OsDoesNotExist;
    }
    return AreMemorySpacesRelated(First->MemorySpace, Second->MemorySpace);
}

MCoreThread_t*
GetThread(
    _In_ UUId_t ThreadId)
{
    foreach(Node, &Threads) {
        MCoreThread_t *Thread = (MCoreThread_t*)Node;
        if (Thread->Header.Key.Value.Id == ThreadId) {
            return Thread;
        }
    }
    return NULL;
}

int
ThreadingIsCurrentTaskIdle(
    _In_ UUId_t CoreId)
{
    SystemCpuCore_t *Core = GetProcessorCore(CoreId);
    return (Core->CurrentThread == &Core->IdleThread) ? 1 : 0;
}

Flags_t
ThreadingGetCurrentMode(void)
{
    if (GetCurrentThreadForCore(ArchGetProcessorCoreId()) == NULL) {
        return THREADING_KERNELMODE;
    }
    return GetCurrentThreadForCore(ArchGetProcessorCoreId())->Flags & THREADING_MODEMASK;
}

OsStatus_t
ThreadingReap(
    _In_ void* Context)
{
    foreach(i, &Threads) {
        if ((void*)i == Context) {
            MCoreThread_t* Thread = (MCoreThread_t*)i;
            CollectionRemoveByNode(&Threads, &Thread->Header);
            ThreadingCleanupThread(Thread);
            break;
        }
    }
    return OsSuccess;
}

void
DisplayActiveThreads(void)
{
    foreach(i, &Threads) {
        MCoreThread_t* Thread = (MCoreThread_t*)i;
        if (atomic_load_explicit(&Thread->Cleanup, memory_order_relaxed) == 0) {
            WRITELINE("Thread %" PRIuIN " (%s) - Parent %" PRIuIN ", Instruction Pointer 0x%" PRIxIN "", 
                Thread->Header.Key.Value.Id, Thread->Name, Thread->ParentThreadId, CONTEXT_IP(Thread->ContextActive));
        }
    }
}

MCoreThread_t*
GetNextRunnableThread(
    _In_ MCoreThread_t* Current, 
    _In_ int            PreEmptive,
    _InOut_ Context_t** Context)
{
    SystemCpuCore_t* Core;
    MCoreThread_t*   NextThread;
    int              Cleanup;

    Core                    = GetCurrentProcessorCore();
    Current->ContextActive  = *Context;
    
    TRACE("%u: current thread: %s (Context 0x%" PRIxIN ", IP 0x%" PRIxIN ", PreEmptive %i)",
        Core->Id, Current->Name, *Context, CONTEXT_IP((*Context)), PreEmptive);

    Cleanup = atomic_load_explicit(&Current->Cleanup, memory_order_relaxed);
GetNextThread:
    if ((Current->Flags & THREADING_IDLE) || Cleanup == 1) {
        // If the thread is finished then add it to garbagecollector
        if (Cleanup == 1) {
            GcSignal(GlbThreadGcId, Current);
        }
        NextThread = SchedulerThreadSchedule(NULL, PreEmptive);
        TRACE(" > (null-schedule) initial next thread: %s", (NextThread) ? NextThread->Name : "null");
    }
    else {
        NextThread = SchedulerThreadSchedule(Current, PreEmptive);
        TRACE(" > initial next thread: %s", (NextThread) ? NextThread->Name : "null");
    }

    // Sanitize if we need to active our idle thread, otherwise
    // do a final check that we haven't just gotten ahold of a thread
    // marked for finish
    if (NextThread == NULL) {
        NextThread = &Core->IdleThread;
    }
    else {
        Cleanup = atomic_load_explicit(&NextThread->Cleanup, memory_order_relaxed);
        if (Cleanup == 1) {
            Current = NextThread;
            goto GetNextThread;
        }
    }

    // Handle level switch // thread startup
    if (NextThread->Flags & THREADING_TRANSITION_USERMODE) {
        NextThread->Flags         &= ~(THREADING_TRANSITION_USERMODE);
        NextThread->ContextActive = NextThread->Contexts[THREADING_CONTEXT_LEVEL1];
    }
    
    // Newly started threads have no active context, except for idle threads
    if (NextThread->ContextActive == NULL) {
        NextThread->ContextActive = NextThread->Contexts[THREADING_CONTEXT_LEVEL0];
    }
    TRACE("%u: next thread: %s (Context 0x%" PRIxIN ", IP 0x%" PRIxIN ", Slice %" PRIuIN ", Queue %i)", 
        Core->Id, NextThread->Name, NextThread->ContextActive, CONTEXT_IP(NextThread->ContextActive), 
        NextThread->TimeSlice, NextThread->Queue);

    // Handle any signals pending for thread
    SignalProcess(NextThread->Header.Key.Value.Id);
    
    Core->CurrentThread = NextThread;
    *Context            = NextThread->ContextActive;
    return NextThread;
}
