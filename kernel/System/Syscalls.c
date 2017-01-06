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
#include <Arch.h>
#include <Modules/Process.h>
#include <Threading.h>
#include <Scheduler.h>
#include <Heap.h>
#include <Timers.h>
#include <Log.h>

/* Includes
 * - Library */
#include <assert.h>
#include <stddef.h>
#include <os/ipc/ipc.h>
#include <ds/list.h>
#include <ds/mstring.h>
#include <string.h>

/* Shorthand */
#define DefineSyscall(_Sys) ((Addr_t)&_Sys)

/***********************
 * Process Functions   *
 ***********************/

/* Spawns a new process with the
 * given executable + arguments 
 * and returns the id, will return
 * PROCESS_NO_PROCESS if failed */
PhxId_t ScProcessSpawn(char *Path, char *Arguments)
{
	/* Alloc on stack */
	MCorePhoenixRequest_t Request;

	/* Convert to MSTrings */
	MString_t *mPath = NULL;
	MString_t *mArguments = NULL;

	/* Sanity */
	if (Path == NULL)
		return PROCESS_NO_PROCESS;

	/* Allocate the mstrings */
	mPath = MStringCreate(Path, StrUTF8);
	mArguments = (Arguments == NULL) ? NULL : MStringCreate(Arguments, StrUTF8);

	/* Clean out structure */
	memset(&Request, 0, sizeof(MCorePhoenixRequest_t));

	/* Setup */
	Request.Base.Type = AshSpawnProcess;
	Request.Path = mPath;
	Request.Arguments = mArguments;
	
	/* Fire! */
	PhoenixCreateRequest(&Request);
	PhoenixWaitRequest(&Request, 0);

	/* Cleanup */
	MStringDestroy(mPath);

	/* Only cleanup arguments if not null */
	if (mArguments != NULL)
		MStringDestroy(mArguments);

	/* Done */
	return Request.AshId;
}

/* This waits for a child process to 
 * finish executing, and does not wakeup
 * before that */
int ScProcessJoin(PhxId_t ProcessId)
{
	/* Wait for process */
	MCoreProcess_t *Process = PhoenixGetProcess(ProcessId);

	/* Sanity */
	if (Process == NULL)
		return -1;

	/* Sleep */
	SchedulerSleepThread((Addr_t*)Process, 0);
	IThreadYield();

	/* Return the exit code */
	return Process->ReturnCode;
}

/* Attempts to kill the process 
 * with the given process-id */
int ScProcessKill(PhxId_t ProcessId)
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
		return 0;
	else
		return -1;
}

/* Kills the current process with the 
 * error code given as argument */
int ScProcessExit(int ExitCode)
{
	/* Retrieve crrent process */
	MCoreProcess_t *Process = PhoenixGetProcess(PROCESS_CURRENT);
	IntStatus_t IntrState;

	/* Sanity 
	 * We don't proceed in case it's not a process */
	if (Process == NULL) {
		return -1;
	}

	/* Log it and save return code */
	LogDebug("SYSC", "Process %s terminated with code %i", 
		MStringRaw(Process->Base.Name), ExitCode);
	Process->ReturnCode = ExitCode;

	/* Disable interrupts before proceeding */
	IntrState = InterruptDisable();

	/* Terminate all threads used by process */
	ThreadingTerminateAshThreads(Process->Base.Id);

	/* Mark process for reaping */
	PhoenixTerminateAsh(&Process->Base);

	/* Enable Interrupts */
	InterruptRestoreState(IntrState);

	/* Kill this thread */
	ThreadingKillThread(ThreadingGetCurrentThreadId());

	/* Yield */
	IThreadYield();

	/* Done */
	return 0;
}

/* Queries information about 
 * the given process id, if called
 * with -1 it queries information
 * about itself */
int ScProcessQuery(PhxId_t ProcessId, AshQueryFunction_t Function, void *Buffer, size_t Length)
{
	/* Variables */
	MCoreProcess_t *Process = NULL;

	/* Sanity arguments */
	if (Buffer == NULL
		|| Length == 0) {
		return -1;
	}

	/* Lookup process */
	Process = PhoenixGetProcess(ProcessId);

	/* Sanity, found? */
	if (Process == NULL) {
		return -2;
	}

	/* Deep Call */
	return PhoenixQueryAsh(&Process->Base, Function, Buffer, Length);
}

/* Installs a signal handler for 
 * the given signal number, it's then invokable
 * by other threads/processes etc */
int ScProcessSignal(int Signal, Addr_t Handler) 
{
	/* Variables */
	MCoreProcess_t *Process = NULL;

	/* Sanitize our params */
	if (Signal > NUMSIGNALS) {
		return -1;
	}

	/* Lookup process */
	Process = PhoenixGetProcess(PROCESS_CURRENT);

	/* Sanity... 
	 * This should never happen though
	 * Only I write code that has no process */
	if (Process == NULL) {
		return -1;
	}

	/* Always retrieve the old handler 
	 * and return it, so temp store it before updating */
	Addr_t OldHandler = Process->Base.Signals.Handlers[Signal];
	Process->Base.Signals.Handlers[Signal] = Handler;

	/* Done, return the old ! */
	return (int)OldHandler;
}

/* Dispatches a signal to the target process id 
 * It will get handled next time it's selected for execution 
 * so we yield instantly as well. If processid is -1, we select self */
int ScProcessRaise(PhxId_t ProcessId, int Signal)
{
	/* Variables */
	MCoreProcess_t *Process = NULL;

	/* Sanitize our params */
	if (Signal > NUMSIGNALS) {
		return -1;
	}

	/* Lookup process */
	Process = PhoenixGetProcess(ProcessId);

	/* Sanity...
	 * This should never happen though
	 * Only I write code that has no process */
	if (Process == NULL) {
		return -1;
	}

	/* Simply create a new signal 
	 * and return it's value */
	if (SignalCreate(ProcessId, Signal))
		return -1;
	else {
		IThreadYield();
		return 0;
	}
}

