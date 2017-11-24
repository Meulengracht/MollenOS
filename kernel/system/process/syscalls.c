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
 * MollenOS MCore - System Calls
 */
#define __MODULE "SCIF"
#define __TRACE

/* Includes 
 * - System */
#include <system/interrupts.h>
#include <system/iospace.h>
#include <system/thread.h>
#include <system/utils.h>

#include <process/process.h>
#include <threading.h>
#include <scheduler.h>
#include <interrupts.h>
#include <heap.h>
#include <debug.h>
#include <timers.h>
#include <log.h>

/* Includes
 * - Library */
#include <assert.h>
#include <stddef.h>
#include <os/ipc/ipc.h>
#include <ds/mstring.h>
#include <string.h>

/* Shorthand */
#define DefineSyscall(_Sys) ((uintptr_t)&_Sys)

/* ScSystemDebug 
 * Debug/trace printing for userspace application and drivers */
OsStatus_t
ScSystemDebug(
    _In_ int Type,
    _In_ __CONST char *Module,
    _In_ __CONST char *Message)
{
    // Validate params
    if (Module == NULL || Message == NULL) {
        return OsError;
    }

    // Switch based on type
    if (Type == 0) {
        LogInformation(Module, Message);
    }
    else if (Type == 1) {
        LogDebug(Module, Message);
    }
    else {
        LogFatal(Module, Message);
    }

    // No more to be done
    return OsSuccess;
}

/***********************
 * Process Functions   *
 ***********************/

/* ScProcessSpawn
 * Spawns a new process with the given path
 * and the given arguments, returns UUID_INVALID on failure */
UUId_t
ScProcessSpawn(
    _In_ __CONST char *Path,
    _In_ __CONST ProcessStartupInformation_t *StartupInformation,
    _In_ int Asynchronous)
{
    // Variables
    MCorePhoenixRequest_t *Request  = NULL;
    MString_t *mPath                = NULL;
    UUId_t Result                   = UUID_INVALID;

    // Only the path cannot be null
    // Arguments are allowed to be null
    if (Path == NULL || StartupInformation == NULL) {
        return UUID_INVALID;
    }

    // Allocate resources for the spawn
    Request = (MCorePhoenixRequest_t*)kmalloc(sizeof(MCorePhoenixRequest_t));
    mPath = MStringCreate((void*)Path, StrUTF8);

    // Reset structure and set it up
    memset(Request, 0, sizeof(MCorePhoenixRequest_t));
    Request->Base.Type = AshSpawnProcess;
    Request->Path = mPath;
    Request->Base.Cleanup = Asynchronous;
    
    // Copy startup-information
    if (StartupInformation->ArgumentPointer != NULL
        && StartupInformation->ArgumentLength != 0) {
        Request->StartupInformation.ArgumentLength = 
            StartupInformation->ArgumentLength;
        Request->StartupInformation.ArgumentPointer = 
            (__CONST char*)kmalloc(StartupInformation->ArgumentLength);
        memcpy((void*)Request->StartupInformation.ArgumentPointer,
            StartupInformation->ArgumentPointer, 
            StartupInformation->ArgumentLength);
    }
    if (StartupInformation->InheritanceBlockPointer != NULL
        && StartupInformation->InheritanceBlockLength != 0) {
        Request->StartupInformation.InheritanceBlockLength = 
            StartupInformation->InheritanceBlockLength;
        Request->StartupInformation.InheritanceBlockPointer = 
            (__CONST char*)kmalloc(StartupInformation->InheritanceBlockLength);
        memcpy((void*)Request->StartupInformation.InheritanceBlockPointer,
            StartupInformation->InheritanceBlockPointer, 
            StartupInformation->InheritanceBlockLength);
    }

    // If it's an async request we return immediately
    // We return an invalid UUID as it cannot be used
    // for queries
    PhoenixCreateRequest(Request);
    if (Asynchronous != 0) {
        return UUID_INVALID;
    }

    // Otherwise wait for request to complete
    // and then cleanup and return the process id
    PhoenixWaitRequest(Request, 0);
    MStringDestroy(mPath);

    // Store result and cleanup
    Result = Request->AshId;
    kfree(Request);
    return Result;
}

/* ScProcessJoin
 * This waits for a child process to 
 * finish executing and returns it's exit-code */
int ScProcessJoin(UUId_t ProcessId)
{
    /* Wait for process */
    MCoreAsh_t *Process = PhoenixGetAsh(ProcessId);

    /* Sanity */
    if (Process == NULL)
        return -1;

    /* Sleep */
    SchedulerThreadSleep((uintptr_t*)Process, 0);

    /* Return the exit code */
    return Process->Code;
}

/* Attempts to kill the process 
 * with the given process-id */
OsStatus_t ScProcessKill(UUId_t ProcessId)
{
    /* Alloc on stack */
    MCorePhoenixRequest_t Request;

    /* Clean out structure */
    memset(&Request, 0, sizeof(MCorePhoenixRequest_t));

    /* Setup */
    Request.Base.Type = AshKill;
    Request.AshId = ProcessId;

    /* Fire! */
    PhoenixCreateRequest(&Request);
    PhoenixWaitRequest(&Request, 1000);

    /* Return the exit code */
    if (Request.Base.State == EventOk)
        return OsSuccess;
    else
        return OsError;
}

/* ScProcessExit
 * Kills the current process with the 
 * error code given as argument */
OsStatus_t ScProcessExit(int ExitCode)
{
    /* Retrieve crrent process */
    MCoreAsh_t *Process = PhoenixGetCurrentAsh();
    IntStatus_t IntrState;

    /* Sanity 
     * We don't proceed in case it's not a process */
    if (Process == NULL) {
        return OsError;
    }

    /* Log it and save return code */
    TRACE("Process %s terminated with code %i", 
        MStringRaw(Process->Name), ExitCode);
    Process->Code = ExitCode;

    /* Disable interrupts before proceeding */
    IntrState = InterruptDisable();

    /* Terminate all threads used by process */
    ThreadingTerminateAshThreads(Process->Id);

    /* Mark process for reaping */
    PhoenixTerminateAsh(Process);

    /* Enable Interrupts */
    InterruptRestoreState(IntrState);

    /* Kill this thread */
    ThreadingKillThread(ThreadingGetCurrentThreadId());
    ThreadingYield();

    /* Done */
    return OsSuccess;
}

/* ScProcessQuery
 * Queries information about 
 * the given process id, if called
 * with -1 it queries information
 * about itself */
