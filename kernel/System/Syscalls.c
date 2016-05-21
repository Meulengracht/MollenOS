/* MollenOS
*
* Copyright 2011 - 2016, Philip Meulengracht
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
* MollenOS MCore - Shared Memory System
*/

/* Includes */
#include <Arch.h>
#include <ProcessManager.h>
#include <Threading.h>
#include <Scheduler.h>
#include <Log.h>
#include <os/Ipc.h>

#include <string.h>

/* Shorthand */
#define DefineSyscall(_Sys) ((Addr_t)&_Sys)

/***********************
 * Process Functions   *
 ***********************/
PId_t ScProcessSpawn(char *Path, char *Arguments)
{
	/* Alloc on stack */
	MCoreProcessRequest_t Request;

	/* Convert to MSTrings */
	MString_t *mPath = NULL;
	MString_t *mArguments = NULL;

	/* Sanity */
	if (Path == NULL)
		return 0xFFFFFFFF;

	/* Allocate the mstrings */
	mPath = MStringCreate(Path, StrUTF8);
	mArguments = (Arguments == NULL) ? NULL : MStringCreate(Arguments, StrUTF8);

	/* Setup */
	Request.Base.Type = ProcessSpawn;
	Request.Base.Cleanup = 0;
	Request.Path = mPath;
	Request.Arguments = mArguments;
	
	/* Fire! */
	PmCreateRequest(&Request);
	PmWaitRequest(&Request, 0);

	/* Cleanup */
	MStringDestroy(mPath);

	/* Only cleanup arguments if not null */
	if (mArguments != NULL)
		MStringDestroy(mArguments);

	/* Done */
	return Request.ProcessId;
}

int ScProcessJoin(PId_t ProcessId)
{
	/* Wait for process */
	MCoreProcess_t *Process = PmGetProcess(ProcessId);

	/* Sanity */
	if (Process == NULL)
		return -1;

	/* Sleep */
	SchedulerSleepThread((Addr_t*)Process, 0);
	IThreadYield();

	/* Return the exit code */
	return Process->ReturnCode;
}

int ScProcessKill(PId_t ProcessId)
{
	/* Alloc on stack */
	MCoreProcessRequest_t Request;

	/* Setup */
	Request.Base.Type = ProcessKill;
	Request.Base.Cleanup = 0;
	Request.ProcessId = ProcessId;

	/* Fire! */
	PmCreateRequest(&Request);
	PmWaitRequest(&Request, 1000);

	/* Return the exit code */
	if (Request.Base.State == EventOk)
		return 0;
	else
		return -1;
}

int ScProcessExit(int ExitCode)
{
	/* Disable interrupts */
	IntStatus_t IntrState = InterruptDisable();
	Cpu_t CurrentCpu = ApicGetCpu();
	MCoreProcess_t *Process = PmGetProcess(ThreadingGetCurrentThread(CurrentCpu)->ProcessId);

	/* Save return code */
	LogDebug("SYSC", "Process %s terminated with code %i", Process->Name->Data, ExitCode);
	Process->ReturnCode = ExitCode;

	/* Terminate all threads used by process */
	ThreadingTerminateProcessThreads(Process->Id);

	/* Mark process for reaping */
	PmTerminateProcess(Process);

	/* Enable Interrupts */
	InterruptRestoreState(IntrState);

	/* Done */
	return 0;
}

int ScProcessYield(void)
{
	/* Deep Call */
	IThreadYield();

	/* Done */
	return 0;
}

/**************************
* Shared Object Functions *
***************************/

/* Load a shared object given a path 
 * path must exists otherwise NULL is returned */
void *ScSharedObjectLoad(const char *SharedObject)
{
	/* Locate Process */
	Cpu_t CurrentCpu = ApicGetCpu();
	MCoreProcess_t *Process =
		PmGetProcess(ThreadingGetCurrentThread(CurrentCpu)->ProcessId);
	Addr_t BaseAddress = 0;
	
	/* Vars for solving library */
	void *Handle = NULL;

	/* Sanity */
	if (Process == NULL)
		return NULL;

	/* Construct a mstring */
	MString_t *Path = MStringCreate((void*)SharedObject, StrUTF8);

	/* Resolve Library */
	BaseAddress = Process->NextBaseAddress;
	Handle = PeResolveLibrary(Process->Executable, NULL, Path, &BaseAddress);
	Process->NextBaseAddress = BaseAddress;

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
	return PeResolveFunctionAddress((MCorePeFile_t*)Handle, Function);
}