/**************************
* Shared Object Functions *
***************************/

/* Load a shared object given a path 
 * path must exists otherwise NULL is returned */
void *ScSharedObjectLoad(const char *SharedObject)
{
	/* Locate Process */
	MCoreProcess_t *Process = PhoenixGetProcess(PROCESS_CURRENT);
	Addr_t BaseAddress = 0;
	
	/* Vars for solving library */
	void *Handle = NULL;

	/* Sanity */
	if (Process == NULL)
		return NULL;

	/* Construct a mstring */
	MString_t *Path = MStringCreate((void*)SharedObject, StrUTF8);

	/* Resolve Library */
	BaseAddress = Process->Base.NextLoadingAddress;
	Handle = PeResolveLibrary(Process->Base.Executable, NULL, Path, &BaseAddress);
	Process->Base.NextLoadingAddress = BaseAddress;

	/* Cleanup Buffers */
	MStringDestroy(Path);

	/* Done */
	return Handle;
}

/* Load a function-address given an shared object
 * handle and a function name, function must exist
 * otherwise null is returned */
Addr_t ScSharedObjectGetFunction(void *Handle, const char *Function)
{
	/* Validate */
	if (Handle == NULL
		|| Function == NULL)
		return 0;

	/* Try to resolve function */
	return PeResolveFunction((MCorePeFile_t*)Handle, Function);
}

/* Unloads a valid shared object handle
 * returns 0 on success */
int ScSharedObjectUnload(void *Handle)
{
	/* Locate Process */
	MCoreProcess_t *Process = PhoenixGetProcess(PROCESS_CURRENT);

	/* Sanity */
	if (Process == NULL)
		return -1;

	/* Do the unload */
	PeUnloadLibrary(Process->Base.Executable, (MCorePeFile_t*)Handle);

	/* Done! */
	return 0;
}

/***********************
* Threading Functions  *
***********************/

/* Creates a new thread bound to 
 * the calling process, with the given
 * entry point and arguments */
int ScThreadCreate(ThreadEntry_t Entry, void *Data, int Flags)
{
	/* Sanity */
	if (Entry == NULL)
		return -1;

	/* Deep Call */
	return (int)ThreadingCreateThread(NULL, Entry, Data, 
		Flags | THREADING_USERMODE | THREADING_INHERIT);
}

/* Exits the current thread and 
 * instantly yields control to scheduler */
int ScThreadExit(int ExitCode)
{
	/* Deep Call */
	ThreadingExitThread(ExitCode);

	/* We will never reach this 
	 * statement */
	return 0;
}

/* Thread join, waits for a given
 * thread to finish executing, and
 * returns it's exit code, works 
 * like processjoin. Must be in same
 * process as asking thread */
int ScThreadJoin(ThreadId_t ThreadId)
{
	/* Lookup process information */
	PhxId_t CurrentPid = ThreadingGetCurrentThread(ApicGetCpu())->AshId;

	/* Sanity */
	if (ThreadingGetThread(ThreadId) == NULL
		|| ThreadingGetThread(ThreadId)->AshId != CurrentPid)
		return -1;

	/* Simply deep call again 
	 * the function takes care 
	 * of validation as well */
	return ThreadingJoinThread(ThreadId);
}

/* Thread kill, kills the given thread
 * id, must belong to same process as the
 * thread that asks. */
int ScThreadKill(ThreadId_t ThreadId)
{
	/* Lookup process information */
	PhxId_t CurrentPid = ThreadingGetCurrentThread(ApicGetCpu())->AshId;

	/* Sanity */
	if (ThreadingGetThread(ThreadId) == NULL
		|| ThreadingGetThread(ThreadId)->AshId != CurrentPid)
		return -1;

	/* Ok, we can kill it */
	ThreadingKillThread(ThreadId);

	/* Done! */
	return 0;
}

/* Thread sleep,
 * Sleeps the current thread for the
 * given milliseconds. */
int ScThreadSleep(size_t MilliSeconds)
{
	/* Deep Call */
	SleepMs(MilliSeconds);

	/* Done, this call never fails */
	return 0;
}

/* Thread get current id
 * Get's the current thread id */
int ScThreadGetCurrentId(void)
{
	/* Deep Call */
	return (int)ThreadingGetCurrentThreadId();
}

/* This yields the current thread 
 * and gives cpu time to another thread */
int ScThreadYield(void)
{
	/* Deep Call */
	IThreadYield();

	/* Done */
	return 0;
}

/***********************
* Synch Functions      *
***********************/

/* Create a new shared handle 
 * that is unique for a condition
 * variable */
Addr_t ScConditionCreate(void)
{
	/* Allocate an int or smthing */
	return (Addr_t)kmalloc(sizeof(int));
}

/* Destroys a shared handle
 * for a condition variable */
int ScConditionDestroy(Addr_t *Handle)
{
	/* Free handle */
	kfree(Handle);

	/* Done */
	return 0;
}

/* Signals a handle for wakeup 
 * This is primarily used for condition
 * variables and semaphores */
int ScSyncWakeUp(Addr_t *Handle)
{
	/* Deep Call */
	return SchedulerWakeupOneThread(Handle);
}

/* Signals a handle for wakeup all
 * This is primarily used for condition
 * variables and semaphores */
int ScSyncWakeUpAll(Addr_t *Handle)
{
	/* Deep Call */
	SchedulerWakeupAllThreads(Handle);

	/* Done! */
	return 0;
}

/* Waits for a signal relating 
 * to the above function, this
 * function uses a timeout. Returns -1
 * if we timed-out, otherwise returns 0 */