OsStatus_t 
ScProcessQuery(
    UUId_t ProcessId, 
    AshQueryFunction_t Function, 
    void *Buffer, 
    size_t Length)
{
    /* Variables */
    MCoreProcess_t *Process = NULL;

    /* Sanity arguments */
    if (Buffer == NULL
        || Length == 0) {
        return OsError;
    }

    /* Lookup process */
    Process = PhoenixGetProcess(ProcessId);

    /* Sanity, found? */
    if (Process == NULL) {
        return OsError;
    }
    return PhoenixQueryAsh(&Process->Base, Function, Buffer, Length);
}

/* Installs a signal handler for 
 * the given signal number, it's then invokable
 * by other threads/processes etc */
uintptr_t
ScProcessSignal(
    int Signal, 
    uintptr_t Handler) 
{
    // Process
    MCoreProcess_t *Process = NULL;

    // Sanitize the signal
    if (Signal > NUMSIGNALS) {
        return 0;
    }

    // Get current process
    Process = PhoenixGetCurrentProcess();

    // Sanity... 
    // This should never happen though
    // Only I write code that has no process
    if (Process == NULL) {
        return 0;
    }

    // Always retrieve the old handler 
    // and return it, so temp store it before updating
    uintptr_t OldHandler = Process->Base.Signals.Handlers[Signal];
    Process->Base.Signals.Handlers[Signal] = Handler;
    return OldHandler;
}

/* Dispatches a signal to the target process id 
 * It will get handled next time it's selected for execution 
 * so we yield instantly as well. If processid is -1, we select self */
OsStatus_t
ScProcessRaise(
    UUId_t ProcessId, 
    int Signal)
{
    /* Variables */
    MCoreProcess_t *Process = NULL;

    /* Sanitize our params */
    if (Signal > NUMSIGNALS) {
        return OsError;
    }

    /* Lookup process */
    Process = PhoenixGetProcess(ProcessId);

    /* Sanity...
     * This should never happen though
     * Only I write code that has no process */
    if (Process == NULL) {
        return OsError;
    }

    /* Simply create a new signal 
     * and return it's value */
    if (SignalCreate(ProcessId, Signal))
        return OsError;
    else {
        ThreadingYield();
        return OsSuccess;
    }
}

/* ScGetStartupInformation
 * Retrieves information passed about process startup. */
OsStatus_t
ScGetStartupInformation(
    _InOut_ ProcessStartupInformation_t *StartupInformation)
{
    // Variables
    MCoreProcess_t *Process = NULL;

    // Sanitize parameters
    if (StartupInformation == NULL) {
        return OsError;
    }

    // Find process
    Process = PhoenixGetCurrentProcess();
    if (Process == NULL) {
        return OsError;
    }

    // Update outs
    if (Process->StartupInformation.ArgumentPointer != NULL) {
        if (StartupInformation->ArgumentPointer != NULL) {
            memcpy((void*)StartupInformation->ArgumentPointer, 
                Process->StartupInformation.ArgumentPointer,
                MIN(StartupInformation->ArgumentLength, 
                    Process->StartupInformation.ArgumentLength));
        }
        else {
            StartupInformation->ArgumentLength = 
                Process->StartupInformation.ArgumentLength;
        }
    }
    if (Process->StartupInformation.InheritanceBlockPointer != NULL) {
        if (StartupInformation->InheritanceBlockPointer != NULL) {
            memcpy((void*)StartupInformation->InheritanceBlockPointer, 
                Process->StartupInformation.InheritanceBlockPointer,
                MIN(StartupInformation->InheritanceBlockLength, 
                    Process->StartupInformation.InheritanceBlockLength));
        }
        else {
            StartupInformation->InheritanceBlockLength = 
                Process->StartupInformation.InheritanceBlockLength;
        }
    }

    // Done
    return OsSuccess;
}

/**************************
* Shared Object Functions *
***************************/

/* ScSharedObjectLoad
 * Load a shared object given a path 
 * path must exists otherwise NULL is returned */
Handle_t
ScSharedObjectLoad(
    _In_ __CONST char *SharedObject)
{
    // Variables
    MCoreAsh_t *Process = PhoenixGetCurrentAsh();
    MString_t *Path = NULL;
    Handle_t Handle = NULL;
    uintptr_t BaseAddress = 0;
    
    // Sanitize the process
    if (Process == NULL) {
        return Handle;
    }

    // Create a mstring object from the string
    Path = MStringCreate((void*)SharedObject, StrUTF8);

    // Try to resolve the library
    BaseAddress = Process->NextLoadingAddress;
    Handle = (Handle_t)PeResolveLibrary(Process->Executable, NULL, Path, &BaseAddress);
    Process->NextLoadingAddress = BaseAddress;

    // Cleanup the mstring object
    MStringDestroy(Path);
    return Handle;
}

/* ScSharedObjectGetFunction
 * Load a function-address given an shared object
 * handle and a function name, function must exist
 * otherwise null is returned */
uintptr_t
ScSharedObjectGetFunction(
    _In_ Handle_t Handle, 
    _In_ __CONST char *Function)
{
    // Validate parameters
    if (Handle == HANDLE_INVALID
        || Function == NULL) {
        return 0;
    }

    // Simply redirect to resolver
    return PeResolveFunction((MCorePeFile_t*)Handle, Function);
}

/* Unloads a valid shared object handle
 * returns 0 on success */
OsStatus_t
ScSharedObjectUnload(
    _In_ Handle_t Handle)
{
    // Variables
    MCoreAsh_t *Process = PhoenixGetCurrentAsh();

    // Sanitize the parameters and lookup
    if (Process == NULL || Handle == HANDLE_INVALID) {
        return OsError;
    }

    // Unload library
    PeUnloadLibrary(Process->Executable, (MCorePeFile_t*)Handle);
    return OsSuccess;
}

/***********************
* Threading Functions  *
***********************/

/* ScThreadCreate
 * Creates a new thread bound to 
 * the calling process, with the given entry point and arguments */
UUId_t
ScThreadCreate(
    _In_ ThreadEntry_t Entry, 
    _In_ void *Data, 
    _In_ Flags_t Flags)
{
    // Sanitize parameters
    if (Entry == NULL) {
        return UUID_INVALID;
    }

    // Redirect call with flags inheritted from
    // the current thread, we don't spawn threads in other priv-modes
    return ThreadingCreateThread(NULL, Entry, Data, 
        ThreadingGetCurrentMode() | THREADING_INHERIT | Flags);
}

/* ScThreadExit
 * Exits the current thread and 
 * instantly yields control to scheduler */
OsStatus_t
ScThreadExit(
    _In_ int ExitCode)
{
    // Redirect to this function, there is no return
    ThreadingExitThread(ExitCode);
    return OsSuccess;
}

/* ScThreadJoin
 * Thread join, waits for a given
 * thread to finish executing, and returns it's exit code */
