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
#include <Shm.h>
#include <List.h>
#include <Log.h>

/* CLib */
#include <string.h>

/* Prototypes */
void PmEventHandler(void *Args);
PId_t PmCreateProcess(MString_t *Path, MString_t *Arguments);

/* Globals */
PId_t GlbProcessId = 0;
list_t *GlbProcesses = NULL; 
list_t *GlbZombieProcesses = NULL;
list_t *GlbProcessRequests = NULL;
Semaphore_t *GlbProcessEventLock = NULL;

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
	GlbProcessRequests = list_create(LIST_SAFE);
	GlbProcessEventLock = SemaphoreCreate(0);

	/* Start */
	ThreadingCreateThread("Process Event Thread", PmEventHandler, NULL, 0);
}

/* Create Request */
void PmCreateRequest(MCoreProcessRequest_t *Request)
{
	/* Add to list */
	list_append(GlbProcessRequests, list_create_node(0, Request));
	
	/* Set */
	Request->State = ProcessRequestPending;

	/* Signal */
	SemaphoreV(GlbProcessEventLock);
}

/* Event Handler */
void PmEventHandler(void *Args)
{
	/* Vars */
	list_node_t *eNode = NULL;
	MCoreProcessRequest_t *Request = NULL;

	/* Unused */
	_CRT_UNUSED(Args);

	/* Forever! */
	while (1)
	{
		/* Get event */
		SemaphoreP(GlbProcessEventLock);

		/* Pop from event queue */
		eNode = list_pop_front(GlbProcessRequests);

		/* Sanity */
		if (eNode == NULL)
			continue;

		/* Cast */
		Request = (MCoreProcessRequest_t*)eNode->data;

		/* Cleanup */
		kfree(eNode);

		/* Sanity */
		if (Request == NULL)
			continue;

		/* Set initial */
		Request->State = ProcessRequestInProgress;

		/* Depends on request */
		switch (Request->Type)
		{
			/* Spawn Process */
			case ProcessSpawn:
			{
				/* Deep Call */
				LogInformation("PROC", "Spawning %s", Request->Path->Data);
				Request->ProcessId = PmCreateProcess(Request->Path, Request->Arguments);

				/* Sanity */
				if (Request->ProcessId != 0xFFFFFFFF)
					Request->State = ProcessRequestOk;
				else
					Request->State = ProcessRequestFailed;

			} break;

			/* Panic */
			default:
			{
				LogDebug("PROC", "Unhandled Event %u", (uint32_t)Request->Type);
			} break;
		}

		/* Cleanup? */
		if (Request->Cleanup != 0)
		{
			if (Request->Path != NULL)
				MStringDestroy(Request->Path);
			if (Request->Arguments != NULL)
				MStringDestroy(Request->Arguments);
		}

		/* Signal Completion */
		SchedulerWakeupAllThreads((Addr_t*)Request);
	}
}

/* Kickstarter function for Process */
void PmStartProcess(void *Args)
{
	/* Cast */
	MCoreProcess_t *Process = (MCoreProcess_t*)Args;

	/* Go to user-land */
	ThreadingEnterUserMode(Process);

	/* Catch */
	_ThreadYield();

	/* SHOULD NEVER reach this point */
	for (;;);
}

/* Create Process */
PId_t PmCreateProcess(MString_t *Path, MString_t *Arguments)
{
	/* Sanity */
	if (Path == NULL)
		return 0xFFFFFFFF;

	/* Does file exist? */
	MCoreProcess_t *Process = NULL;
	AddressSpace_t *KernelAddrSpace = NULL;
	MCoreFile_t *File = NULL;
	uint8_t *fBuffer = NULL;
	Addr_t BaseAddress = MEMORY_LOCATION_USER;
	IntStatus_t IntrState = 0;
	int Index = 0;

	/* Open File */
	LogInformation("PROC", "Opening File");
	File = VfsOpen(Path->Data, Read);

	/* Sanity */
	if (File->Code != VfsOk)
	{
		VfsClose(File);
		return 0xFFFFFFFF;
	}

	/* Allocate a buffer */
	fBuffer = (uint8_t*)kmalloc((size_t)File->Size);

	/* Read */
	LogInformation("PROC", "Reading File, Size 0x%x", (size_t)File->Size);
	VfsRead(File, fBuffer, (size_t)File->Size);

	/* Close */
	VfsClose(File);

	/* Validate File */
	if (!PeValidate(fBuffer)
		|| PeValidate(fBuffer))
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

	/* Create address space */
	Process->AddrSpace = AddressSpaceCreate(ADDRESS_SPACE_USER);

	/* Get a reference to current */
	KernelAddrSpace = AddressSpaceGetCurrent();

	/* Disable Interrupts */
	IntrState = InterruptDisable();

	/* Switch to new address space */
	AddressSpaceSwitch(Process->AddrSpace);

	/* Load Executable */
	Process->Executable = PeLoadImage(NULL, Process->Name, fBuffer, &BaseAddress);

	/* Switch to kernel address space */
	AddressSpaceSwitch(KernelAddrSpace);

	/* Enable Interrupts */
	InterruptRestoreState(IntrState);

	/* Cleanup file buffer */
	kfree(fBuffer);

	/* Create a heap */
	Process->Heap = HeapCreate(MEMORY_LOCATION_USER_HEAP, 1);

	/* Map in arguments */
	if (Arguments != NULL && Arguments->Length != 0)
	{
		BaseAddress = ShmAllocateForProcess(Process->Id,
			Process->AddrSpace, MEMORY_LOCATION_USER_ARGS, Arguments->Length);

		/* Copy arguments */
		memcpy((void*)BaseAddress, Arguments->Data, Arguments->Length);

	}

	/* Build pipes */
	BaseAddress = ShmAllocateForProcess(Process->Id,
		Process->AddrSpace, MEMORY_LOCATION_PIPE_IN, PROCESS_PIPE_SIZE);

	/* Save */
	Process->iPipe = (RingBuffer_t*)BaseAddress;

	/* Construct In */
	RingBufferConstruct(Process->iPipe,
		(uint8_t*)(BaseAddress + sizeof(RingBuffer_t)),
		PROCESS_PIPE_SIZE - sizeof(RingBuffer_t));

	/* Allocate Out */
	BaseAddress = ShmAllocateForProcess(Process->Id,
		Process->AddrSpace, MEMORY_LOCATION_PIPE_OUT, PROCESS_PIPE_SIZE);

	/* Save */
	Process->oPipe = (RingBuffer_t*)BaseAddress;

	/* Construct In */
	RingBufferConstruct(Process->oPipe,
		(uint8_t*)(BaseAddress + sizeof(RingBuffer_t)),
		PROCESS_PIPE_SIZE - sizeof(RingBuffer_t));

	/* Add process to list */
	list_append(GlbProcesses, list_create_node((int)Process->Id, Process));

	/* Create the loader thread */
	ThreadingCreateThread("Process", PmStartProcess, Process, 0);

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
}