/* Unloads a valid shared object handle
 * returns 0 on success */
int ScSharedObjectUnload(void *Handle)
{
	/* Locate Process */
	Cpu_t CurrentCpu = ApicGetCpu();
	MCoreProcess_t *Process =
		PmGetProcess(ThreadingGetCurrentThread(CurrentCpu)->ProcessId);

	/* Sanity */
	if (Process == NULL)
		return -1;

	/* Do the unload */
	PeUnloadLibrary(Process->Executable, (MCorePeFile_t*)Handle);

	/* Done! */
	return 0;
}

/***********************
* Threading Functions  *
***********************/


/***********************
* Memory Functions     *
***********************/
Addr_t ScMemoryAllocate(size_t Size, int Flags)
{
	/* Locate Process */
	Cpu_t CurrentCpu = ApicGetCpu();
	MCoreProcess_t *Process = 
		PmGetProcess(ThreadingGetCurrentThread(CurrentCpu)->ProcessId);

	/* For now.. */
	_CRT_UNUSED(Flags);

	/* Sanity */
	if (Process == NULL)
		return (Addr_t)-1;

	/* Call */
	return (Addr_t)umalloc(Process->Heap, Size);
}

int ScMemoryFree(Addr_t Address, size_t Length)
{
	/* Locate Process */
	Cpu_t CurrentCpu = ApicGetCpu();
	MCoreProcess_t *Process =
		PmGetProcess(ThreadingGetCurrentThread(CurrentCpu)->ProcessId);

	/* For now.. */
	_CRT_UNUSED(Length);

	/* Sanity */
	if (Process == NULL)
		return (Addr_t)-1;

	/* Call */
	ufree(Process->Heap, (void*)Address);

	/* Done */
	return 0;
}

int ScMemoryQuery(void)
{
	return 0;
}

/* Share memory with a process */
Addr_t ScMemoryShare(IpcComm_t Target, Addr_t Address, size_t Size)
{
	/* Locate Process */
	MCoreProcess_t *Process = PmGetProcess(Target);
	size_t NumBlocks, i;

	/* Sanity */
	if (Process == NULL
		|| Address == 0
		|| Size == 0)
		return 0;

	/* Start out by allocating memory 
	 * in target process */
	Addr_t Shm = BitmapAllocateAddress(Process->Shm, Size);
	NumBlocks = DIVUP(Size, PAGE_SIZE);

	/* Sanity -> If we cross a page boundary */
	if (((Address + Size) & PAGE_MASK)
		!= (Address & PAGE_MASK))
		NumBlocks++;

	/* Sanity */
	if (Shm == 0)
		return 0;

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
			AddressSpaceMap(AddressSpaceGetCurrent(), AdjustedAddr, PAGE_SIZE, ADDRESS_SPACE_FLAG_USER);
	
		/* Get physical mapping */
		PhysicalAddr = AddressSpaceGetMap(AddressSpaceGetCurrent(), AdjustedAddr);

		/* Map it directly into target process */
		AddressSpaceMapFixed(Process->AddressSpace, PhysicalAddr, 
			AdjustedShm, PAGE_SIZE, ADDRESS_SPACE_FLAG_USER | ADDRESS_SPACE_FLAG_VIRTUAL);
	}

	/* Done! */
	return Shm + (Address & ATTRIBUTE_MASK);
}

/* Unshare memory with a process */
int ScMemoryUnshare(IpcComm_t Target, Addr_t TranslatedAddress, size_t Size)
{
	/* Locate Process */
	MCoreProcess_t *Process = PmGetProcess(Target);
	size_t NumBlocks, i;

	/* Sanity */
	if (Process == NULL)
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
		AddressSpaceUnmap(Process->AddressSpace, AdjustedAddr, PAGE_SIZE);
	}

	/* Now unallocate it in their bitmap */
	BitmapFreeAddress(Process->Shm, TranslatedAddress, Size);

	/* Done */
	return 0;
}