OsStatus_t
ScThreadJoin(
    _In_ UUId_t ThreadId,
    _Out_ int *ExitCode)
{
    // Variables
    UUId_t PId;

    // Lookup process id
    PId = ThreadingGetCurrentThread(CpuGetCurrentId())->AshId;

    // Perform security checks
    if (ThreadingGetThread(ThreadId) == NULL
        || ThreadingGetThread(ThreadId)->AshId != PId) {
        return OsError;
    }

    // Redirect to thread function
    *ExitCode = ThreadingJoinThread(ThreadId);
    return OsSuccess;
}

/* ScThreadSignal
 * Kills the thread with the given id, owner
 * must be same process */
OsStatus_t
ScThreadSignal(
    _In_ UUId_t ThreadId,
    _In_ int SignalCode)
{
    // Variables
    UUId_t PId;

    // Unused
    _CRT_UNUSED(SignalCode);

    // Lookup process id
    PId = ThreadingGetCurrentThread(CpuGetCurrentId())->AshId;

    // Perform security checks
    if (ThreadingGetThread(ThreadId) == NULL
        || ThreadingGetThread(ThreadId)->AshId != PId) {
        return OsError;
    }

    // Error
    ERROR("ThreadSignal invoked, not implemented");
    return OsSuccess;
}

/* ScThreadSleep
 * Sleeps the current thread for the given milliseconds. */
OsStatus_t
ScThreadSleep(
    _In_ size_t MilliSeconds)
{
    SchedulerThreadSleep(NULL, MilliSeconds);
    return OsSuccess;
}

/* ScThreadGetCurrentId
 * Retrieves the thread id of the calling thread */
UUId_t
ScThreadGetCurrentId(void)
{
    // Simple
    return ThreadingGetCurrentThreadId();
}

/* ScThreadYield
 * This yields the current thread 
 * and gives cpu time to another thread */
OsStatus_t
ScThreadYield(void)
{
    // Invoke yield and return
    ThreadingYield();
    return OsSuccess;
}

/***********************
* Synch Functions      *
***********************/

/* ScConditionCreate
 * Create a new shared handle 
 * that is unique for a condition variable */
Handle_t
ScConditionCreate(void)
{
    /* Allocate a new unique address */
    return (Handle_t)kmalloc(sizeof(int));
}

/* ScConditionDestroy
 * Destroys a shared handle
 * for a condition variable */
OsStatus_t
ScConditionDestroy(
    _In_ Handle_t Handle)
{
    kfree(Handle);
    return OsSuccess;
}

/* ScSignalHandle
 * Signals a handle for wakeup 
 * This is primarily used for condition
 * variables and semaphores */
OsStatus_t
ScSignalHandle(
    _In_ uintptr_t *Handle)
{
    return SchedulerThreadWake(Handle);
}

/* Signals a handle for wakeup all
 * This is primarily used for condition
 * variables and semaphores */
OsStatus_t
ScSignalHandleAll(
    _In_ uintptr_t *Handle)
{
    SchedulerThreadWakeAll(Handle);
    return OsSuccess;
}

/* ScWaitForObject
 * Waits for a signal relating to the above function, this
 * function uses a timeout. Returns OsError on timed-out */
OsStatus_t
ScWaitForObject(
    _In_ uintptr_t *Handle,
    _In_ size_t Timeout)
{
    // Store reason for waking up
    int WakeReason = SchedulerThreadSleep(Handle, Timeout);
    if (WakeReason == SCHEDULER_SLEEP_OK) {
        return OsSuccess;
    }
    else {
        return OsError;
    }
}

/***********************
* Memory Functions     *
***********************/
#include <os/mollenos.h>

/* ScMemoryAllocate
 * Allows a process to allocate memory
 * from the userheap, it takes a size and allocation flags */
OsStatus_t
ScMemoryAllocate(
    _In_ size_t Size, 
    _In_ Flags_t Flags, 
    _Out_ uintptr_t *VirtualAddress,
    _Out_ uintptr_t *PhysicalAddress)
{
    // Variables
    MCoreAsh_t *Ash = NULL;
    uintptr_t AllocatedAddress = 0;

    // Locate the current running process
    Ash = PhoenixGetCurrentAsh();

    // Sanitize the process we looked up
    // we want it to exist of course
    if (Ash == NULL || Size == 0) {
        return OsError;
    }
    
    // Now do the allocation in the user-bitmap 
    // since memory is managed in userspace for speed
    AllocatedAddress = BlockBitmapAllocate(Ash->Heap, Size);

    // Sanitize the returned address
    if (AllocatedAddress == 0) {
        return OsError;
    }

    // Force a commit of memory if any flags
    // is given, because we can't apply flags later
    if (Flags != 0) {
        Flags |= MEMORY_COMMIT;
    }

    // Handle flags
    // If the commit flag is not given the flags won't be applied
    if (Flags & MEMORY_COMMIT) {
        int ExtendedFlags = AS_FLAG_APPLICATION;

        // Build extensions
        if (Flags & MEMORY_CONTIGIOUS) {
            ExtendedFlags |= AS_FLAG_CONTIGIOUS;
        }
        if (Flags & MEMORY_UNCHACHEABLE) {
            ExtendedFlags |= AS_FLAG_NOCACHE;
        }
        if (Flags & MEMORY_LOWFIRST) {
            // Handle mask
        }

        // Do the actual mapping
        if (AddressSpaceMap(AddressSpaceGetCurrent(),
            AllocatedAddress, Size, __MASK, ExtendedFlags, PhysicalAddress) != OsSuccess) {
            BlockBitmapFree(Ash->Heap, AllocatedAddress, Size);
            *VirtualAddress = 0;
            return OsError;
        }

        // Handle post allocation flags
        if (Flags & MEMORY_CLEAN) {
            memset((void*)AllocatedAddress, 0, Size);
        }
    }
    else {
        *PhysicalAddress = 0;
    }

    // Update out and return
    *VirtualAddress = (uintptr_t)AllocatedAddress;
    return OsSuccess;
}

/* Free's previous allocated memory, given an address
 * and a size (though not needed for now!) */
OsStatus_t 
ScMemoryFree(
    _In_ uintptr_t Address, 
    _In_ size_t Size)
{
    // Variables
    MCoreAsh_t *Ash = NULL;

    // Locate the current running process
    Ash = PhoenixGetCurrentAsh();

    // Sanitize the process we looked up
    // we want it to exist of course
    if (Ash == NULL || Size == 0) {
        return OsError;
    }

    // Now do the deallocation in the user-bitmap 
    // since memory is managed in userspace for speed
    BlockBitmapFree(Ash->Heap, Address, Size);

    // Return the result from unmap
    return AddressSpaceUnmap(AddressSpaceGetCurrent(), Address, Size);
}

