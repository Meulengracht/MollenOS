/* MollenOS
*
* Copyright 2011 - 2014, Philip Meulengracht
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

/* Prototypes */
void PmEventHandler(void *Args);
PId_t PmCreateProcess(MString_t *Path, MString_t *Arguments);

/* Globals */
PId_t GlbProcessId = 0;
list_t *GlbProcesses = NULL;
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
	GlbProcessRequests = list_create(LIST_SAFE);
	GlbProcessEventLock = SemaphoreCreate(0);

	/* Start */
	ThreadingCreateThread("Process Event Thread", PmEventHandler, NULL, 0);
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

		/* Signal Completion */
		SchedulerWakeupAllThreads((Addr_t*)Request);
	}
}

/* Create Process */
PId_t PmCreateProcess(MString_t *Path, MString_t *Arguments)
{
	/* Sanity */
	if (Path == NULL
		|| Arguments == NULL)
		return 0xFFFFFFFF;

	/* Does file exist? */
	MCoreProcess_t *Process = NULL;
	AddressSpace_t *KernelAddrSpace = NULL;
	MCoreFile_t *File = VfsOpen(Path->Data, Read);
	uint8_t *fBuffer = NULL;
	Addr_t BaseAddress = MEMORY_LOCATION_USER;
	IntStatus_t IntrState = 0;
	int Index = 0;

	/* Sanity */
	if (File->Code != VfsOk)
	{
		VfsClose(File);
		return 0xFFFFFFFF;
	}

	/* Allocate a buffer */
	fBuffer = (uint8_t*)kmalloc((size_t)File->Size);

	/* Read */
	VfsRead(File, fBuffer, (size_t)File->Size);

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

	/* Unmap kernel space */
	AddressSpaceReleaseKernel(Process->AddrSpace);

	/* Create a heap */
	Process->Heap = HeapCreate(MEMORY_LOCATION_USER_HEAP);

	/* Map in arguments */

	/* Build pipes */

	/* Map Syscall Handler */

	/* Add process to list */
	list_append(GlbProcesses, list_create_node(Process->Id, Process));

	/* Create the loader thread */

	/* Done */
	return Process->Id;
}