int ScSyncSleep(Addr_t *Handle, size_t Timeout)
{
	/* Get current thread */
	MCoreThread_t *Current = ThreadingGetCurrentThread(ApicGetCpu());

	/* Sleep */
	SchedulerSleepThread(Handle, Timeout);
	IThreadYield();

	/* Sanity */
	if (Timeout != 0
		&& Current->Sleep == 0)
		return -1;
	else
		return 0;
}

/***********************
* Memory Functions     *
***********************/

/* Allows a process to allocate memory
 * from the userheap, it takes a size and 
 * allocation flags which describe the type of allocation */
Addr_t ScMemoryAllocate(size_t Size, Flags_t Flags)
{
	/* Locate the current running process */
	MCoreAsh_t *Ash = PhoenixGetAsh(PHOENIX_CURRENT);
	Addr_t AllocatedAddress = 0;

	/* Sanitize the process we looked up
	 * we want it to exist of course */
	if (Ash == NULL)
		return (Addr_t)-1;
	
	/* Now do the allocation in the user-bitmap 
	 * since memory is managed in userspace for speed */
	AllocatedAddress = BitmapAllocateAddress(Ash->Heap, Size);

	/* Sanitize the returned address */
	assert(AllocatedAddress != 0);

	/* Handle flags here */
	if (Flags & ALLOCATION_COMMIT) {
		/* Commit the pages */
	}

	/* Return the address */
	return AllocatedAddress;
}

/* Free's previous allocated memory, given an address
 * and a size (though not needed for now!) */
int ScMemoryFree(Addr_t Address, size_t Size)
{
	/* Locate Process */
	MCoreAsh_t *Ash = PhoenixGetAsh(PHOENIX_CURRENT);

	/* Sanitize the process we looked up
	 * we want it to exist of course */
	if (Ash == NULL)
		return (Addr_t)-1;

	/* Now do the deallocation in the user-bitmap 
	 * since memory is managed in userspace for speed */
	BitmapFreeAddress(Ash->Heap, Address, Size);

	/* Done */
	return 0;
}

/* Queries information about a chunk of memory 
 * and returns allocation information or stats 
 * depending on query function */
int ScMemoryQuery(void)
{
	return 0;
}

/* Share memory with a process */
Addr_t ScMemoryShare(IpcComm_t Target, Addr_t Address, size_t Size)
{
	/* Locate the current running process */
	MCoreAsh_t *Ash = PhoenixGetAsh(Target);
	size_t NumBlocks, i;

	/* Sanity */
	if (Ash == NULL
		|| Address == 0
		|| Size == 0)
		return 0;

	/* Start out by allocating memory 
	 * in target process's shared memory space */
	Addr_t Shm = BitmapAllocateAddress(Ash->Shm, Size);
	NumBlocks = DIVUP(Size, PAGE_SIZE);

	/* Sanity -> If we cross a page boundary */
	if (((Address + Size) & PAGE_MASK)
		!= (Address & PAGE_MASK))
		NumBlocks++;

	/* Sanity */
	assert(Shm != 0);

	/* Now we have to transfer our physical mappings 
	 * to their new virtual */
	for (i = 0; i < NumBlocks; i++) 
	{
		/* Adjust address */
		Addr_t AdjustedAddr = (Address & PAGE_MASK) + (i * PAGE_SIZE);
		Addr_t AdjustedShm = Shm + (i * PAGE_SIZE);
		Addr_t PhysicalAddr = 0;

		/* The address MUST be mapped in ours */
		if (!AddressSpaceGetMap(AddressSpaceGetCurrent(), AdjustedAddr))
			AddressSpaceMap(AddressSpaceGetCurrent(), AdjustedAddr, PAGE_SIZE, MEMORY_MASK_DEFAULT, ADDRESS_SPACE_FLAG_USER);
	
		/* Get physical mapping */
		PhysicalAddr = AddressSpaceGetMap(AddressSpaceGetCurrent(), AdjustedAddr);

		/* Map it directly into target process */
		AddressSpaceMapFixed(Ash->AddressSpace, PhysicalAddr,
			AdjustedShm, PAGE_SIZE, ADDRESS_SPACE_FLAG_USER | ADDRESS_SPACE_FLAG_VIRTUAL);
	}

	/* Done! */
	return Shm + (Address & ATTRIBUTE_MASK);
}

/* Unshare memory with a process */
int ScMemoryUnshare(IpcComm_t Target, Addr_t TranslatedAddress, size_t Size)
{
	/* Locate the current running process */
	MCoreAsh_t *Ash = PhoenixGetAsh(Target);
	size_t NumBlocks, i;

	/* Sanity */
	if (Ash == NULL)
		return -1;

	/* Calculate */
	NumBlocks = DIVUP(Size, PAGE_SIZE);

	/* Sanity -> If we cross a page boundary */
	if (((TranslatedAddress + Size) & PAGE_MASK)
		!= (TranslatedAddress & PAGE_MASK))
		NumBlocks++;

	/* Start out by unmapping their 
	 * memory in their address space */
	for (i = 0; i < NumBlocks; i++)
	{
		/* Adjust address */
		Addr_t AdjustedAddr = (TranslatedAddress & PAGE_MASK) + (i * PAGE_SIZE);

		/* Map it directly into target process */
		AddressSpaceUnmap(Ash->AddressSpace, AdjustedAddr, PAGE_SIZE);
	}

	/* Now unallocate it in their bitmap */
	BitmapFreeAddress(Ash->Shm, TranslatedAddress, Size);

	/* Done */
	return 0;
}

/***********************
* IPC Functions        *
***********************/
#include <InputManager.h>

/* Opens a new pipe for the calling Ash process
 * and allows communication to this port from other
 * processes */
