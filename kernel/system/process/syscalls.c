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

/* Includes 
 * - System */
#include <system/iospace.h>
#include <system/thread.h>
#include <system/utils.h>

#include <process/process.h>
#include <threading.h>
#include <scheduler.h>
#include <interrupts.h>
#include <heap.h>
#include <timers.h>
#include <log.h>

/* Includes
 * - Library */
#include <assert.h>
#include <stddef.h>
#include <os/ipc/ipc.h>
#include <ds/list.h>
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
	return OsNoError;
}

/***********************
 * Process Functions   *
 ***********************/

/* ScProcessSpawn
 * Spawns a new process with the given path
 * and the given arguments, returns UUID_INVALID 
 * on failure */
UUId_t ScProcessSpawn(char *Path, char *Arguments)
{
	/* Variables */
	MCorePhoenixRequest_t Request;
	MString_t *mPath = NULL;
	MString_t *mArguments = NULL;

	/* Sanitize the path only */
	if (Path == NULL) {
		return UUID_INVALID;
	}

	/* Allocate the string instances */
	mPath = MStringCreate(Path, StrUTF8);
	mArguments = (Arguments == NULL) ? NULL : MStringCreate(Arguments, StrUTF8);

	/* Clean out structure */
	memset(&Request, 0, sizeof(MCorePhoenixRequest_t));

	/* Setup */
	Request.Base.Type = AshSpawnProcess;
	Request.Path = mPath;
	Request.Arguments.String = mArguments;
	
	/* Fire! */
	PhoenixCreateRequest(&Request);
	PhoenixWaitRequest(&Request, 0);

	/* Cleanup */
	MStringDestroy(mPath);

	/* Only cleanup arguments if not null */
	if (mArguments != NULL) {
		MStringDestroy(mArguments);
	}

	/* Done */
	return Request.AshId;
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
	SchedulerSleepThread((uintptr_t*)Process, 0);
	IThreadYield();

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
		return OsNoError;
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
	LogDebug("SYSC", "Process %s terminated with code %i", 
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

	/* Yield */
	IThreadYield();

	/* Done */
	return OsNoError;
}

/* ScProcessQuery
 * Queries information about 
 * the given process id, if called
 * with -1 it queries information
 * about itself */
OsStatus_t ScProcessQuery(UUId_t ProcessId, AshQueryFunction_t Function, void *Buffer, size_t Length)
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

	/* Deep Call */
	return (OsStatus_t)PhoenixQueryAsh(&Process->Base, Function, Buffer, Length);
}

/* Installs a signal handler for 
 * the given signal number, it's then invokable
 * by other threads/processes etc */
int ScProcessSignal(int Signal, uintptr_t Handler) 
{
	// Process
	MCoreProcess_t *Process = NULL;

	// Sanitize the signal
	if (Signal > NUMSIGNALS) {
		return -1;
	}

	// Get current process
	Process = PhoenixGetCurrentProcess();

	// Sanity... 
	// This should never happen though
	// Only I write code that has no process
	if (Process == NULL) {
		return -1;
	}

	// Always retrieve the old handler 
	// and return it, so temp store it before updating
	uintptr_t OldHandler = Process->Base.Signals.Handlers[Signal];
	Process->Base.Signals.Handlers[Signal] = Handler;
	return (int)OldHandler;
}

/* Dispatches a signal to the target process id 
 * It will get handled next time it's selected for execution 
 * so we yield instantly as well. If processid is -1, we select self */
OsStatus_t ScProcessRaise(UUId_t ProcessId, int Signal)
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
		IThreadYield();
		return OsNoError;
	}
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
	uintptr_t BaseAddress = 0;
	Handle_t Handle = NULL;
	
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
uintptr_t ScSharedObjectGetFunction(Handle_t Handle, __CONST char *Function)
{
	/* Validate */
	if (Handle == HANDLE_INVALID
		|| Function == NULL)
		return 0;

	/* Try to resolve function */
	return PeResolveFunction((MCorePeFile_t*)Handle, Function);
}

