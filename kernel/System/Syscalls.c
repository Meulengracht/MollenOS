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
#include <string.h>

/* Shorthand */
#define DefineSyscall(_Sys) ((Addr_t)&_Sys)

/***********************
 * Process Functions   *
 ***********************/
PId_t ScProcessSpawn(void)
{
	/* Alloc on stack */
	MCoreProcessRequest_t Request;

	/* Setup */
	Request.Type = ProcessSpawn;
	Request.Cleanup = 0;
	Request.Path = NULL;
	Request.Arguments = NULL;
	
	/* Fire! */
	PmCreateRequest(&Request);
	PmWaitRequest(&Request);

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
	SchedulerSleepThread((Addr_t*)Process);
	IThreadYield();

	/* Return the exit code */
	return Process->ReturnCode;
}

int ScProcessKill(PId_t ProcessId)
{
	/* Alloc on stack */
	MCoreProcessRequest_t Request;

	/* Setup */
	Request.Type = ProcessKill;
	Request.Cleanup = 0;
	Request.ProcessId = ProcessId;

	/* Fire! */
	PmCreateRequest(&Request);
	PmWaitRequest(&Request);

	/* Return the exit code */
	if (Request.State == ProcessRequestOk)
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

/* Sends a message to another process */
int ScIpcWrite(PId_t ProcessId, uint8_t *Message, size_t MessageLength)
{
	/* Validation */
	if (Message == NULL
		|| MessageLength == 0)
		return -1;

	/* Get current process */
	MCoreProcess_t *Process = PmGetProcess(ProcessId);

	/* Sanity */
	if (Process == NULL)
		return -1;

	/* Write */
	return PipeWrite(Process->Pipe, MessageLength, Message);
}

/***********************
* VFS Functions        *
***********************/
#include <Vfs/Vfs.h>
#include <stdio.h>
#include <errno.h>

/* Vfs Code to C-Library code, 
 * used for c-library syscalls */
int ScVfsConvertCodeAndSetStatus(MCoreFileInstance_t *Handle, FILE *cData, VfsErrorCode_t Code)
{
	/* Start out by converting error code */
	if (Code == VfsOk)
		cData->code = EOK;
	else if (Code == VfsDeleted)
		cData->code = ENOENT;
	else if (Code == VfsInvalidParameters)
		cData->code = EINVAL;
	else if (Code == VfsInvalidPath 
		     || Code == VfsPathNotFound)
		cData->code = ENOENT;
	else if (Code == VfsAccessDenied)
		cData->code = EACCES;
	else if (Code == VfsPathIsNotDirectory)
		cData->code = EISDIR;
	else if (Code == VfsPathExists)
		cData->code = EEXIST;
	else if (Code == VfsDiskError)
		cData->code = EIO;
	else
		cData->code = EINVAL;

	/* Update Status */
	if (Handle != NULL) 
	{
		/* Reset */
		cData->status = 0;

		/* Check EOF */
		if (Handle->IsEOF)
			cData->status |= CLIB_FCODE_EOF;
	}

	/* Done */
	return 0 - (int)Code;
}

/* Open File */
int ScVfsOpen(const char *Utf8, FILE *cData, VfsFileFlags_t OpenFlags)
{
	/* Sanity */
	if (Utf8 == NULL || cData == NULL)
		return -1;

	/* Try */
	MCoreFileInstance_t *Handle = VfsOpen(Utf8, OpenFlags);

	/* Save handle */
	cData->_handle = (void*)Handle;

	/* Update status */
	return ScVfsConvertCodeAndSetStatus(Handle, cData, Handle->Code);
}

/* Close File */
int ScVfsClose(FILE *cData)
{
	/* Sanity */
	if (cData == NULL || cData->_handle == NULL)
		return -1;

	/* Deep Call */
	return 0 - (int)VfsClose((MCoreFileInstance_t*)cData->_handle);
}

/* Read File */
int ScVfsRead(FILE *cData, uint8_t *Buffer, size_t Length)
{
	/* Sanity */
	if (cData == NULL || cData->_handle == NULL
		|| Buffer == NULL)
		return -1;

	/* More sanity */
	if (Length == 0)
		return 0;

	/* Do the read */
	int bRead = (int)VfsRead((MCoreFileInstance_t*)cData->_handle, Buffer, Length);

	/* Update Status */
	ScVfsConvertCodeAndSetStatus((MCoreFileInstance_t*)cData->_handle, cData,
		((MCoreFileInstance_t*)cData->_handle)->Code);

	/* Done */
	return bRead;
}

/* Write File */
int ScVfsWrite(FILE *cData, uint8_t *Buffer, size_t Length)
{
	/* Sanity */
	if (cData == NULL || cData->_handle == NULL
		|| Buffer == NULL)
		return -1;

	/* More sanity */
	if (Length == 0)
		return 0;

	/* Do the write */
	int bWritten = (int)VfsWrite((MCoreFileInstance_t*)cData->_handle, Buffer, Length);

	/* Update Status */
	ScVfsConvertCodeAndSetStatus((MCoreFileInstance_t*)cData->_handle, cData,
		((MCoreFileInstance_t*)cData->_handle)->Code);

	/* Done */
	return bWritten;
}

/* Seek File */
int ScVfsSeek(FILE *cData, size_t Position)
{
	/* Sanity */
	if (cData == NULL || cData->_handle == NULL)
		return -1;

	/* Seek in file */
	VfsErrorCode_t ErrCode = VfsSeek((MCoreFileInstance_t*)cData->_handle, (uint64_t)Position);

	/* Update Status */
	return ScVfsConvertCodeAndSetStatus((MCoreFileInstance_t*)cData->_handle, cData, ErrCode);
}

/* Delete File */
int ScVfsDelete(FILE *cData)
{
	/* Sanity */
	if (cData == NULL || cData->_handle == NULL)
		return -1;

	/* Deep Call */
	return VfsDelete((MCoreFileInstance_t*)cData->_handle);
}

/* Flush File */
int ScVfsFlush(FILE *cData)
{
	/* Sanity */
	if (cData == NULL || cData->_handle == NULL)
		return -1;

	/* Deep Call */
	return VfsFlush((MCoreFileInstance_t*)cData->_handle);
}

/* Query information about 
 * a file handle or directory handle */
int ScVfsQuery(FILE *cData, VfsQueryFunction_t Function, void *Buffer, size_t Length)
{
	/* Sanity */
	if (cData == NULL || cData->_handle == NULL || Buffer == NULL)
		return -1;

	/* Redirect to Vfs */
	return 0 - (int)VfsQuery((MCoreFileInstance_t*)cData->_handle, Function, Buffer, Length);
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
	return 0 - (int)VfsMove(Source, Destination, Copy);
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
	DmWaitRequest(&Request);

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

	/* Process Functions - 1*/
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
	DefineSyscall(NoOperation),	//ScMemoryQuery
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),

	/* IPC Functions - 31 */
	DefineSyscall(ScIpcPeek),
	DefineSyscall(ScIpcRead),
	DefineSyscall(ScIpcWrite),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
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