/***********************
* IPC Functions        *
***********************/
#include <InputManager.h>

/* Get the top message for this process 
 * without actually consuming it */
int ScIpcPeek(uint8_t *MessageContainer, size_t MessageLength)
{
	/* Validation */
	if (MessageContainer == NULL
		|| MessageLength == 0)
		return -1;

	/* Get current process */
	Cpu_t CurrentCpu = ApicGetCpu();
	MCoreProcess_t *Process =
		PmGetProcess(ThreadingGetCurrentThread(CurrentCpu)->ProcessId);

	/* Should never happen this */
	if (Process == NULL)
		return -2;

	/* Read */
	return PipeRead(Process->Pipe, MessageLength, MessageContainer, 1);
}

/* Get the top message for this process
 * and consume the message, if no message 
 * is available, this function will block untill 
 * a message is available */
int ScIpcRead(uint8_t *MessageContainer, size_t MessageLength)
{
	/* Validation */
	if (MessageContainer == NULL
		|| MessageLength == 0)
		return -1;

	/* Get current process */
	Cpu_t CurrentCpu = ApicGetCpu();
	MCoreProcess_t *Process =
		PmGetProcess(ThreadingGetCurrentThread(CurrentCpu)->ProcessId);

	/* Should never happen this */
	if (Process == NULL)
		return -2;

	/* Read */
	return PipeRead(Process->Pipe, MessageLength, MessageContainer, 0);
}

/* Sends a message to another process, 
 * so far this system call is made in the fashion
 * that the recieving process must have room in their
 * message queue... dunno */
int ScIpcWrite(PId_t ProcessId, uint8_t *Message, size_t MessageLength)
{
	/* Vars */
	Cpu_t CurrentCpu = ApicGetCpu();
	MCoreProcess_t *Process = NULL;
	IpcComm_t Sender = 0;

	/* Validation */
	if (Message == NULL
		|| MessageLength == 0)
		return -1;

	/* Get current process */
	Process = PmGetProcess(ProcessId);
	Sender = ThreadingGetCurrentThread(CurrentCpu)->ProcessId;

	/* Sanity */
	if (Process == NULL)
		return -2;

	/* Fill in sender */
	((MEventMessageBase_t*)Message)->Sender = Sender;

	/* Write */
	return PipeWrite(Process->Pipe, MessageLength, Message);
}

/* This is a bit of a tricky synchronization method
 * and should always be used with care and WITH the timeout
 * since it could hang a process */
int ScIpcSleep(size_t Timeout)
{
	/* Get current process */
	Cpu_t CurrentCpu = ApicGetCpu();
	MCoreProcess_t *Process =
		PmGetProcess(ThreadingGetCurrentThread(CurrentCpu)->ProcessId);

	/* Should never happen this 
	 * Only threads associated with processes
	 * can call this */
	if (Process == NULL)
		return -1;

	/* Sleep on process handle */
	SchedulerSleepThread((Addr_t*)Process, Timeout);
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
	/* Vars */
	MCoreProcess_t *Process = NULL;

	/* Get current process */
	Process = PmGetProcess(Target);

	/* Sanity */
	if (Process == NULL)
		return -1;

	/* Send a wakeup signal */
	SchedulerWakeupOneThread((Addr_t*)Process);

	/* Now we should have waked up the waiting process */
	return 0;
}

/***********************
* VFS Functions        *
***********************/
#include <Vfs/Vfs.h>
#include <stdio.h>
#include <errno.h>

/* Open File */
int ScVfsOpen(const char *Utf8, VfsFileFlags_t OpenFlags, VfsErrorCode_t *ErrCode)
{
	/* Get current process */
	Cpu_t CurrentCpu = ApicGetCpu();
	MCoreProcess_t *Process =
		PmGetProcess(ThreadingGetCurrentThread(CurrentCpu)->ProcessId);

	/* Should never happen this
	 * Only threads associated with processes
	 * can call this */
	if (Process == NULL 
		|| Utf8 == NULL)
		return -1;

	/* Try */
	MCoreFileInstance_t *Handle = VfsOpen(Utf8, OpenFlags);

	/* Add to process list of open files */
	list_append(Process->OpenFiles, list_create_node(Handle->Id, Handle));

	/* Save error code */
	if (ErrCode != NULL)
		*ErrCode = Handle->Code;

	/* Done! */
	return Handle->Id;
}