int ScPipeOpen(int Port, Flags_t Flags)
{
	/* No need for any preperation on this call, the
	 * underlying call takes care of validation as well */
	return PhoenixOpenAshPipe(PhoenixGetAsh(PHOENIX_CURRENT), Port, Flags);
}

/* Closes an existing pipe on a given port and
 * shutdowns any communication on that port */
int ScPipeClose(int Port)
{
	/* No need for any preperation on this call, the
	 * underlying call takes care of validation as well */
	return PhoenixCloseAshPipe(PhoenixGetAsh(PHOENIX_CURRENT), Port);
}

/* Get the top message for this process
 * and consume the message, if no message 
 * is available, this function will block untill 
 * a message is available */
int ScPipeRead(int Port, uint8_t *Container, size_t Length, int Peek)
{
	/* Variables */
	MCorePipe_t *Pipe = NULL;

	/* Lookup the pipe for the given port */
	Pipe = PhoenixGetAshPipe(PhoenixGetAsh(PHOENIX_CURRENT), Port);

	/* Sanitize the pipe */
	if (Pipe == NULL) {
		return -2;
	}

	/* Read */
	return PipeRead(Pipe, Container, Length, Peek);
}

/* Sends a message to another process, 
 * so far this system call is made in the fashion
 * that the recieving process must have room in their
 * message queue... dunno */
int ScPipeWrite(PhxId_t AshId, int Port, uint8_t *Message, size_t Length)
{
	/* Variables */
	MCorePipe_t *Pipe = NULL;

	/* Santizie the parameters */
	if (Message == NULL
		|| Length == 0) {
		return -1;
	}

	/* Lookup the pipe for the given port */
	Pipe = PhoenixGetAshPipe(PhoenixGetAsh(AshId), Port);

	/* Sanitize the pipe */
	if (Pipe == NULL) {
		return -2;
	}

	/* Write */
	return PipeWrite(Pipe, Message, Length);
}

/* This is a bit of a tricky synchronization method
 * and should always be used with care and WITH the timeout
 * since it could hang a process */
int ScIpcSleep(size_t Timeout)
{
	/* Locate Process */
	MCoreAsh_t *Ash = PhoenixGetAsh(PHOENIX_CURRENT);

	/* Should never happen this 
	 * Only threads associated with processes
	 * can call this */
	if (Ash == NULL)
		return -1;

	/* Sleep on process handle */
	SchedulerSleepThread((Addr_t*)Ash, Timeout);
	IThreadYield();

	/* Now we reach this when the timeout is 
	 * is triggered or another process wakes us */
	return 0;
}

/* This must be used in conjuction with the above function
 * otherwise this function has no effect, this is used for
 * very limited IPC synchronization */
int ScIpcWake(IpcComm_t Target)
{
	/* Locate Process */
	MCoreAsh_t *Ash = PhoenixGetAsh(Target);

	/* Sanity */
	if (Ash == NULL)
		return -1;

	/* Send a wakeup signal */
	SchedulerWakeupOneThread((Addr_t*)Ash);

	/* Now we should have waked up the waiting process */
	return 0;
}

/* ScRpcResponse (System Call)
 * Waits for IPC RPC request to finish 
 * by polling the default pipe for a rpc-response */
OsStatus_t ScRpcResponse(MRemoteCall_t *Rpc)
{
	/* Variables */
	MCoreAsh_t *Ash = NULL;
	MCorePipe_t *Pipe = NULL;

	/* Resolve the current running process
	 * and the default pipe in the rpc */
	Ash = PhoenixGetAsh(ThreadingGetCurrentThread(ApicGetCpu())->AshId);
	Pipe = PhoenixGetAshPipe(Ash, Rpc->Port);

	/* Sanitize the lookups */
	if (Ash == NULL || Pipe == NULL
		|| Rpc->Result.InUse == 0) {
		return OsError;
	}

	/* Read the data into the response-buffer */
	PipeRead(Pipe, (uint8_t*)Rpc->Result.Buffer, Rpc->Result.Length, 0);

	/* Done, it finally ran! */
	return OsNoError;
}

/* ScRpcExecute (System Call)
 * Executes an IPC RPC request to the
 * given process and optionally waits for
 * a reply/response */
OsStatus_t ScRpcExecute(MRemoteCall_t *Rpc, IpcComm_t Target, int Async)
{
	/* Variables */
	MCoreAsh_t *Ash = NULL;
	MCorePipe_t *Pipe = NULL;
	int i = 0;

	/* Start out by resolving both the
	 * process and pipe */
	Ash = PhoenixGetAsh(Target);
	Pipe = PhoenixGetAshPipe(Ash, Rpc->Port);

	/* Sanitize the lookups */
	if (Ash == NULL || Pipe == NULL) {
		return OsError;
	}

	/* Install the sender */
	Rpc->Sender = ThreadingGetCurrentThread(ApicGetCpu())->AshId;

	/* Write the base request 
	 * and then iterate arguments and write them */
	PipeWrite(Pipe, (uint8_t*)Rpc, sizeof(MRemoteCall_t));
	for (i = 0; i < IPC_MAX_ARGUMENTS; i++) {
		if (Rpc->Arguments[i].InUse) {
			PipeWrite(Pipe, (uint8_t*)Rpc->Arguments[i].Buffer, Rpc->Arguments[i].Length);
		}
	}

	/* Async request? Because if yes, don't
	 * wait for response */
	if (Async) {
		return OsNoError;
	}

	/* Ok, wait for response */
	return ScRpcResponse(Rpc);
}

/* ScEvtExecute (System Call)
 * Executes an IPC EVT request to the
 * given process, does not give a reply */