/* Unloads a valid shared object handle
 * returns 0 on success */
OsStatus_t ScSharedObjectUnload(Handle_t Handle)
{
	// Variables
	MCoreAsh_t *Process = PhoenixGetCurrentAsh();

	// Sanitize the parameters and lookup
	if (Process == NULL || Handle == HANDLE_INVALID) {
		return OsError;
	}

	// Unload library
	PeUnloadLibrary(Process->Executable, (MCorePeFile_t*)Handle);
	return OsNoError;
}

/***********************
* Threading Functions  *
***********************/

/* ScThreadCreate
 * Creates a new thread bound to 
 * the calling process, with the given entry point and arguments */
UUId_t ScThreadCreate(ThreadEntry_t Entry, void *Data, Flags_t Flags)
{
	/* Sanity */
	if (Entry == NULL) {
		return UUID_INVALID;
	}

	/* Don't use flags for now */
	_CRT_UNUSED(Flags);

	/* Redirect call with flags inheritted from
	 * the current thread, we don't spawn threads in other priv-modes */
	return ThreadingCreateThread(NULL, Entry, Data, 
		ThreadingGetCurrentMode() | THREADING_INHERIT);
}

/* ScThreadExit
 * Exits the current thread and 
 * instantly yields control to scheduler */
OsStatus_t ScThreadExit(int ExitCode)
{
	/* Deep Call */
	ThreadingExitThread(ExitCode);

	/* We will never reach this 
	 * statement */
	return OsNoError;
}

/* ScThreadJoin
 * Thread join, waits for a given
 * thread to finish executing, and returns it's exit code */
int ScThreadJoin(UUId_t ThreadId)
{
	/* Lookup process information */
	UUId_t CurrentPid = ThreadingGetCurrentThread(CpuGetCurrentId())->AshId;

	/* Sanity */
	if (ThreadingGetThread(ThreadId) == NULL
		|| ThreadingGetThread(ThreadId)->AshId != CurrentPid) {
		return -1;
	}

	/* Simply deep call again 
	 * the function takes care 
	 * of validation as well */
	return ThreadingJoinThread(ThreadId);
}

/* ScThreadKill
 * Kills the thread with the given id, owner
 * must be same process */
OsStatus_t ScThreadKill(UUId_t ThreadId)
{
	/* Lookup process information */
	UUId_t CurrentPid = ThreadingGetCurrentThread(CpuGetCurrentId())->AshId;

	/* Sanity */
	if (ThreadingGetThread(ThreadId) == NULL
		|| ThreadingGetThread(ThreadId)->AshId != CurrentPid) {
		return OsError;
	}

	/* Ok, we can kill it */
	ThreadingKillThread(ThreadId);

	/* Done! */
	return OsNoError;
}

/* ScThreadSleep
 * Sleeps the current thread for the
 * given milliseconds. */
OsStatus_t ScThreadSleep(size_t MilliSeconds)
{
	/* Redirect the call */
	SleepMs(MilliSeconds);
	return OsNoError;
}

/* ScThreadGetCurrentId
 * Retrieves the current thread id */
UUId_t ScThreadGetCurrentId(void)
{
	return ThreadingGetCurrentThreadId();
}

/* ScThreadYield
 * This yields the current thread 
 * and gives cpu time to another thread */
OsStatus_t ScThreadYield(void)
{
	/* Redirect the call */
	IThreadYield();
	return OsNoError;
}

/***********************
* Synch Functions      *
***********************/

/* ScConditionCreate
 * Create a new shared handle 
 * that is unique for a condition variable */
uintptr_t ScConditionCreate(void)
{
	/* Allocate a new unique address */
	return (uintptr_t)kmalloc(sizeof(int));
}

/* ScConditionDestroy
 * Destroys a shared handle
 * for a condition variable */