/* Queries information about a chunk of memory 
 * and returns allocation information or stats 
 * depending on query function */
OsStatus_t
ScMemoryQuery(
    _Out_ MemoryDescriptor_t *Descriptor)
{
    // Variables
    SystemInformation_t SystemInfo;

    // Query information
    if (SystemInformationQuery(&SystemInfo) != OsSuccess) {
        return OsError;
    }

    // Copy relevant data over
    Descriptor->PageSizeBytes = PAGE_SIZE;
    Descriptor->PagesTotal = SystemInfo.PagesTotal;
    Descriptor->PagesUsed = SystemInfo.PagesAllocated;

    // Return no error, should never fail
    return OsSuccess;
}

/* ScMemoryAcquire
 * Acquires a given physical memory region described by a starting address
 * and region size. This is then mapped into the caller's address space and
 * is then accessible. The virtual address pointer is returned. */
OsStatus_t
ScMemoryAcquire(
    _In_ uintptr_t PhysicalAddress,
    _In_ size_t Size,
    _Out_ uintptr_t *VirtualAddress)
{
    // Variables
    MCoreAsh_t *Ash = NULL;
    size_t NumBlocks = 0, i = 0;

    // Assumptions:
    // PhysicalAddress is page aligned
    // Size is page-aligned

    // Locate the current running process
    Ash = PhoenixGetCurrentAsh();

    // Sanity
    if (Ash == NULL || PhysicalAddress == 0 || Size == 0) {
        return OsError;
    }

    // Start out by allocating memory 
    // in target process's shared memory space
    uintptr_t Shm = BlockBitmapAllocate(Ash->Shm, Size);
    NumBlocks = DIVUP(Size, PAGE_SIZE);

    // Sanity -> If we cross a page boundary
    if (((PhysicalAddress + Size) & PAGE_MASK)
        != (PhysicalAddress & PAGE_MASK)) {
        NumBlocks++;
    }

    // Sanitize the memory allocation
    assert(Shm != 0);

    // Update out
    *VirtualAddress = Shm + (PhysicalAddress & ATTRIBUTE_MASK);

    // Now we have to transfer our physical mappings 
    // to their new virtual
    for (i = 0; i < NumBlocks; i++) {
        // Map it directly into target process
        AddressSpaceMapFixed(Ash->AddressSpace, 
            PhysicalAddress + (i * PAGE_SIZE),
            Shm + (i * PAGE_SIZE),
            PAGE_SIZE, AS_FLAG_APPLICATION | AS_FLAG_VIRTUAL);
    }

    // Done
    return OsSuccess;
}

/* ScMemoryRelease
 * Releases a previously acquired memory region and unmaps it from the caller's
 * address space. The virtual address pointer will no longer be accessible. */
OsStatus_t
ScMemoryRelease(
    _In_ uintptr_t VirtualAddress, 
    _In_ size_t Size)
{
    // Variables
    MCoreAsh_t *Ash = PhoenixGetCurrentAsh();
    size_t NumBlocks, i;

    // Assumptions:
    // VirtualAddress is page aligned
    // Size is page-aligned

    // Sanitize the running process
    if (Ash == NULL || VirtualAddress == 0 || Size == 0) {
        return OsError;
    }

    // Calculate the number of blocks
    NumBlocks = DIVUP(Size, PAGE_SIZE);

    // Sanity -> If we cross a page boundary
    if (((VirtualAddress + Size) & PAGE_MASK)
        != (VirtualAddress & PAGE_MASK)) {
        NumBlocks++;
    }

    // Iterate through allocated pages and free them
    for (i = 0; i < NumBlocks; i++) {
        AddressSpaceUnmap(Ash->AddressSpace, 
            VirtualAddress + (i * PAGE_SIZE), PAGE_SIZE);
    }

    // Free it in bitmap
    BlockBitmapFree(Ash->Shm, VirtualAddress, Size);
    return OsSuccess;
}

/***********************
* Path Functions       *
***********************/

/* ScPathQueryWorkingDirectory
 * Queries the current working directory path
 * for the current process (See _MAXPATH) */
OsStatus_t ScPathQueryWorkingDirectory(
    char *Buffer, size_t MaxLength)
{
    // Variables
    MCoreProcess_t *Process = PhoenixGetCurrentProcess();
    size_t BytesToCopy = MaxLength;

    // Sanitize parameters
    if (Process == NULL || Buffer == NULL) {
        return OsError;
    }

    // Make sure we copy optimal num of bytes
    if (strlen(MStringRaw(Process->WorkingDirectory)) < MaxLength) {
        BytesToCopy = strlen(MStringRaw(Process->WorkingDirectory));
    }

    // Copy data over into buffer
    memcpy(Buffer, MStringRaw(Process->WorkingDirectory), BytesToCopy);
    return OsSuccess;
}

/* ScPathChangeWorkingDirectory
 * Performs changes to the current working directory
 * by canonicalizing the given path modifier or absolute
 * path */
OsStatus_t ScPathChangeWorkingDirectory(__CONST char *Path)
{
    // Variables
    MCoreProcess_t *Process = PhoenixGetCurrentProcess();
    MString_t *Translated = NULL;

    // Sanitize parameters
    if (Process == NULL || Path == NULL) {
        return OsError;
    }

    // Create a new string instead of modification
    Translated = MStringCreate((void*)Path, StrUTF8);
    MStringDestroy(Process->WorkingDirectory);
    Process->WorkingDirectory = Translated;
    return OsSuccess;
}

/* ScPathQueryApplication
 * Queries the application path for
 * the current process (See _MAXPATH) */
OsStatus_t ScPathQueryApplication(
    char *Buffer, size_t MaxLength)
{
    // Variables
    MCoreProcess_t *Process = PhoenixGetCurrentProcess();
    size_t BytesToCopy = MaxLength;

    // Sanitize parameters
    if (Process == NULL || Buffer == NULL) {
        return OsError;
    }

    // Make sure we copy optimal num of bytes
    if (strlen(MStringRaw(Process->BaseDirectory)) < MaxLength) {
        BytesToCopy = strlen(MStringRaw(Process->BaseDirectory));
    }

    // Copy data over into buffer
    memcpy(Buffer, MStringRaw(Process->BaseDirectory), BytesToCopy);
    return OsSuccess;
}

/***********************
* IPC Functions        *
***********************/

/* ScPipeOpen
 * Opens a new pipe for the calling Ash process
 * and allows communication to this port from other
 * processes */