OsStatus_t ScEvtExecute(MEventMessage_t *Event, IpcComm_t Target)
{
	/* Variables */
	MCoreAsh_t *Ash = NULL;
	MCorePipe_t *Pipe = NULL;
	int i = 0;

	/* Start out by resolving both the
	* process and pipe */
	Ash = PhoenixGetAsh(Target);
	Pipe = PhoenixGetAshPipe(Ash, Event->Port);

	/* Sanitize the lookups */
	if (Ash == NULL || Pipe == NULL) {
		return OsError;
	}

	/* Install the sender */
	Event->Sender = ThreadingGetCurrentThread(ApicGetCpu())->AshId;

	/* Write the base request
	* and then iterate arguments and write them */
	PipeWrite(Pipe, (uint8_t*)Event, sizeof(MEventMessage_t));
	for (i = 0; i < IPC_MAX_ARGUMENTS; i++) {
		if (Event->Arguments[i].InUse) {
			PipeWrite(Pipe, (uint8_t*)Event->Arguments[i].Buffer, Event->Arguments[i].Length);
		}
	}

	/* Ok, we are done! */
	return OsNoError;
}

/***********************
* VFS Functions        *
***********************/
#include <Vfs/Vfs.h>
#include <Server.h>
#include <stdio.h>
#include <errno.h>

/* ScVfsOpen 
 * System call edition of VFS Open File 
 * It does a lot of validation, but returns a filedescriptor Id */
int ScVfsOpen(const char *Utf8, VfsFileFlags_t OpenFlags, VfsErrorCode_t *ErrCode)
{
	/* Get current process */
	MCoreProcess_t *Process = PhoenixGetProcess(PROCESS_CURRENT);
	MCoreFileInstance_t *Handle = NULL;
	MCoreVfsRequest_t *Request = NULL;
	DataKey_t Key;

	/* Should never happen this
	 * Only threads associated with processes
	 * can call this */
	if (Process == NULL 
		|| Utf8 == NULL)
		return -1;

	/* Create a new request with the VFS
	 * Ask it to open the file */
	Request = (MCoreVfsRequest_t*)kmalloc(sizeof(MCoreVfsRequest_t));

	/* Reset request */
	memset(Request, 0, sizeof(MCoreVfsRequest_t));

	/* Setup base request */
	Request->Base.Type = VfsRequestOpenFile;

	/* Setup params for the request 
	 * we allocate a copy of the string in kernel memory
	 * so everyone can access it */
	Request->Pointer.Path = strdup(Utf8);
	Request->Value.Lo.Flags = OpenFlags;

	/* Send the request */
	VfsRequestCreate(Request);
	VfsRequestWait(Request, 0);

	/* Store handle */
	Handle = Request->Pointer.Handle;

	/* Cleanup */
	kfree((void*)Request->Pointer.Path);
	kfree(Request);

	/* Add to process list of open files */
	Key.Value = Handle->Id;
	ListAppend(Process->OpenFiles, ListCreateNode(Key, Key, Handle));

	/* Save error code */
	if (ErrCode != NULL)
		*ErrCode = Handle->Code;

	/* Done! */
	return Handle->Id;
}

/* ScVfsClose 
 * System call edition of VFS Close File 
 * Validates its a real descriptor, and cleans up resources */
int ScVfsClose(int FileDescriptor)
{
	/* Get current process */
	MCoreProcess_t *Process = PhoenixGetProcess(PROCESS_CURRENT);
	VfsErrorCode_t RetCode = VfsInvalidParameters;
	MCoreVfsRequest_t *Request = NULL;
	DataKey_t Key;
	Key.Value = FileDescriptor;

	/* Should never happen this
	 * Only threads associated with processes
	 * can call this */
	if (Process == NULL)
		return RetCode;

	/* Lookup file handle */
	ListNode_t *fNode = ListGetNodeByKey(Process->OpenFiles, Key, 0);
	
	/* Sanity */
	if (fNode == NULL)
		return RetCode;

	/* Remove from open files */
	ListRemoveByNode(Process->OpenFiles, fNode);

	/* Create a new request with the VFS
	 * Ask it to cleanup the file */
	Request = (MCoreVfsRequest_t*)kmalloc(sizeof(MCoreVfsRequest_t));

	/* Reset request */
	memset(Request, 0, sizeof(MCoreVfsRequest_t));

	/* Setup base request */
	Request->Base.Type = VfsRequestCloseFile;

	/* Setup params for the request */
	Request->Pointer.Handle = (MCoreFileInstance_t*)fNode->Data;

	/* Send the request */
	VfsRequestCreate(Request);
	VfsRequestWait(Request, 0);

	/* Store code */
	RetCode = Request->Error;

	/* Cleanup */
	kfree(Request);

	/* free node */
	kfree(fNode);

	/* Deep Call */
	return (int)RetCode;
}

/* ScVfsRead  
 * System call edition of VFS Read File,
 * It tries to read the requested amount of bytes,
 * and returns the actual byte amount read */