OsStatus_t ScConditionDestroy(uintptr_t *Handle)
{
	kfree(Handle);
	return OsNoError;
}

/* ScSyncWakeUp
 * Signals a handle for wakeup 
 * This is primarily used for condition
 * variables and semaphores */
OsStatus_t ScSyncWakeUp(uintptr_t *Handle)
{
	return SchedulerWakeupOneThread(Handle) == 1 ? OsNoError : OsError;
}

/* Signals a handle for wakeup all
 * This is primarily used for condition
 * variables and semaphores */
OsStatus_t ScSyncWakeUpAll(uintptr_t *Handle)
{
	SchedulerWakeupAllThreads(Handle);
	return OsNoError;
}

/* ScSyncSleep
 * Waits for a signal relating to the above function, this
 * function uses a timeout. Returns OsError on timed-out */
OsStatus_t ScSyncSleep(uintptr_t *Handle, size_t Timeout)
{
	/* Get current thread */
	MCoreThread_t *Current = ThreadingGetCurrentThread(CpuGetCurrentId());

	/* Sleep */
	SchedulerSleepThread(Handle, Timeout);
	IThreadYield();

	/* Sanity */
	if (Timeout != 0 && Current->Sleep == 0) {
		return OsError;
	}
	else {
		return OsNoError;
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
	AllocatedAddress = BitmapAllocateAddress(Ash->Heap, Size);

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
			AllocatedAddress, Size, __MASK, ExtendedFlags, PhysicalAddress) != OsNoError) {
			BitmapFreeAddress(Ash->Heap, AllocatedAddress, Size);
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
	return OsNoError;
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
	BitmapFreeAddress(Ash->Heap, Address, Size);

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
	if (SystemInformationQuery(&SystemInfo) != OsNoError) {
		return OsError;
	}

	// Copy relevant data over
	Descriptor->PageSizeBytes = PAGE_SIZE;
	Descriptor->PagesTotal = SystemInfo.PagesTotal;
	Descriptor->PagesUsed = SystemInfo.PagesAllocated;

	// Return no error, should never fail
	return OsNoError;
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
	uintptr_t Shm = BitmapAllocateAddress(Ash->Shm, Size);
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
	return OsNoError;
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
	BitmapFreeAddress(Ash->Shm, VirtualAddress, Size);
	return OsNoError;
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
	return OsNoError;
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
	return OsNoError;
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
	return OsNoError;
}

/***********************
* IPC Functions        *
***********************/

/* ScPipeOpen
 * Opens a new pipe for the calling Ash process
 * and allows communication to this port from other
 * processes */
OsStatus_t ScPipeOpen(int Port, Flags_t Flags)
{
	// No need for any preperation on this call, the
	// underlying call takes care of validation as well
	return PhoenixOpenAshPipe(PhoenixGetCurrentAsh(), Port, Flags);
}

/* ScPipeClose
 * Closes an existing pipe on a given port and
 * shutdowns any communication on that port */
OsStatus_t ScPipeClose(int Port)
{
	// No need for any preperation on this call, the
	// underlying call takes care of validation as well
	return PhoenixCloseAshPipe(PhoenixGetCurrentAsh(), Port);
}

/* ScPipeRead
 * Get the top message for this process
 * and consume the message, if no message 
 * is available, this function will block untill 
 * a message is available */
OsStatus_t ScPipeRead(int Port, uint8_t *Container, size_t Length, int Peek)
{
	/* Variables */
	MCorePipe_t *Pipe = NULL;

	/* Lookup the pipe for the given port */
	if (Port == -1) {
		Pipe = Pipe = ThreadingGetCurrentThread(CpuGetCurrentId())->Pipe;
	}
	else {
		Pipe = PhoenixGetAshPipe(PhoenixGetCurrentAsh(), Port);
	}

	/* Sanitize the pipe */
	if (Pipe == NULL) {
		return OsError;
	}

	/* Read */
	return (PipeRead(Pipe, Container, Length, Peek) > 0) ? OsNoError : OsError;
}