/* Close File */
int ScVfsClose(int FileDescriptor)
{
	/* Get current process */
	Cpu_t CurrentCpu = ApicGetCpu();
	MCoreProcess_t *Process =
		PmGetProcess(ThreadingGetCurrentThread(CurrentCpu)->ProcessId);
	VfsErrorCode_t RetCode = VfsInvalidParameters;

	/* Should never happen this
	* Only threads associated with processes
	* can call this */
	if (Process == NULL)
		return RetCode;

	/* Lookup file handle */
	list_node_t *fNode = list_get_node_by_id(Process->OpenFiles, FileDescriptor, 0);
	
	/* Sanity */
	if (fNode == NULL)
		return RetCode;

	/* Remove from open files */
	list_remove_by_node(Process->OpenFiles, fNode);

	/* Deep call */
	RetCode = VfsClose((MCoreFileInstance_t*)fNode->data);

	/* free node */
	kfree(fNode);

	/* Deep Call */
	return RetCode;
}

/* Read File */
size_t ScVfsRead(int FileDescriptor, uint8_t *Buffer, size_t Length, VfsErrorCode_t *ErrCode)
{
	/* Get current process */
	Cpu_t CurrentCpu = ApicGetCpu();
	MCoreProcess_t *Process =
		PmGetProcess(ThreadingGetCurrentThread(CurrentCpu)->ProcessId);
	VfsErrorCode_t RetCode = VfsInvalidParameters;
	size_t bRead = 0;

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
	list_node_t *fNode = list_get_node_by_id(Process->OpenFiles, FileDescriptor, 0);

	/* Sanity */
	if (fNode == NULL)
		goto done;

	/* Do the read */
	bRead = (int)VfsRead((MCoreFileInstance_t*)fNode->data, Buffer, Length);
	RetCode = ((MCoreFileInstance_t*)fNode->data)->Code;

done:
	/* Save error code */
	if (ErrCode != NULL)
		*ErrCode = RetCode;

	/* Done */
	return bRead;
}

/* Write File */
size_t ScVfsWrite(int FileDescriptor, uint8_t *Buffer, size_t Length, VfsErrorCode_t *ErrCode)
{
	/* Get current process */
	Cpu_t CurrentCpu = ApicGetCpu();
	MCoreProcess_t *Process =
		PmGetProcess(ThreadingGetCurrentThread(CurrentCpu)->ProcessId);
	VfsErrorCode_t RetCode = VfsInvalidParameters;
	size_t bWritten = 0;

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
	list_node_t *fNode = list_get_node_by_id(Process->OpenFiles, FileDescriptor, 0);

	/* Sanity */
	if (fNode == NULL)
		goto done;

	/* Do the write */
	bWritten = VfsWrite((MCoreFileInstance_t*)fNode->data, Buffer, Length);
	RetCode = ((MCoreFileInstance_t*)fNode->data)->Code;

done:
	/* Save error code */
	if (ErrCode != NULL)
		*ErrCode = RetCode;

	/* Done */
	return bWritten;
}

/* Seek File */
int ScVfsSeek(int FileDescriptor, off_t Position)
{
	/* Get current process */
	Cpu_t CurrentCpu = ApicGetCpu();
	MCoreProcess_t *Process =
		PmGetProcess(ThreadingGetCurrentThread(CurrentCpu)->ProcessId);
	VfsErrorCode_t RetCode = VfsInvalidParameters;

	/* Should never happen this
	* Only threads associated with processes
	* can call this */
	if (Process == NULL)
		return RetCode;

	/* Lookup file handle */
	list_node_t *fNode = list_get_node_by_id(Process->OpenFiles, FileDescriptor, 0);

	/* Sanity */
	if (fNode == NULL)
		return RetCode;

	/* Seek */
	return (int)VfsSeek((MCoreFileInstance_t*)fNode->data, (uint64_t)Position);
}