size_t ScVfsRead(int FileDescriptor, uint8_t *Buffer, size_t Length, VfsErrorCode_t *ErrCode)
{
	/* Get current process */
	MCoreProcess_t *Process = PhoenixGetProcess(PROCESS_CURRENT);
	VfsErrorCode_t RetCode = VfsInvalidParameters;
	MCoreVfsRequest_t *Request = NULL;
	size_t bRead = 0;
	DataKey_t Key;

	/* Should never happen this
	 * Only threads associated with processes
	 * can call this */
	if (Process == NULL || Buffer == NULL)
		goto done;

	/* Sanitize length */
	if (Length == 0) {
		RetCode = VfsOk;
		goto done;
	}

	/* Lookup file handle */
	Key.Value = FileDescriptor;
	ListNode_t *fNode = ListGetNodeByKey(Process->OpenFiles, Key, 0);

	/* Sanity */
	if (fNode == NULL)
		goto done;

	/* Create a new request with the VFS
	 * Ask it to read the file */
	Request = (MCoreVfsRequest_t*)kmalloc(sizeof(MCoreVfsRequest_t));

	/* Reset request */
	memset(Request, 0, sizeof(MCoreVfsRequest_t));

	/* Setup base request */
	Request->Base.Type = VfsRequestReadFile;

	/* Setup params for the request */
	Request->Pointer.Handle = (MCoreFileInstance_t*)fNode->Data;
	Request->Buffer = (uint8_t*)ServerMemoryAllocate(Length);
	Request->Value.Lo.Length = Length;

	/* Send the request */
	VfsRequestCreate(Request);
	VfsRequestWait(Request, 0);

	/* Copy data from proxy to buffer */
	memcpy((void*)Buffer, (const void*)Request->Buffer, Length);
	ServerMemoryFree((void*)Request->Buffer);

	/* Store bytes read */
	bRead = Request->Value.Hi.Length;
	RetCode = ((MCoreFileInstance_t*)fNode->Data)->Code;

	/* Cleanup */
	kfree(Request);

done:
	/* Save error code */
	if (ErrCode != NULL)
		*ErrCode = RetCode;

	/* Done */
	return bRead;
}

/* ScVfsWrite
 * System call edition of VFS Write File,
 * It tries to write the requested amount of bytes,
 * and returns the actual byte amount written */
size_t ScVfsWrite(int FileDescriptor, uint8_t *Buffer, size_t Length, VfsErrorCode_t *ErrCode)
{
	/* Get current process */
	MCoreProcess_t *Process = PhoenixGetProcess(PROCESS_CURRENT);
	VfsErrorCode_t RetCode = VfsInvalidParameters;
	MCoreVfsRequest_t *Request = NULL;
	size_t bWritten = 0;
	DataKey_t Key;
	Key.Value = FileDescriptor;

	/* Should never happen this
	 * Only threads associated with processes
	 * can call this */
	if (Process == NULL || Buffer == NULL)
		goto done;

	/* Sanitize length */
	if (Length == 0) {
		RetCode = VfsOk;
		goto done;
	}

	/* Lookup file handle */
	ListNode_t *fNode = ListGetNodeByKey(Process->OpenFiles, Key, 0);

	/* Sanity */
	if (fNode == NULL)
		goto done;

	/* Create a new request with the VFS
	 * Ask it to write the file */
	Request = (MCoreVfsRequest_t*)kmalloc(sizeof(MCoreVfsRequest_t));

	/* Reset request */
	memset(Request, 0, sizeof(MCoreVfsRequest_t));

	/* Setup base request */
	Request->Base.Type = VfsRequestWriteFile;

	/* Setup params for the request */
	Request->Pointer.Handle = (MCoreFileInstance_t*)fNode->Data;
	Request->Buffer = ServerMemoryAllocate(Length);
	Request->Value.Lo.Length = Length;

	/* Copy data from proxy to buffer */
	memcpy((void*)Request->Buffer, (const void*)Buffer, Length);

	/* Send the request */
	VfsRequestCreate(Request);
	VfsRequestWait(Request, 0);

	/* Cleanup proxy */
	ServerMemoryFree((void*)Request->Buffer);

	/* Store bytes read */
	bWritten = Request->Value.Hi.Length;
	RetCode = ((MCoreFileInstance_t*)fNode->Data)->Code;

	/* Cleanup */
	kfree(Request);

done:
	/* Save error code */
	if (ErrCode != NULL)
		*ErrCode = RetCode;

	/* Done */
	return bWritten;
}

/* Seek in File Descriptor
 * This function takes a file-descriptor (id) and 
 * a position split up into high/low parts so we can
 * support large files even on 32 bit */
int ScVfsSeek(int FileDescriptor, off_t PositionLow, off_t PositionHigh)
{
	/* Get current process */
	MCoreProcess_t *Process = PhoenixGetProcess(PROCESS_CURRENT);
	VfsErrorCode_t RetCode = VfsInvalidParameters;
	MCoreVfsRequest_t *Request = NULL;
	DataKey_t Key;
	Key.Value = FileDescriptor;

	/* Should never happen this
	* Only threads associated with processes
	* can call this */
	if (Process == NULL)
		return RetCode;

	/* Lookup file handle */
	ListNode_t *fNode = ListGetNodeByKey(Process->OpenFiles, Key, 0);

	/* Sanity */
	if (fNode == NULL)
		return RetCode;

	/* Create a new request with the VFS
	 * Ask it to seek in the file */
	Request = (MCoreVfsRequest_t*)kmalloc(sizeof(MCoreVfsRequest_t));

	/* Reset request */
	memset(Request, 0, sizeof(MCoreVfsRequest_t));

	/* Setup base request */
	Request->Base.Type = VfsRequestSeekFile;

	/* Setup params for the request */
	Request->Pointer.Handle = (MCoreFileInstance_t*)fNode->Data;
	Request->Value.Lo.Length = PositionLow;
	Request->Value.Hi.Length = PositionHigh;

	/* Send the request */
	VfsRequestCreate(Request);
	VfsRequestWait(Request, 0);

	/* Store error */
	RetCode = Request->Error;

	/* Cleanup */
	kfree(Request);

	/* Done, return error code */
	return (int)RetCode;
}

/* ScVfsDelete 
 * System call edition of VFS Delete File 
 * It does a lot of validation on the FD
 * and deletes the file, it's important to close
 * the file-descriptor afterwards as it's invalidated */