OsStatus_t
ScPipeOpen(
    _In_ int Port, 
    _In_ Flags_t Flags)
{
    // No need for any preperation on this call, the
    // underlying call takes care of validation as well
    return PhoenixOpenAshPipe(PhoenixGetCurrentAsh(), Port, Flags);
}

/* ScPipeClose
 * Closes an existing pipe on a given port and
 * shutdowns any communication on that port */
OsStatus_t
ScPipeClose(
    _In_ int Port)
{
    // No need for any preperation on this call, the
    // underlying call takes care of validation as well
    return PhoenixCloseAshPipe(PhoenixGetCurrentAsh(), Port);
}

/* ScPipeRead
 * Reads the requested number of bytes from the system-pipe. */
OsStatus_t
ScPipeRead(
    _In_ int         Port,
    _In_ uint8_t    *Container,
    _In_ size_t      Length)
{
    // Variables
    MCorePipe_t *Pipe   = NULL;
    unsigned PipeWorker = 0;

    // Lookup the pipe for the given port
    if (Port == -1) {
        Pipe = ThreadingGetCurrentThread(CpuGetCurrentId())->Pipe;
    }
    else {
        Pipe = PhoenixGetAshPipe(PhoenixGetCurrentAsh(), Port);
    }

    // Sanitize the pipe
    if (Pipe == NULL) {
        ERROR("Trying to read from non-existing pipe %i", Port);
        return OsError;
    }

    // Sanitize parameters
    if (Length == 0) {
        return OsSuccess;
    }

    // Debug
    PipeConsumeAcquire(Pipe, &PipeWorker);
    PipeConsume(Pipe, Container, Length, PipeWorker);
    return PipeConsumeCommit(Pipe, PipeWorker);
}

/* ScPipeWrite
 * Writes the requested number of bytes to the system-pipe. */
OsStatus_t
ScPipeWrite(
    _In_ UUId_t      AshId,
    _In_ int         Port,
    _In_ uint8_t    *Message,
    _In_ size_t      Length)
{
    // Variables
    MCorePipe_t *Pipe   = NULL;
    unsigned PipeWorker = 0;
    unsigned PipeIndex  = 0;

    // Sanitize parameters
    if (Message == NULL || Length == 0) {
        ERROR("Invalid paramters for pipe-write");
        return OsError;
    }

    // Lookup the pipe for the given port
    if (Port == -1 && ThreadingGetThread(AshId) != NULL) {
        Pipe = ThreadingGetThread(AshId)->Pipe;
    }
    else {
        Pipe = PhoenixGetAshPipe(PhoenixGetAsh(AshId), Port);
    }

    // Sanitize the pipe
    if (Pipe == NULL) {
        ERROR("Invalid pipe %i", Port);
        return OsError;
    }
    
    // Debug
    PipeProduceAcquire(Pipe, Length, &PipeWorker, &PipeIndex);
    PipeProduce(Pipe, Message, Length, &PipeIndex);
    return PipeProduceCommit(Pipe, PipeWorker);
}

/* ScIpcSleep
 * This is a bit of a tricky synchronization method
 * and should always be used with care and WITH the timeout
 * since it could hang a process */
OsStatus_t
ScIpcSleep(
    _In_ size_t Timeout)
{
    // Variables
    MCoreAsh_t *Ash = PhoenixGetCurrentAsh();
    if (Ash == NULL) {
        FATAL(FATAL_SCOPE_KERNEL, "System-call from non-process.");
    }

    if (SchedulerThreadSleep((uintptr_t*)Ash, Timeout) != SCHEDULER_SLEEP_OK) {
        return OsError;
    }
    else {
        return OsSuccess;
    }
}

/* ScIpcWake
 * This must be used in conjuction with the above function
 * otherwise this function has no effect, this is used for
 * very limited IPC synchronization */
OsStatus_t
ScIpcWake(
    _In_ UUId_t Target)
{
    // Variables
    MCoreAsh_t *Ash = PhoenixGetAsh(Target);
    if (Ash == NULL) {
        return OsError;
    }

    // Signal wake-up
    SchedulerThreadWake((uintptr_t*)Ash);
    return OsSuccess;
}

/* ScRpcResponse
 * Waits for IPC RPC request to finish 
 * by polling the default pipe for a rpc-response */
OsStatus_t
ScRpcResponse(
    _In_ MRemoteCall_t *RemoteCall)
{
    // Variables
    MCoreAsh_t *Ash     = NULL;
    MCorePipe_t *Pipe   = NULL;
    size_t ToRead       = RemoteCall->Result.Length;
    unsigned PipeWorker = 0;
    
    // There can be a special case where 
    // Sender == PHOENIX_NO_ASH 
    // Use the builtin thread pipe
    if (RemoteCall->Sender == UUID_INVALID) {
        Pipe = ThreadingGetCurrentThread(CpuGetCurrentId())->Pipe;
    }
    else {
        // Resolve the current running process
        // and the default pipe in the rpc
        Ash = PhoenixGetAsh(ThreadingGetCurrentThread(CpuGetCurrentId())->AshId);
        Pipe = PhoenixGetAshPipe(Ash, RemoteCall->ResponsePort);

        // Sanitize the lookups
        if (Ash == NULL || Pipe == NULL) {
            ERROR("Process lookup failed for process 0x%x:%i", 
                ThreadingGetCurrentThread(CpuGetCurrentId())->AshId, RemoteCall->ResponsePort);
            return OsError;
        }
        else if (RemoteCall->Result.Type == ARGUMENT_NOTUSED) {
            ERROR("No result expected but used result-executer.");
            return OsError;
        }
    }

    // Read the data into the response-buffer
    PipeConsumeAcquire(Pipe, &PipeWorker);
    PipeConsume(Pipe, (uint8_t*)RemoteCall->Result.Data.Buffer, ToRead, PipeWorker);
    return PipeConsumeCommit(Pipe, PipeWorker);
}

/* ScRpcExecute
 * Executes an IPC RPC request to the
 * given process and optionally waits for
 * a reply/response */