/* Delete File */
int ScVfsDelete(int FileDescriptor)
{
	/* Get current process */
	Cpu_t CurrentCpu = ApicGetCpu();
	MCoreProcess_t *Process =
		PmGetProcess(ThreadingGetCurrentThread(CurrentCpu)->ProcessId);
	VfsErrorCode_t RetCode = VfsInvalidParameters;

	/* Should never happen this
	* Only threads associated with processes
	* can call this */
	if (Process == NULL)
		return RetCode;

	/* Lookup file handle */
	list_node_t *fNode = list_get_node_by_id(Process->OpenFiles, FileDescriptor, 0);

	/* Sanity */
	if (fNode == NULL)
		return RetCode;

	/* Deep Call */
	return (int)VfsDelete((MCoreFileInstance_t*)fNode->data);
}

/* Flush File */
int ScVfsFlush(int FileDescriptor)
{
	/* Get current process */
	Cpu_t CurrentCpu = ApicGetCpu();
	MCoreProcess_t *Process =
		PmGetProcess(ThreadingGetCurrentThread(CurrentCpu)->ProcessId);
	VfsErrorCode_t RetCode = VfsInvalidParameters;

	/* Should never happen this
	* Only threads associated with processes
	* can call this */
	if (Process == NULL)
		return RetCode;

	/* Lookup file handle */
	list_node_t *fNode = list_get_node_by_id(Process->OpenFiles, FileDescriptor, 0);

	/* Sanity */
	if (fNode == NULL)
		return RetCode;

	/* Deep Call */
	return (int)VfsFlush((MCoreFileInstance_t*)fNode->data);
}

/* Query information about 
 * a file handle or directory handle */
int ScVfsQuery(int FileDescriptor, VfsQueryFunction_t Function, void *Buffer, size_t Length)
{
	/* Get current process */
	Cpu_t CurrentCpu = ApicGetCpu();
	MCoreProcess_t *Process =
		PmGetProcess(ThreadingGetCurrentThread(CurrentCpu)->ProcessId);
	VfsErrorCode_t RetCode = VfsInvalidParameters;

	/* Should never happen this
	* Only threads associated with processes
	* can call this */
	if (Process == NULL
		|| Buffer == NULL)
		return RetCode;

	/* Lookup file handle */
	list_node_t *fNode = list_get_node_by_id(Process->OpenFiles, FileDescriptor, 0);

	/* Sanity */
	if (fNode == NULL)
		return RetCode;

	/* Redirect to Vfs */
	return (int)VfsQuery((MCoreFileInstance_t*)fNode->data, Function, Buffer, Length);
}

/* The file move operation 
 * this function copies Source -> destination
 * or moves it, deleting the Source. */
int ScVfsMove(const char *Source, const char *Destination, int Copy)
{
	/* Sanity */
	if (Source == NULL || Destination == NULL)
		return -1;

	/* Redirect to Vfs */
	return (int)VfsMove(Source, Destination, Copy);
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
	memcpy(StrBuffer, ResolvedPath->Data, ResolvedPath->Length);

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
	MCoreDevice_t *Device = DmGetDevice(Type);

	/* Sanity */
	if (Device == NULL)
		return -1;

	/* Allocate a proxy buffer */
	uint8_t *Proxy = (uint8_t*)kmalloc(BufferLength);

	/* Setup */
	Request.Type = RequestQuery;
	Request.Buffer = Proxy;
	Request.Length = BufferLength;
	Request.DeviceId = Device->Id;
	
	/* Fire request */
	DmCreateRequest(&Request);
	DmWaitRequest(&Request, 1000);

	/* Sanity */
	if (Request.Status == RequestOk)
		memcpy(Buffer, Proxy, BufferLength);

	/* Cleanup */
	kfree(Proxy);

	/* Done! */
	return (int)RequestOk - (int)Request.Status;
}

/***********************
* Driver Functions     *
***********************/