int ScVfsDelete(int FileDescriptor)
{
	/* Get current process */
	MCoreProcess_t *Process = PhoenixGetProcess(PROCESS_CURRENT);
	VfsErrorCode_t RetCode = VfsInvalidParameters;
	MCoreVfsRequest_t *Request = NULL;
	DataKey_t Key;
	Key.Value = FileDescriptor;

	/* Should never happen this
	* Only threads associated with processes
	* can call this */
	if (Process == NULL)
		return RetCode;

	/* Lookup file handle */
	ListNode_t *fNode = ListGetNodeByKey(Process->OpenFiles, Key, 0);

	/* Sanity */
	if (fNode == NULL)
		return RetCode;

	/* Create a new request with the VFS
	 * Ask it to delete the file */
	Request = (MCoreVfsRequest_t*)kmalloc(sizeof(MCoreVfsRequest_t));

	/* Reset request */
	memset(Request, 0, sizeof(MCoreVfsRequest_t));

	/* Setup base request */
	Request->Base.Type = VfsRequestDeleteFile;

	/* Setup params for the request */
	Request->Pointer.Handle = (MCoreFileInstance_t*)fNode->Data;

	/* Send the request */
	VfsRequestCreate(Request);
	VfsRequestWait(Request, 0);

	/* Store error */
	RetCode = Request->Error;

	/* Cleanup */
	kfree(Request);

	/* Done, return error code */
	return (int)RetCode;
}

/* ScVfsFlush 
 * System call edition of VFS Flush File 
 * Currently it just flushes the out-buffer
 * as in-buffer is buffered in userspace */
int ScVfsFlush(int FileDescriptor)
{
	/* Get current process */
	MCoreProcess_t *Process = PhoenixGetProcess(PROCESS_CURRENT);
	VfsErrorCode_t RetCode = VfsInvalidParameters;
	MCoreVfsRequest_t *Request = NULL;
	DataKey_t Key;
	Key.Value = FileDescriptor;

	/* Should never happen this
	* Only threads associated with processes
	* can call this */
	if (Process == NULL)
		return RetCode;

	/* Lookup file handle */
	ListNode_t *fNode = ListGetNodeByKey(Process->OpenFiles, Key, 0);

	/* Sanity */
	if (fNode == NULL)
		return RetCode;

	/* Create a new request with the VFS
	 * Ask it to flush the file */
	Request = (MCoreVfsRequest_t*)kmalloc(sizeof(MCoreVfsRequest_t));

	/* Reset request */
	memset(Request, 0, sizeof(MCoreVfsRequest_t));

	/* Setup base request */
	Request->Base.Type = VfsRequestFlushFile;

	/* Setup params for the request */
	Request->Pointer.Handle = (MCoreFileInstance_t*)fNode->Data;

	/* Send the request */
	VfsRequestCreate(Request);
	VfsRequestWait(Request, 0);

	/* Store error */
	RetCode = Request->Error;

	/* Cleanup */
	kfree(Request);

	/* Done, return error code */
	return (int)RetCode;
}

/* Query information about 
 * a file handle or directory handle */
int ScVfsQuery(int FileDescriptor, VfsQueryFunction_t Function, void *Buffer, size_t Length)
{
	/* Get current process */
	MCoreProcess_t *Process = PhoenixGetProcess(PROCESS_CURRENT);
	VfsErrorCode_t RetCode = VfsInvalidParameters;
	void *Proxy = NULL;
	DataKey_t Key;
	Key.Value = FileDescriptor;

	/* Should never happen this
	* Only threads associated with processes
	* can call this */
	if (Process == NULL
		|| Buffer == NULL)
		return RetCode;

	/* Lookup file handle */
	ListNode_t *fNode = ListGetNodeByKey(Process->OpenFiles, Key, 0);

	/* Sanity */
	if (fNode == NULL)
		return RetCode;

	/* Allocate a proxy buffer */
	Proxy = ServerMemoryAllocate(Length);

	/* Redirect to Vfs */
	RetCode = VfsQuery((MCoreFileInstance_t*)fNode->Data, Function, Proxy, Length);

	/* Copy */
	memcpy(Buffer, Proxy, Length);

	/* Cleanup proxy */
	ServerMemoryFree(Proxy);

	/* Done! */
	return (int)RetCode;
}

/* The file move operation 
 * this function copies Source -> destination
 * or moves it, deleting the Source. */
int ScVfsMove(const char *Source, const char *Destination, int Copy)
{
	/* Variables */
	MCoreVfsRequest_t *Request = NULL;
	VfsErrorCode_t RetCode = VfsInvalidParameters;

	/* Sanity */
	if (Source == NULL || Destination == NULL)
		return -1;

	/* Create a new request with the VFS
	 * Ask it to move the file */
	Request = (MCoreVfsRequest_t*)kmalloc(sizeof(MCoreVfsRequest_t));

	/* Reset request */
	memset(Request, 0, sizeof(MCoreVfsRequest_t));

	/* Setup base request */
	Request->Base.Type = VfsRequestDeleteFile; //TODO;

	/* Setup params for the request */
	Request->Pointer.Path = Source;
	Request->Buffer = (uint8_t*)Destination;
	Request->Value.Lo.Copy = Copy;

	/* Send the request */
	VfsRequestCreate(Request);
	VfsRequestWait(Request, 0);

	/* Store error */
	RetCode = Request->Error;

	/* Cleanup */
	kfree(Request);

	/* Done, return error code */
	return (int)RetCode;
}

/* Vfs - Resolve Environmental Path
 * Resolves the environmental type
 * to an valid absolute path */
int ScVfsResolvePath(int EnvPath, char *StrBuffer)
{
	/* Result String */
	MString_t *ResolvedPath = NULL;

	/* Sanity */
	if (EnvPath < 0 || EnvPath >= (int)PathEnvironmentCount)
		EnvPath = 0;

	/* Resolve it */
	ResolvedPath = VfsResolveEnvironmentPath((VfsEnvironmentPath_t)EnvPath);

	/* Sanity */
	if (ResolvedPath == NULL)
		return -1;

	/* Copy it to user-buffer */
	memcpy(StrBuffer, MStringRaw(ResolvedPath), MStringSize(ResolvedPath));

	/* Cleanup */
	MStringDestroy(ResolvedPath);

	/* Done! */
	return 0;
}