OsStatus_t
ScRpcExecute(
    _In_ MRemoteCall_t *RemoteCall,
    _In_ UUId_t         Target,
    _In_ int            Async)
{
    // Variables
    MCorePipe_t *Pipe   = NULL;
    MCoreAsh_t *Ash     = NULL;
    int i               = 0;
    size_t TotalLength  = sizeof(MRemoteCall_t);
    unsigned PipeWorker = 0;
    unsigned PipeIndex  = 0;

    // Trace
    TRACE("ScRpcExecute()");
    
    // Start out by resolving both the
    // process and pipe
    Ash                 = PhoenixGetAsh(Target);
    Pipe                = PhoenixGetAshPipe(Ash, RemoteCall->Port);

    // Sanitize the lookups
    if (Ash == NULL || Pipe == NULL) {
        ERROR("Either target 0x%x or port %u did not exist in target",
            Target, RemoteCall->Port);
        return OsError;
    }

    // Install Sender
    RemoteCall->Sender  = ThreadingGetCurrentThread(CpuGetCurrentId())->AshId;
    for (i = 0; i < IPC_MAX_ARGUMENTS; i++) {
        if (RemoteCall->Arguments[i].Type == ARGUMENT_BUFFER) {
            TotalLength += RemoteCall->Arguments[i].Length;
        }
    }

    // Setup producer access
    PipeProduceAcquire(Pipe, TotalLength, &PipeWorker, &PipeIndex);
    PipeProduce(Pipe, (uint8_t*)RemoteCall, sizeof(MRemoteCall_t), &PipeIndex);
    for (i = 0; i < IPC_MAX_ARGUMENTS; i++) {
        if (RemoteCall->Arguments[i].Type == ARGUMENT_BUFFER) {
            PipeProduce(Pipe, (uint8_t*)RemoteCall->Arguments[i].Data.Buffer,
                RemoteCall->Arguments[i].Length, &PipeIndex);
        }
    }
    PipeProduceCommit(Pipe, PipeWorker);

    // Async request? Because if yes, don't
    // wait for response
    if (Async) {
        return OsSuccess;
    }

    // Ok, wait for response
    return ScRpcResponse(RemoteCall);
}

/* ScRpcListen
 * Listens for a new rpc-message on the default rpc-pipe. */
OsStatus_t
ScRpcListen(
    _In_ int             Port,
    _In_ MRemoteCall_t  *RemoteCall,
    _In_ uint8_t        *ArgumentBuffer)
{
    // Variables
    MCorePipe_t *Pipe       = NULL;
    MCoreAsh_t *Ash         = NULL;
    uint8_t *BufferPointer  = ArgumentBuffer;
    unsigned PipeWorker     = 0;
    int i                   = 0;
    
    // Start out by resolving both the
    // process and pipe
    Ash                     = PhoenixGetCurrentAsh();
    Pipe                    = PhoenixGetAshPipe(Ash, Port);

    // Start consuming
    PipeConsumeAcquire(Pipe, &PipeWorker);
    PipeConsume(Pipe, (uint8_t*)RemoteCall, sizeof(MRemoteCall_t), PipeWorker);
    for (i = 0; i < IPC_MAX_ARGUMENTS; i++) {
        if (RemoteCall->Arguments[i].Type == ARGUMENT_BUFFER) {
            RemoteCall->Arguments[i].Data.Buffer = (__CONST void*)BufferPointer;
            PipeConsume(Pipe, BufferPointer, RemoteCall->Arguments[i].Length, PipeWorker);
            BufferPointer += RemoteCall->Arguments[i].Length;
        }
    }
    return PipeConsumeCommit(Pipe, PipeWorker);
}

/***********************
 * Driver Functions    *
 ***********************/
#include <acpiinterface.h>
#include <os/driver/io.h>
#include <os/driver/device.h>
#include <os/driver/buffer.h>
#include <modules/modules.h>
#include <process/server.h>

/* ScAcpiQueryStatus
 * Queries basic acpi information and returns either OsSuccess
 * or OsError if Acpi is not supported on the running platform */
OsStatus_t ScAcpiQueryStatus(AcpiDescriptor_t *AcpiDescriptor)
{
    /* Sanitize the parameters */
    if (AcpiDescriptor == NULL) {
        return OsError;
    }

    /* Sanitize the acpi-status in this system */
    if (AcpiAvailable() == ACPI_NOT_AVAILABLE) {
        return OsError;
    }
    else {
        /* Copy information over to descriptor */
        AcpiDescriptor->Century = AcpiGbl_FADT.Century;
        AcpiDescriptor->BootFlags = AcpiGbl_FADT.BootFlags;
        AcpiDescriptor->ArmBootFlags = AcpiGbl_FADT.ArmBootFlags;
        AcpiDescriptor->Version = ACPI_VERSION_6_0;

        /* Wuhu! */
        return OsSuccess;
    }
}

/* ScAcpiQueryTableHeader
 * Queries the table header of the table that matches
 * the given signature, if none is found OsError is returned */
OsStatus_t ScAcpiQueryTableHeader(const char *Signature, ACPI_TABLE_HEADER *Header)
{
    /* Use a temporary buffer as we don't
     * really know the length */
    ACPI_TABLE_HEADER *PointerToHeader = NULL;

    /* Sanitize that ACPI is enabled on this
     * system before we query */
    if (AcpiAvailable() == ACPI_NOT_AVAILABLE) {
        return OsError;
    }

    /* Now query for the header */
    if (ACPI_FAILURE(AcpiGetTable((ACPI_STRING)Signature, 0, &PointerToHeader))) {
        return OsError;
    }

    /* Wuhuu, the requested table exists, copy the
     * header information over */
    memcpy(Header, PointerToHeader, sizeof(ACPI_TABLE_HEADER));
    return OsSuccess;
}

/* ScAcpiQueryTable
 * Queries the full table information of the table that matches
 * the given signature, if none is found OsError is returned */
OsStatus_t ScAcpiQueryTable(const char *Signature, ACPI_TABLE_HEADER *Table)
{
    /* Use a temporary buffer as we don't
     * really know the length */
    ACPI_TABLE_HEADER *Header = NULL;

    /* Sanitize that ACPI is enabled on this
     * system before we query */
    if (AcpiAvailable() == ACPI_NOT_AVAILABLE) {
        return OsError;
    }

    /* Now query for the full table */
    if (ACPI_FAILURE(AcpiGetTable((ACPI_STRING)Signature, 0, &Header))) {
        return OsError;
    }

    /* Wuhuu, the requested table exists, copy the
     * table information over */
    memcpy(Header, Table, Header->Length);
    return OsSuccess;
}

/* ScAcpiQueryInterrupt 
 * Queries the interrupt-line for the given bus, device and
 * pin combination. The pin must be zero indexed. Conform flags
 * are returned in the <AcpiConform> */
OsStatus_t ScAcpiQueryInterrupt(DevInfo_t Bus, DevInfo_t Device, int Pin, 
    int *Interrupt, Flags_t *AcpiConform)
{
    // Redirect the call to the interrupt system
    *Interrupt = AcpiDeriveInterrupt(Bus, Device, Pin, AcpiConform);
    return (*Interrupt == INTERRUPT_NONE) ? OsError : OsSuccess;
}
 
/* ScIoSpaceRegister
 * Creates and registers a new IoSpace with our
 * architecture sub-layer, it must support io-spaces 
 * or atleast dummy-implementation */