/* Create device io-space */
DeviceIoSpace_t *ScIoSpaceCreate(int Type, Addr_t PhysicalBase, size_t Size)
{
	/* Vars */
	DeviceIoSpace_t *IoSpace = NULL;

	/* Sanitize params */

	/* Validate process permissions */

	/* Try to create io space */
	IoSpace = IoSpaceCreate(Type, PhysicalBase, Size); /* Add owner to io-space */

	/* If null, space is already claimed */

	/* Done! */
	return IoSpace;
}

/* Read from an existing io-space */
size_t ScIoSpaceRead(DeviceIoSpace_t *IoSpace, size_t Offset, size_t Length)
{
	/* Sanitize params */

	/* Validate process permissions */

	/* Done */
	return IoSpaceRead(IoSpace, Offset, Length);
}

/* Write to an existing io-space */
int ScIoSpaceWrite(DeviceIoSpace_t *IoSpace, size_t Offset, size_t Value, size_t Length)
{
	/* Sanitize params */

	/* Validate process permissions */

	/* Write */
	IoSpaceWrite(IoSpace, Offset, Value, Length);

	/* Done! */
	return 0;
}

/* Destroys an io-space */
int ScIoSpaceDestroy(DeviceIoSpace_t *IoSpace)
{
	/* Sanitize params */

	/* Validate process permissions */

	/* Destroy */
	IoSpaceDestroy(IoSpace);

	/* Done! */
	return 0;
}

/*
DefineSyscall(DmCreateDevice),
DefineSyscall(DmRequestResource),
DefineSyscall(DmGetDevice),
DefineSyscall(DmDestroyDevice),
DefineSyscall(AddressSpaceGetCurrent),
DefineSyscall(AddressSpaceGetMap),
DefineSyscall(AddressSpaceMapFixed),
DefineSyscall(AddressSpaceUnmap),
DefineSyscall(AddressSpaceMap), */

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
	Cpu_t CurrentCpu = ApicGetCpu();
	MCoreProcess_t *Process =
		PmGetProcess(ThreadingGetCurrentThread(CurrentCpu)->ProcessId);

	/* Sanity */
	if (Process == NULL)
		return -1;

	/* Register Us */
	EmRegisterSystemTarget(Process->Id);

	/* Done */
	return 0;
}

/* Empty Operation, mostly
 * because the operation is
 * reserved */
int NoOperation(void)
{
	return 0;
}

/* Syscall Table */
Addr_t GlbSyscallTable[111] =
{
	/* Kernel Log */
	DefineSyscall(LogDebug),

	/* Process Functions - 1 */
	DefineSyscall(ScProcessExit),
	DefineSyscall(ScProcessYield),
	DefineSyscall(ScProcessSpawn),
	DefineSyscall(ScProcessJoin),
	DefineSyscall(ScProcessKill),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(ScSharedObjectLoad),
	DefineSyscall(ScSharedObjectGetFunction),
	DefineSyscall(ScSharedObjectUnload),

	/* Threading Functions - 11 */
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

	/* Memory Functions - 21 */
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

	/* IPC Functions - 31 */
	DefineSyscall(ScIpcPeek),
	DefineSyscall(ScIpcRead),
	DefineSyscall(ScIpcWrite),
	DefineSyscall(ScIpcSleep),
	DefineSyscall(ScIpcWake),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),

	/* Vfs Functions - 41 */
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

	/* Timer Functions - 61 */
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

	/* Device Functions - 71 */
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

	/* System Functions - 81 */
	DefineSyscall(ScEndBootSequence),
	DefineSyscall(ScRegisterWindowManager),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),

	/* Driver Functions - 91 */
	DefineSyscall(ScIoSpaceCreate),
	DefineSyscall(ScIoSpaceRead),
	DefineSyscall(ScIoSpaceWrite),
	DefineSyscall(ScIoSpaceDestroy),
	DefineSyscall(DmCreateDevice),
	DefineSyscall(DmRequestResource),
	DefineSyscall(DmGetDevice),
	DefineSyscall(DmDestroyDevice),
	DefineSyscall(AddressSpaceGetCurrent),
	DefineSyscall(AddressSpaceGetMap),
	DefineSyscall(AddressSpaceMapFixed),
	DefineSyscall(AddressSpaceUnmap),
	DefineSyscall(AddressSpaceMap),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation)
};