/* ScPipeWrite
 * Sends a message to another process, 
 * so far this system call is made in the fashion
 * that the recieving process must have room in their
 * message queue... dunno */
OsStatus_t ScPipeWrite(UUId_t AshId, int Port, uint8_t *Message, size_t Length)
{
	/* Variables */
	MCorePipe_t *Pipe = NULL;

	/* Santizie the parameters */
	if (Message == NULL
		|| Length == 0) {
		return OsError;
	}

	/* Lookup the pipe for the given port */
	if (Port == -1
		&& ThreadingGetThread(AshId) != NULL) {
		Pipe = ThreadingGetThread(AshId)->Pipe;
	}
	else {
		Pipe = PhoenixGetAshPipe(PhoenixGetAsh(AshId), Port);
	}

	/* Sanitize the pipe */
	if (Pipe == NULL) {
		return OsError;
	}

	/* Write */
	return (PipeWrite(Pipe, Message, Length) > 0) ? OsNoError : OsError;
}

/* ScIpcSleep
 * This is a bit of a tricky synchronization method
 * and should always be used with care and WITH the timeout
 * since it could hang a process */
OsStatus_t ScIpcSleep(size_t Timeout)
{
	/* Locate Process */
	MCoreAsh_t *Ash = PhoenixGetCurrentAsh();

	/* Should never happen this 
	 * Only threads associated with processes
	 * can call this */
	if (Ash == NULL) {
		return OsError;
	}

	/* Sleep on process handle */
	SchedulerSleepThread((uintptr_t*)Ash, Timeout);
	IThreadYield();

	/* Now we reach this when the timeout is 
	 * is triggered or another process wakes us */
	return OsNoError;
}

/* ScIpcWake
 * This must be used in conjuction with the above function
 * otherwise this function has no effect, this is used for
 * very limited IPC synchronization */
OsStatus_t ScIpcWake(UUId_t Target)
{
	/* Locate Process */
	MCoreAsh_t *Ash = PhoenixGetAsh(Target);

	/* Sanity */
	if (Ash == NULL) {
		return OsError;
	}

	/* Send a wakeup signal */
	SchedulerWakeupOneThread((uintptr_t*)Ash);

	/* Now we should have waked up the waiting process */
	return OsNoError;
}

/* ScRpcResponse
 * Waits for IPC RPC request to finish 
 * by polling the default pipe for a rpc-response */
OsStatus_t ScRpcResponse(MRemoteCall_t *Rpc)
{
	/* Variables */
	MCoreAsh_t *Ash = NULL;
	MCorePipe_t *Pipe = NULL;
	size_t ToRead = Rpc->Result.Length;

	/* There can be a special case where 
	 * Sender == PHOENIX_NO_ASH 
	 * Use the builtin thread pipe */
	if (Rpc->Sender == UUID_INVALID) {
		Pipe = ThreadingGetCurrentThread(CpuGetCurrentId())->Pipe;
	}
	else {
		/* Resolve the current running process
		 * and the default pipe in the rpc */
		Ash = PhoenixGetAsh(ThreadingGetCurrentThread(CpuGetCurrentId())->AshId);
		Pipe = PhoenixGetAshPipe(Ash, Rpc->ResponsePort);

		/* Sanitize the lookups */
		if (Ash == NULL || Pipe == NULL
			|| Rpc->Result.Type == ARGUMENT_NOTUSED) {
			return OsError;
		}
	}

	/* Wait for data to enter the pipe */
	PipeWait(Pipe, 0);
	if ((size_t)PipeBytesAvailable(Pipe) < ToRead) {
		ToRead = PipeBytesAvailable(Pipe);
	}

	/* Read the data into the response-buffer */
	PipeRead(Pipe, (uint8_t*)Rpc->Result.Data.Buffer, ToRead, 0);

	/* Done, it finally ran! */
	return OsNoError;
}