/***********************
 * Device Functions     *
 ***********************/
#include <DeviceManager.h>

/* Query Device Information */
int ScDeviceQuery(DeviceType_t Type, uint8_t *Buffer, size_t BufferLength)
{
	/* Alloc on stack */
	MCoreDeviceRequest_t Request;

	/* Locate */
	MCoreDevice_t *Device = NULL;// DmGetDevice(Type);
	_CRT_UNUSED(Type);

	/* Sanity */
	if (Device == NULL)
		return -1;

	/* Allocate a proxy buffer */
	uint8_t *Proxy = (uint8_t*)kmalloc(BufferLength);

	/* Reset request */
	memset(&Request, 0, sizeof(MCoreDeviceRequest_t));

	/* Setup */
	Request.Base.Type = RequestQuery;
	Request.Buffer = Proxy;
	Request.Length = BufferLength;
	Request.DeviceId = Device->Id;
	
	/* Fire request */
	//DmCreateRequest(&Request);
	//DmWaitRequest(&Request, 1000);

	/* Sanity */
	if (Request.Base.State == EventOk)
		memcpy(Buffer, Proxy, BufferLength);

	/* Cleanup */
	kfree(Proxy);

	/* Done! */
	return (int)EventOk - (int)Request.Base.State;
}

/***********************
* Driver Functions     *
***********************/
#include <AcpiInterface.h>
#define __ACPI_EXCLUDE_TABLES
#include <os/driver/acpi.h>

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
	if (ACPI_FAILURE(AcpiGetTable(Signature, 0, &PointerToHeader))) {
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
	if (ACPI_FAILURE(AcpiGetTable(Signature, 0, &Header))) {
		return OsError;
	}

	/* Wuhuu, the requested table exists, copy the
	 * table information over */
	memcpy(Header, Table, Header->Length);
	return OsNoError;
}

/* Include the driver-version of the io-space too */
#include <os/driver/io.h>

/* Creates and registers a new IoSpace with our
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

/* Tries to claim a given io-space, only one driver
 * can claim a single io-space at a time, to avoid
 * two drivers using the same device */
OsStatus_t ScIoSpaceAcquire(IoSpaceId_t IoSpace)
{
	/* Validate process permissions */

	/* Now lets try to acquire the IoSpace */
	return IoSpaceAcquire(IoSpace);
}

/* Tries to release a given io-space, only one driver
 * can claim a single io-space at a time, to avoid
 * two drivers using the same device */
OsStatus_t ScIoSpaceRelease(IoSpaceId_t IoSpace)
{
	/* Now lets try to release the IoSpace 
	 * Don't bother with validation */
	return IoSpaceRelease(IoSpace);
}

/* Destroys an io-space */
OsStatus_t ScIoSpaceDestroy(DeviceIoSpace_t *IoSpace)
{
	/* Sanitize params */

	/* Validate process permissions */

	/* Destroy the io-space, it might
	 * not be possible, if we don't own it */
	return IoSpaceDestroy(IoSpace);
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

/* This registers the calling 
 * process as the active window
 * manager, and thus shall recieve
 * all input messages */
int ScRegisterWindowManager(void)
{
	/* Locate Process */
	MCoreAsh_t *Ash = PhoenixGetAsh(PHOENIX_CURRENT);

	/* Sanity */
	if (Ash == NULL)
		return -1;

	/* Register Us */
	EmRegisterSystemTarget(Ash->Id);

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
Addr_t GlbSyscallTable[131] =
{
	/* Kernel Log */
	DefineSyscall(LogDebug),

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
	DefineSyscall(ScMemoryShare),
	DefineSyscall(ScMemoryUnshare),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),

	/* IPC Functions - 41 */
	DefineSyscall(ScPipeOpen),
	DefineSyscall(ScPipeClose),
	DefineSyscall(ScPipeRead),
	DefineSyscall(ScPipeWrite),
	DefineSyscall(ScIpcSleep),
	DefineSyscall(ScIpcWake),
	DefineSyscall(ScRpcExecute),
	DefineSyscall(ScRpcResponse),
	DefineSyscall(ScEvtExecute),
	DefineSyscall(NoOperation),

	/* Vfs Functions - 51 */
	DefineSyscall(ScVfsOpen),
	DefineSyscall(ScVfsClose),
	DefineSyscall(ScVfsRead),
	DefineSyscall(ScVfsWrite),
	DefineSyscall(ScVfsSeek),
	DefineSyscall(ScVfsFlush),
	DefineSyscall(ScVfsDelete),
	DefineSyscall(ScVfsMove),
	DefineSyscall(ScVfsQuery),
	DefineSyscall(ScVfsResolvePath),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),

	/* Timer Functions - 71 */
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),

	/* Device Functions - 81 */
	DefineSyscall(ScDeviceQuery),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),

	/* System Functions - 91 */
	DefineSyscall(ScEndBootSequence),
	DefineSyscall(ScRegisterWindowManager),
	DefineSyscall(ScEnvironmentQuery),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),

	/* Driver Functions - 101 
	 * - ACPI Support */
	DefineSyscall(ScAcpiQueryStatus),
	DefineSyscall(ScAcpiQueryTableHeader),
	DefineSyscall(ScAcpiQueryTable),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),

	/* Driver Functions - 111 
	 * - I/O Support */
	DefineSyscall(ScIoSpaceRegister),
	DefineSyscall(ScIoSpaceAcquire),
	DefineSyscall(ScIoSpaceRelease),
	DefineSyscall(ScIoSpaceDestroy),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation)
};
