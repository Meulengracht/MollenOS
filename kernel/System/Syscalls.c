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
	_ThreadYield();

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
	_ThreadYield();

	/* Done */
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


/***********************
* VFS Functions        *
***********************/
#include <Vfs/Vfs.h>
#include <stdio.h>

/* Open File */
int ScVfsOpen(const char *Utf8, FILE *cData, VfsFileFlags_t OpenFlags)
{
	/* Sanity */
	if (Utf8 == NULL || cData == NULL)
		return -1;

	/* Try */
	MCoreFile_t *Handle = VfsOpen(Utf8, OpenFlags);

	/* Done */
	cData->_handle = (void*)Handle;
	return 0 - (int)Handle->Code;
}

/* Close File */
int ScVfsClose(FILE *cData)
{
	/* Sanity */
	if (cData == NULL || cData->_handle == NULL)
		return -1;

	/* Deep Call */
	return 0 - (int)VfsClose((MCoreFile_t*)cData->_handle);
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

	/* Done */
	return (int)VfsRead((MCoreFile_t*)cData->_handle, Buffer, Length);
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

	/* Done */
	return (int)VfsWrite((MCoreFile_t*)cData->_handle, Buffer, Length);
}

/* Seek File */
int ScVfsSeek(FILE *cData, size_t Position)
{
	/* Sanity */
	if (cData == NULL || cData->_handle == NULL)
		return -1;

	/* Deep Call */
	return VfsSeek((MCoreFile_t*)cData->_handle, (uint64_t)Position);
}

/* Delete File */
int ScVfsDelete(FILE *cData)
{
	/* Sanity */
	if (cData == NULL || cData->_handle == NULL)
		return -1;

	/* Deep Call */
	return VfsDelete((MCoreFile_t*)cData->_handle);
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
* System Functions     *
***********************/
#include <InputManager.h>

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
Addr_t GlbSyscallTable[71] =
{
	/* Kernel Log */
	DefineSyscall(LogDebug),

	/* Process Functions */
	DefineSyscall(ScProcessExit),
	DefineSyscall(ScProcessYield),
	DefineSyscall(ScProcessSpawn),
	DefineSyscall(ScProcessJoin),
	DefineSyscall(ScProcessKill),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),

	/* Threading Functions */
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

	/* Memory Functions */
	DefineSyscall(ScMemoryAllocate),
	DefineSyscall(ScMemoryFree),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),

	/* IPC Functions */
	DefineSyscall(NoOperation), //ReadMessage
	DefineSyscall(NoOperation), //WriteMessage
	DefineSyscall(NoOperation), //PeekMessage
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),

	/* Vfs Functions */
	DefineSyscall(ScVfsOpen),
	DefineSyscall(ScVfsClose),
	DefineSyscall(ScVfsRead),
	DefineSyscall(ScVfsWrite),
	DefineSyscall(ScVfsSeek),
	DefineSyscall(NoOperation), //Flush
	DefineSyscall(ScVfsDelete),
	DefineSyscall(NoOperation), //Move/Copy
	DefineSyscall(NoOperation), //Query
	DefineSyscall(NoOperation),

	/* Device Functions */
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

	/* System Functions */
	DefineSyscall(ScEndBootSequence),
	DefineSyscall(ScRegisterWindowManager),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation),
	DefineSyscall(NoOperation)
};