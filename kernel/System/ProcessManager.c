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
#include <ProcessManager.h>
#include <Vfs/Vfs.h>
#include <Threading.h>
#include <Semaphore.h>
#include <Scheduler.h>
#include <List.h>
#include <Log.h>

/* CLib */
#include <string.h>

/* Prototypes */
int PmEventHandler(void *UserData, MCoreEvent_t *Event);
PId_t PmCreateProcess(MString_t *Path, MString_t *Arguments);

/* Globals */
MCoreEventHandler_t *GlbProcessEventHandler = NULL;
PId_t GlbProcessId = 0;
list_t *GlbProcesses = NULL; 
list_t *GlbZombieProcesses = NULL;

/* Setup & Start Request Handler */
void PmInit(void)
{
	/* Debug */
	LogInformation("PROC", "Installing Request Handler");

	/* Reset */
	GlbProcessId = 0;

	/* Create */
	GlbProcesses = list_create(LIST_SAFE);
	GlbZombieProcesses = list_create(LIST_SAFE);

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
	Process->OpenFiles = list_create(LIST_NORMAL);

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
	int Index = 0;

	/* Sanity */
	if (Path == NULL)
		return 0xFFFFFFFF;

	/* Open File */
	File = VfsOpen(Path->Data, Read);

	/* Sanity */
	if (File->Code != VfsOk)
	{
		VfsClose(File);
		return 0xFFFFFFFF;
	}

	/* Allocate a buffer */
	fBuffer = (uint8_t*)kmalloc((size_t)File->File->Size);

	/* Read */
	VfsRead(File, fBuffer, (size_t)File->File->Size);

	/* Close */
	VfsClose(File);

	/* Validate File */
	if (!PeValidate(fBuffer))
	{
		/* Bail Out */
		kfree(fBuffer);
		return 0xFFFFFFFF;
	}

	/* Allocate */
	Process = (MCoreProcess_t*)kmalloc(sizeof(MCoreProcess_t));

	/* Set initial */
	Process->Id = GlbProcessId;
	GlbProcessId++;

	/* Split path */
	Index = MStringFindReverse(Path, '/');
	Process->Name = MStringSubString(Path, Index + 1, -1);
	Process->WorkingDirectory = MStringSubString(Path, 0, Index);
	Process->BaseDirectory = MStringSubString(Path, 0, Index);

	/* Save file buffer */
	Process->fBuffer = fBuffer;

	/* Save arguments */
	if (Arguments != NULL
		&& Arguments->Length != 0) {
		Process->Arguments = MStringCreate(Path->Data, StrUTF8);
		MStringAppendChar(Process->Arguments, ' ');
		MStringAppendString(Process->Arguments, Arguments);
	}
	else
		Process->Arguments = MStringCreate(Path->Data, StrUTF8);

	/* Add process to list */
	list_append(GlbProcesses, list_create_node((int)Process->Id, Process));

	/* Create the loader thread */
	ThreadingCreateThread("Process", PmStartProcess, Process, THREADING_USERMODE);

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
		MCoreProcess_t *Process = (MCoreProcess_t*)pNode->data;

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
		MCoreProcess_t *Process = (MCoreProcess_t*)pNode->data;

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
		MCoreProcess_t *Process = (MCoreProcess_t*)pNode->data;

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
	/* Lookup node */
	list_node_t *pNode = list_get_node_by_id(GlbProcesses, (int)Process->Id, 0);

	/* Sanity */
	if (pNode == NULL)
		return;

	/* Remove it, add to zombies */
	list_remove_by_node(GlbProcesses, pNode);
	list_append(GlbZombieProcesses, pNode);

	/* Wake all that waits for this to finish */
	SchedulerWakeupAllThreads((Addr_t*)pNode->data);
}

/* Cleans a process and it's resources */
void PmCleanupProcess(MCoreProcess_t *Process)
{
	/* Vars */
	list_node_t *fNode = NULL;

	/* Cleanup Strings */
	MStringDestroy(Process->Name);
	MStringDestroy(Process->Arguments);
	MStringDestroy(Process->WorkingDirectory);
	MStringDestroy(Process->BaseDirectory);

	/* Go through open files, cleanup all handles */
	_foreach(fNode, Process->OpenFiles)
	{
		/* Cast */
		MCoreFileInstance_t *fHandle = (MCoreFileInstance_t*)fNode->data;

		/* Cleanup */
		VfsClose(fHandle);
	}

	/* Destroy list */
	list_destroy(Process->OpenFiles);

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
	list_node_t *tNode = list_pop_front(GlbZombieProcesses);

	while (tNode != NULL)
	{
		/* Cast */
		MCoreProcess_t *Process = (MCoreProcess_t*)tNode->data;

		/* Clean it up */
		PmCleanupProcess(Process);

		/* Clean up rest */
		kfree(tNode);

		/* Get next node */
		tNode = list_pop_front(GlbZombieProcesses);
	}
}