/* ScRpcExecute
 * Executes an IPC RPC request to the
 * given process and optionally waits for
 * a reply/response */
OsStatus_t ScRpcExecute(MRemoteCall_t *Rpc, UUId_t Target, int Async)
{
	// Variables
	MCorePipe_t *Pipe = NULL;
	MCoreAsh_t *Ash = NULL;
	int i = 0;

	// Start out by resolving both the
	// process and pipe
	Ash = PhoenixGetAsh(Target);
	Pipe = PhoenixGetAshPipe(Ash, Rpc->Port);

	// Sanitize the lookups
	if (Ash == NULL || Pipe == NULL) {
		LogFatal("SYSC", "Either target 0x%x or port %u did not exist in target",
			Target, Rpc->Port);
		return OsError;
	}

	// Install Sender
	Rpc->Sender = ThreadingGetCurrentThread(CpuGetCurrentId())->AshId;

	// Write the base request 
	// and then iterate arguments and write them
	PipeWrite(Pipe, (uint8_t*)Rpc, sizeof(MRemoteCall_t));
	for (i = 0; i < IPC_MAX_ARGUMENTS; i++) {
		if (Rpc->Arguments[i].Type == ARGUMENT_BUFFER) {
			PipeWrite(Pipe, (uint8_t*)Rpc->Arguments[i].Data.Buffer, 
				Rpc->Arguments[i].Length);
		}
	}

	// Async request? Because if yes, don't
	// wait for response
	if (Async) {
		return OsNoError;
	}

	// Ok, wait for response
	return ScRpcResponse(Rpc);
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
 * Queries basic acpi information and returns either OsNoError
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
		return OsNoError;
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
	return OsNoError;
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
	return OsNoError;
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
	return (*Interrupt == INTERRUPT_NONE) ? OsError : OsNoError;
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
OsStatus_t ScRegisterAliasId(UUId_t Alias)
{
	// Redirect call to phoenix
	return PhoenixRegisterAlias(
		PhoenixGetCurrentAsh(), Alias);
}

/* ScLoadDriver
 * Attempts to resolve the best possible drive for
 * the given device information */
OsStatus_t ScLoadDriver(MCoreDevice_t *Device)
{
	/* Variables */
	MCorePhoenixRequest_t *Request = NULL;
	MCoreServer_t *Server = NULL;
	MCoreModule_t *Module = NULL;
	MRemoteCall_t Message;
	MString_t *Path = NULL;

	/* Sanitize information */
	if (Device == NULL) {
		return OsError;
	}

	/* First of all, if a server has already been spawned
	 * for the specific driver, then call it's RegisterInstance */
	Server = PhoenixGetServerByDriver(Device->VendorId, Device->DeviceId,
		Device->Class, Device->Subclass);

	/* Sanitize the lookup 
	 * If it's not found, spawn server */
	if (Server == NULL)
	{
		/* Lookup specific driver */
		Module = ModulesFindSpecific(Device->VendorId, Device->DeviceId);

		/* Lookup generic driver if it failed */
		if (Module == NULL) {
			Module = ModulesFindGeneric(Device->Class, Device->Subclass);
		}

		/* Return error if that failed */
		if (Module == NULL) {
			return OsError;
		}

		/* Build Path */
		Path = MStringCreate("rd:/", StrUTF8);
		MStringAppendString(Path, Module->Name);

		/* Create a phoenix request */
		Request = (MCorePhoenixRequest_t*)kmalloc(sizeof(MCorePhoenixRequest_t));
		memset(Request, 0, sizeof(MCorePhoenixRequest_t));
		Request->Base.Type = AshSpawnServer;

		/* Set our parameters as well */
		Request->Path = Path;
		Request->Arguments.Raw.Data = kmalloc(sizeof(MCoreDevice_t));
		Request->Arguments.Raw.Length = sizeof(MCoreDevice_t);

		/* Copy data */
		memcpy(Request->Arguments.Raw.Data, Device, sizeof(MCoreDevice_t));

		/* Send off the request */
		PhoenixCreateRequest(Request);
		PhoenixWaitRequest(Request, 0);

		/* Lookup server */
		Server = PhoenixGetServer(Request->AshId);

		/* Sanity */
		assert(Server != NULL);

		/* Cleanup resources */
		MStringDestroy(Request->Path);
		kfree(Request->Arguments.Raw.Data);
		kfree(Request);

		/* Update server params */
		Server->VendorId = Device->VendorId;
		Server->DeviceId = Device->DeviceId;
		Server->DeviceClass = Device->Class;
		Server->DeviceSubClass = Device->Subclass;
	}

	/* Prepare the message */
	RPCInitialize(&Message, 1, PIPE_RPCOUT, __DRIVER_REGISTERINSTANCE);
	RPCSetArgument(&Message, 0, Device, sizeof(MCoreDevice_t));
	Message.Sender = ThreadingGetCurrentThread(CpuGetCurrentId())->AshId;

	/* Wait for the driver to open it's
	 * communication pipe */
	PhoenixWaitAshPipe(&Server->Base, PIPE_RPCOUT);

	/* Done! */
	return ScRpcExecute(&Message, Server->Base.Id, 1);
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
		|| (Flags & (INTERRUPT_KERNEL | INTERRUPT_SOFTWARE))) {
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

/* ScAcknowledgeInterrupt 
 * Acknowledges the interrupt source and unmasks
 * the interrupt-line, allowing another interrupt
 * to occur for the given driver */
OsStatus_t ScAcknowledgeInterrupt(UUId_t Source)
{
	return InterruptAcknowledge(Source);
}

/* ScRegisterSystemTimer
 * Registers the given interrupt source as a system
 * timer source, with the given tick. This way the system
 * can always keep track of timers */
OsStatus_t ScRegisterSystemTimer(UUId_t Interrupt, size_t NsPerTick)
{
	return TimersRegister(Interrupt, NsPerTick);
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
	/* Log it */
	LogDebug("SYST", "Ending console session");

	/* Redirect */
	LogRedirect(LogFile);

	/* Done */
	return 0;
}

/* System (Environment) Query 
 * This function allows the user to query 
 * information about cpu, memory, stats etc */
int ScEnvironmentQuery(void)
{
	return 0;
}

/* NoOperation
 * Empty Operation, mostly
 * because the operation is reserved */
int NoOperation(void)
{
	return 0;
}

/* Syscall Table */
uintptr_t GlbSyscallTable[91] =
{
	/* Kernel Log */
	DefineSyscall(ScSystemDebug),

	/* Process Functions - 1 */
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

	/* Threading Functions - 11 */
	DefineSyscall(ScThreadCreate),
	DefineSyscall(ScThreadExit),
	DefineSyscall(ScThreadKill),
	DefineSyscall(ScThreadJoin),
	DefineSyscall(ScThreadSleep),
	DefineSyscall(ScThreadYield),
	DefineSyscall(ScThreadGetCurrentId),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),

	/* Synchronization Functions - 21 */
	DefineSyscall(ScConditionCreate),
	DefineSyscall(ScConditionDestroy),
	DefineSyscall(ScSyncSleep),
	DefineSyscall(ScSyncWakeUp),
	DefineSyscall(ScSyncWakeUpAll),
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
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),

	/* System Functions - 51 */
	DefineSyscall(ScEndBootSequence),
	DefineSyscall(NoOperation),
	DefineSyscall(ScEnvironmentQuery),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
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
	DefineSyscall(ScAcknowledgeInterrupt),
	DefineSyscall(ScRegisterSystemTimer),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation)
};