OsStatus_t ScIoSpaceRegister(DeviceIoSpace_t *IoSpace)
{
    /* Sanitize params */
    if (IoSpace == NULL) {
        return OsError;
    }

    /* Validate process permissions */

    /* Now we can try to actually register the
     * io-space, if it fails it exists already */
    return IoSpaceRegister(IoSpace);
}

/* ScIoSpaceAcquire
 * Tries to claim a given io-space, only one driver
 * can claim a single io-space at a time, to avoid
 * two drivers using the same device */
OsStatus_t ScIoSpaceAcquire(DeviceIoSpace_t *IoSpace)
{
    /* Sanitize params */
    if (IoSpace == NULL) {
        return OsError;
    }

    /* Validate process permissions */

    /* Now lets try to acquire the IoSpace */
    return IoSpaceAcquire(IoSpace);
}

/* ScIoSpaceRelease
 * Tries to release a given io-space, only one driver
 * can claim a single io-space at a time, to avoid
 * two drivers using the same device */
OsStatus_t ScIoSpaceRelease(DeviceIoSpace_t *IoSpace)
{
    /* Now lets try to release the IoSpace 
     * Don't bother with validation */
    return IoSpaceRelease(IoSpace);
}

/* ScIoSpaceDestroy
 * Destroys the io-space with the given id and removes
 * it from the io-manage in the operation system, it
 * can only be removed if its not already acquired */
OsStatus_t ScIoSpaceDestroy(UUId_t IoSpace)
{
    /* Sanitize params */

    /* Validate process permissions */

    /* Destroy the io-space, it might
     * not be possible, if we don't own it */
    return IoSpaceDestroy(IoSpace);
}

/* Allows a server to register an alias for its 
 * process id, as applications can't possibly know
 * its id if it changes */
OsStatus_t
ScRegisterAliasId(
    _In_ UUId_t Alias)
{
    // Debug
    TRACE("ScRegisterAliasId(Server %s, Alias 0x%X)",
        MStringRaw(PhoenixGetCurrentAsh()->Name), Alias);

    // Redirect call to phoenix
    return PhoenixRegisterAlias(
        PhoenixGetCurrentAsh(), Alias);
}

/* ScLoadDriver
 * Attempts to resolve the best possible drive for
 * the given device information */
OsStatus_t
ScLoadDriver(
    _In_ MCoreDevice_t *Device,
    _In_ size_t         Length)
{
    // Variables
    MCorePhoenixRequest_t *Request  = NULL;
    MCoreServer_t *Server           = NULL;
    MCoreModule_t *Module           = NULL;
    MString_t *Path                 = NULL;
    MRemoteCall_t RemoteCall        = { 0 };

    // Trace
    TRACE("ScLoadDriver()");

    // Sanitize parameters, length must not be less than base
    if (Device == NULL || Length < sizeof(MCoreDevice_t)) {
        return OsError;
    }

    // First of all, if a server has already been spawned
    // for the specific driver, then call it's RegisterInstance
    Server = PhoenixGetServerByDriver(
        Device->VendorId, Device->DeviceId,
        Device->Class, Device->Subclass);

    // Sanitize the lookup 
    // If it's not found, spawn server
    if (Server == NULL) {
        // Look for matching driver first, then generic
        Module = ModulesFindSpecific(Device->VendorId, Device->DeviceId);
        if (Module == NULL) {
            Module = ModulesFindGeneric(Device->Class, Device->Subclass);
        }
        if (Module == NULL) {
            return OsError;
        }

        // Build ramdisk path for module/server
        Path = MStringCreate("rd:/", StrUTF8);
        MStringAppendString(Path, Module->Name);

        // Create the request 
        Request = (MCorePhoenixRequest_t*)kmalloc(sizeof(MCorePhoenixRequest_t));
        memset(Request, 0, sizeof(MCorePhoenixRequest_t));
        Request->Base.Type = AshSpawnServer;
        Request->Path = Path;

        // Initiate request
        PhoenixCreateRequest(Request);
        PhoenixWaitRequest(Request, 0);

        // Sanitize startup
        Server = PhoenixGetServer(Request->AshId);
        assert(Server != NULL);

        // Cleanup
        MStringDestroy(Request->Path);
        kfree(Request);

        // Update the server params for next load
        Server->VendorId = Device->VendorId;
        Server->DeviceId = Device->DeviceId;
        Server->DeviceClass = Device->Class;
        Server->DeviceSubClass = Device->Subclass;
    }

    // Initialize the base of a new message, always protocol version 1
    RPCInitialize(&RemoteCall, 1, PIPE_RPCOUT, __DRIVER_REGISTERINSTANCE);
    RPCSetArgument(&RemoteCall, 0, Device, Length);

    // Make sure the server has opened it's comm-pipe
    PhoenixWaitAshPipe(&Server->Base, PIPE_RPCOUT);
    return ScRpcExecute(&RemoteCall, Server->Base.Id, 1);
}

/* ScRegisterInterrupt 
 * Allocates the given interrupt source for use by
 * the requesting driver, an id for the interrupt source
 * is returned. After a succesful register, OnInterrupt
 * can be called by the event-system */
UUId_t ScRegisterInterrupt(MCoreInterrupt_t *Interrupt, Flags_t Flags)
{
    /* Sanitize parameters */
    if (Interrupt == NULL
        || (Flags & (INTERRUPT_KERNEL | INTERRUPT_SOFT))) {
        return UUID_INVALID;
    }

    /* Just redirect the call */
    return InterruptRegister(Interrupt, Flags);
}

/* ScUnregisterInterrupt 
 * Unallocates the given interrupt source and disables
 * all events of OnInterrupt */
OsStatus_t ScUnregisterInterrupt(UUId_t Source)
{
    return InterruptUnregister(Source);
}

/* ScTimersStart
 * Creates a new standard timer for the requesting process. 
 * When interval elapses a __TIMEOUT event is generated for
 * the owner of the timer. */
UUId_t
ScTimersStart(
    _In_ size_t Interval,
    _In_ int Periodic,
    _In_ __CONST void *Data)
{
    return TimersStart(Interval, Periodic, Data);
}

/* ScTimersStop
 * Destroys a existing standard timer, owner must be the requesting
 * process. Otherwise access fault. */
OsStatus_t
ScTimersStop(
    _In_ UUId_t TimerId)
{
    return TimersStop(TimerId);
}

/***********************
* System Functions     *
***********************/

/* This ends the boot sequence
 * and thus redirects logging
 * to the system log-file
 * rather than the stdout */
int ScEndBootSequence(void)
{
    TRACE("Ending console session");
    LogRedirect(LogFile);
    return 0;
}

