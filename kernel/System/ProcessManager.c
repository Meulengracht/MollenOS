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
* MollenOS MCore - Process Manager
*/

/* Includes */
#include <Arch.h>
#include <ProcessManager.h>
#include <Vfs/Vfs.h>
#include <GarbageCollector.h>
#include <Threading.h>
#include <Semaphore.h>
#include <Scheduler.h>
#include <Log.h>

/* CLib */
#include <stddef.h>
#include <ds/list.h>
#include <ds/mstring.h>
#include <string.h>

/* Prototypes */
int PmEventHandler(void *UserData, MCoreEvent_t *Event);
PId_t PmCreateProcess(MString_t *Path, MString_t *Arguments);

/* Globals */
MCoreEventHandler_t *GlbProcessEventHandler = NULL;
PId_t GlbProcessId = 0;
List_t *GlbProcesses = NULL; 
List_t *GlbZombieProcesses = NULL;

/* Setup & Start Request Handler */
void PmInit(void)
{
	/* Debug */
	LogInformation("PROC", "Installing Request Handler");

	/* Reset */
	GlbProcessId = 1;

	/* Create */
	GlbProcesses = ListCreate(KeyInteger, LIST_SAFE);
	GlbZombieProcesses = ListCreate(KeyInteger, LIST_SAFE);

	/* Create event handler */
	GlbProcessEventHandler = EventInit("Process Manager", PmEventHandler, NULL);
}

/* Create Request 
 * We simply move it on to
 * the event handler */
void PmCreateRequest(MCoreProcessRequest_t *Request)
{
	/* Deep call */
	EventCreate(GlbProcessEventHandler, &Request->Base);
}

/* Wait for request 
 * just as above we just call further
 * the event handler */
void PmWaitRequest(MCoreProcessRequest_t *Request, size_t Timeout)
{
	/* Deep Call */
	EventWait(&Request->Base, Timeout);
}

/* Event Handler */
int PmEventHandler(void *UserData, MCoreEvent_t *Event)
{
	/* Vars */
	MCoreProcessRequest_t *Request = NULL;

	/* Unused */
	_CRT_UNUSED(UserData);

	/* Cast */
	Request = (MCoreProcessRequest_t*)Event;

	/* Depends on request */
	switch (Request->Base.Type)
	{
		/* Spawn Process */
		case ProcessSpawn:
		{
			/* Deep Call */
			LogInformation("PROC", "Spawning %s", Request->Path->Data);
			Request->ProcessId = PmCreateProcess(Request->Path, Request->Arguments);

			/* Sanity */
			if (Request->ProcessId != 0xFFFFFFFF)
				Request->Base.State = EventOk;
			else
				Request->Base.State = EventFailed;

		} break;

		/* Kill Process */
		case ProcessKill:
		{
			/* Lookup process */
			MCoreProcess_t *Process = PmGetProcess(Request->ProcessId);

			/* Sanity */
			if (Process != NULL)
			{
				/* Terminate all threads used by process */
				ThreadingTerminateProcessThreads(Process->Id);

				/* Mark process for reaping */
				PmTerminateProcess(Process);
			}
			else
				Request->Base.State = EventFailed;

		} break;

		/* Panic */
		default:
		{
			LogDebug("PROC", "Unhandled Event %u", (uint32_t)Request->Base.Type);
		} break;
	}

	/* Cleanup? */
	if (Request->Base.Cleanup != 0)
	{
		if (Request->Path != NULL)
			MStringDestroy(Request->Path);
		if (Request->Arguments != NULL)
			MStringDestroy(Request->Arguments);
	}

	/* Return 0 */
	return 0;
}

/* Kickstarter function for Process */
void PmStartProcess(void *Args)
{
	/* Cast */
	Addr_t BaseAddress = MEMORY_LOCATION_USER;
	MCoreProcess_t *Process = (MCoreProcess_t*)Args;
	Cpu_t CurrentCpu = ApicGetCpu();
	MCoreThread_t *cThread = ThreadingGetCurrentThread(CurrentCpu);

	/* Update this thread */
	cThread->ProcessId = Process->Id;

	/* Allocate a open file list */
	Process->OpenFiles = ListCreate(KeyInteger, LIST_NORMAL);

	/* Save address space */
	Process->AddressSpace = AddressSpaceGetCurrent();

	/* Load Executable */
	Process->Executable = 
		PeLoadImage(NULL, Process->Name, Process->fBuffer, &BaseAddress);
	Process->NextBaseAddress = BaseAddress;

	/* Cleanup file buffer */
	kfree(Process->fBuffer);

	/* Create a heap */
	Process->Heap = HeapCreate(MEMORY_LOCATION_USER_HEAP, 1);
	Process->Shm = BitmapCreate(MEMORY_LOCATION_USER_SHM, MEMORY_LOCATION_USER_SHM_END, PAGE_SIZE);

	/* Map in arguments */
	AddressSpaceMap(AddressSpaceGetCurrent(), 
		MEMORY_LOCATION_USER_ARGS, PAGE_SIZE, ADDRESS_SPACE_FLAG_USER);

	/* Copy arguments */
	memcpy((void*)MEMORY_LOCATION_USER_ARGS,
		Process->Arguments->Data, Process->Arguments->Length);

	/* Create in */
	Process->Pipe = PipeCreate(PROCESS_PIPE_SIZE);

	/* Map Stack */
	BaseAddress = ((MEMORY_LOCATION_USER_STACK - 0x1) & PAGE_MASK);
	AddressSpaceMap(AddressSpaceGetCurrent(), BaseAddress, PROCESS_STACK_INIT, ADDRESS_SPACE_FLAG_USER);
	BaseAddress += (MEMORY_LOCATION_USER_STACK & ~(PAGE_MASK));
	Process->StackStart = BaseAddress;

	/* Go to user-land */
	ThreadingEnterUserMode(Process);

	/* Catch */
	IThreadYield();

	/* SHOULD NEVER reach this point */
	for (;;);
}

/* Create Process */
PId_t PmCreateProcess(MString_t *Path, MString_t *Arguments)
{
	/* Vars */
	MCoreProcess_t *Process = NULL;
	MCoreFileInstance_t *File = NULL;
	uint8_t *fBuffer = NULL;
	MString_t *PathCopy = NULL;
	DataKey_t Key;
	int Index = 0;

	/* Sanity */
	if (Path == NULL)
		return 0xFFFFFFFF;

	/* Open File */
	File = VfsOpen(Path->Data, Read);

	/* Sanity */
	if (File->Code != VfsOk
		|| File->File == NULL) {
		VfsClose(File);
		return 0xFFFFFFFF;
	}

	/* Allocate a buffer */
	fBuffer = (uint8_t*)kmalloc((size_t)File->File->Size);

	/* Read */
	VfsRead(File, fBuffer, (size_t)File->File->Size);

	/* Copy path */
	PathCopy = MStringCreate(File->File->Path->Data, StrUTF8);

	/* Close */
	VfsClose(File);

	/* Validate File */
	if (!PeValidate(fBuffer)) {
		/* Bail Out */
		MStringDestroy(PathCopy);
		kfree(fBuffer);
		return 0xFFFFFFFF;
	}

	/* Allocate */
	Process = (MCoreProcess_t*)kmalloc(sizeof(MCoreProcess_t));
	memset(Process, 0, sizeof(MCoreProcess_t));

	/* Set initial */
	Process->Id = GlbProcessId;
	Process->Parent = ThreadingGetCurrentThread(ApicGetCpu())->ProcessId;
	GlbProcessId++;

	/* Split path */
	Index = MStringFindReverse(PathCopy, '/');
	Process->Name = MStringSubString(PathCopy, Index + 1, -1);
	Process->WorkingDirectory = MStringSubString(PathCopy, 0, Index);
	Process->BaseDirectory = MStringSubString(PathCopy, 0, Index);

	/* Save file buffer */
	Process->fBuffer = fBuffer;

	/* Save arguments */
	if (Arguments != NULL
		&& Arguments->Length != 0) {
		Process->Arguments = PathCopy;
		MStringAppendChar(Process->Arguments, ' ');
		MStringAppendString(Process->Arguments, Arguments);
	}
	else
		Process->Arguments = PathCopy;

	/* Add process to list */
	Key.Value = (int)Process->Id;
	ListAppend(GlbProcesses, ListCreateNode(Key, Key, Process));

	/* Create the loader thread */
	ThreadingCreateThread(Process->Name->Data, PmStartProcess, Process, THREADING_USERMODE);

	/* Done */
	return Process->Id;
}