/* System (Environment) Query 
 * This function allows the user to query 
 * information about cpu, memory, stats etc */
int ScEnvironmentQuery(void)
{
    return 0;
}

/* ScSystemTime
 * Retrieves the system time. This is only ticking
 * if a system clock has been initialized. */
OsStatus_t
ScSystemTime(
    _Out_ struct tm *SystemTime)
{
    // Sanitize input
    if (SystemTime == NULL) {
        return OsError;
    }
    return TimersGetSystemTime(SystemTime);
}

/* ScSystemTick
 * Retrieves the system tick counter. This is only ticking
 * if a system timer has been initialized. */
OsStatus_t
ScSystemTick(
    _Out_ clock_t *SystemTick)
{
    // Sanitize input
    if (SystemTick == NULL) {
        return OsError;
    }
    return TimersGetSystemTick(SystemTick);
}

/* ScPerformanceFrequency
 * Returns how often the performance timer fires every
 * second, the value will never be 0 */
OsStatus_t
ScPerformanceFrequency(
    _Out_ LargeInteger_t *Frequency)
{
    // Sanitize input
    if (Frequency == NULL) {
        return OsError;
    }
    return TimersQueryPerformanceFrequency(Frequency);
}

/* ScPerformanceTick
 * Retrieves the system performance tick counter. This is only ticking
 * if a system performance timer has been initialized. */
OsStatus_t
ScPerformanceTick(
    _Out_ LargeInteger_t *Frequency)
{
    // Sanitize input
    if (Frequency == NULL) {
        return OsError;
    }
    return TimersQueryPerformanceTick(Frequency);
}

/* NoOperation
 * Empty operation, mostly because the operation is reserved */
OsStatus_t
NoOperation(void) {
    return OsSuccess;
}

/* Syscall Table */
uintptr_t GlbSyscallTable[91] = {
    DefineSyscall(ScSystemDebug),

    /* Process & Threading
     * - Starting index is 1 */
    DefineSyscall(ScProcessExit),
    DefineSyscall(ScProcessQuery),
    DefineSyscall(ScProcessSpawn),
    DefineSyscall(ScProcessJoin),
    DefineSyscall(ScProcessKill),
    DefineSyscall(ScProcessSignal),
    DefineSyscall(ScProcessRaise),
    DefineSyscall(ScSharedObjectLoad),
    DefineSyscall(ScSharedObjectGetFunction),
    DefineSyscall(ScSharedObjectUnload),
    DefineSyscall(ScThreadCreate),
    DefineSyscall(ScThreadExit),
    DefineSyscall(ScThreadSignal),
    DefineSyscall(ScThreadJoin),
    DefineSyscall(ScThreadSleep),
    DefineSyscall(ScThreadYield),
    DefineSyscall(ScThreadGetCurrentId),
    DefineSyscall(ScGetStartupInformation),
    DefineSyscall(NoOperation),
    DefineSyscall(NoOperation),

    /* Synchronization Functions - 21 */
    DefineSyscall(ScConditionCreate),
    DefineSyscall(ScConditionDestroy),
    DefineSyscall(ScWaitForObject),
    DefineSyscall(ScSignalHandle),
    DefineSyscall(ScSignalHandleAll),
    DefineSyscall(NoOperation),
    DefineSyscall(NoOperation),
    DefineSyscall(NoOperation),
    DefineSyscall(NoOperation),
    DefineSyscall(NoOperation),

    /* Memory Functions - 31 */
    DefineSyscall(ScMemoryAllocate),
    DefineSyscall(ScMemoryFree),
    DefineSyscall(ScMemoryQuery),
    DefineSyscall(ScMemoryAcquire),
    DefineSyscall(ScMemoryRelease),
    DefineSyscall(NoOperation),
    DefineSyscall(NoOperation),

    /* Path Functions - 38 */
    DefineSyscall(ScPathQueryWorkingDirectory),
    DefineSyscall(ScPathChangeWorkingDirectory),
    DefineSyscall(ScPathQueryApplication),

    /* IPC Functions - 41 */
    DefineSyscall(ScPipeOpen),
    DefineSyscall(ScPipeClose),
    DefineSyscall(ScPipeRead),
    DefineSyscall(ScPipeWrite),
    DefineSyscall(ScIpcSleep),
    DefineSyscall(ScIpcWake),
    DefineSyscall(ScRpcExecute),
    DefineSyscall(ScRpcResponse),
    DefineSyscall(ScRpcListen),
    DefineSyscall(NoOperation),

    /* System Functions - 51 */
    DefineSyscall(ScEndBootSequence),
    DefineSyscall(NoOperation),
    DefineSyscall(ScEnvironmentQuery),
    DefineSyscall(ScSystemTick),
    DefineSyscall(ScPerformanceFrequency),
    DefineSyscall(ScPerformanceTick),
    DefineSyscall(ScSystemTime),
    DefineSyscall(NoOperation),
    DefineSyscall(NoOperation),
    DefineSyscall(NoOperation),

    /* Driver Functions - 61 
     * - ACPI Support */
    DefineSyscall(ScAcpiQueryStatus),
    DefineSyscall(ScAcpiQueryTableHeader),
    DefineSyscall(ScAcpiQueryTable),
    DefineSyscall(ScAcpiQueryInterrupt),
    DefineSyscall(NoOperation),
    DefineSyscall(NoOperation),
    DefineSyscall(NoOperation),
    DefineSyscall(NoOperation),
    DefineSyscall(NoOperation),
    DefineSyscall(NoOperation),

    /* Driver Functions - 71 
     * - I/O Support */
    DefineSyscall(ScIoSpaceRegister),
    DefineSyscall(ScIoSpaceAcquire),
    DefineSyscall(ScIoSpaceRelease),
    DefineSyscall(ScIoSpaceDestroy),

    /* Driver Functions - 75
     * - Support */
    DefineSyscall(ScRegisterAliasId),
    DefineSyscall(ScLoadDriver),
    DefineSyscall(NoOperation),
    DefineSyscall(NoOperation),
    DefineSyscall(NoOperation),
    DefineSyscall(NoOperation),

    /* Driver Functions - 81
     * - Interrupt Support */
    DefineSyscall(ScRegisterInterrupt),
    DefineSyscall(ScUnregisterInterrupt),
    DefineSyscall(NoOperation),
    DefineSyscall(NoOperation),
    DefineSyscall(ScTimersStart),
    DefineSyscall(ScTimersStop),
    DefineSyscall(NoOperation),
    DefineSyscall(NoOperation),
    DefineSyscall(NoOperation),
    DefineSyscall(NoOperation)
};