/* Get Process */
MCoreProcess_t *PmGetProcess(PId_t ProcessId)
{
	/* Iterate */
	foreach(pNode, GlbProcesses)
	{
		/* Cast */
		MCoreProcess_t *Process = (MCoreProcess_t*)pNode->Data;

		/* Found? */
		if (Process->Id == ProcessId)
			return Process;
	}

	/* Found? NO! */
	return NULL;
}

/* Get the working directory */
MString_t *PmGetWorkingDirectory(PId_t ProcessId)
{
	/* Iterate */
	foreach(pNode, GlbProcesses)
	{
		/* Cast */
		MCoreProcess_t *Process = (MCoreProcess_t*)pNode->Data;

		/* Found? */
		if (Process->Id == ProcessId)
			return Process->WorkingDirectory;
	}

	/* Found? NO! */
	return NULL;
}

/* Get the base directory */
MString_t *PmGetBaseDirectory(PId_t ProcessId)
{
	/* Iterate */
	foreach(pNode, GlbProcesses)
	{
		/* Cast */
		MCoreProcess_t *Process = (MCoreProcess_t*)pNode->Data;

		/* Found? */
		if (Process->Id == ProcessId)
			return Process->BaseDirectory;
	}

	/* Found? NO! */
	return NULL;
}

/* End Process */
void PmTerminateProcess(MCoreProcess_t *Process)
{
	/* Variables needed */
	ListNode_t *pNode = NULL;
	DataKey_t Key;

	/* Lookup node */
	Key.Value = (int)Process->Id;
	pNode = ListGetNodeByKey(GlbProcesses, Key, 0);

	/* Sanity */
	if (pNode == NULL)
		return;

	/* Remove it, add to zombies */
	ListRemoveByNode(GlbProcesses, pNode);
	ListAppend(GlbZombieProcesses, pNode);

	/* Wake all that waits for this to finish */
	SchedulerWakeupAllThreads((Addr_t*)pNode->Data);

	/* Tell GC */
	GcAddWork();
}

/* Cleans a process and it's resources */
void PmCleanupProcess(MCoreProcess_t *Process)
{
	/* Vars */
	ListNode_t *fNode = NULL;

	/* Cleanup Strings */
	MStringDestroy(Process->Name);
	MStringDestroy(Process->Arguments);
	MStringDestroy(Process->WorkingDirectory);
	MStringDestroy(Process->BaseDirectory);

	/* Go through open files, cleanup all handles */
	_foreach(fNode, Process->OpenFiles)
	{
		/* Cast */
		MCoreFileInstance_t *fHandle = (MCoreFileInstance_t*)fNode->Data;

		/* Cleanup */
		VfsClose(fHandle);
	}

	/* Destroy list */
	ListDestroy(Process->OpenFiles);

	/* Destroy Pipe */
	PipeDestroy(Process->Pipe);

	/* Destroy shared memory */
	BitmapDestroy(Process->Shm);

	/* Clean heap */
	kfree(Process->Heap);

	/* Cleanup executable data */
	PeUnload(Process->Executable);

	/* Rest of memory is cleaned during 
	 * address space cleanup */
	kfree(Process);
}

/* Cleans up all the unused processes */
void PmReapZombies(void)
{
	/* Reap untill list is empty */
	ListNode_t *tNode = ListPopFront(GlbZombieProcesses);

	while (tNode != NULL)
	{
		/* Cast */
		MCoreProcess_t *Process = (MCoreProcess_t*)tNode->Data;

		/* Clean it up */
		PmCleanupProcess(Process);

		/* Clean up rest */
		kfree(tNode);

		/* Get next node */
		tNode = ListPopFront(GlbZombieProcesses);
	}
}