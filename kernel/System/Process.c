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
* MollenOS MCore - Processes Implementation
*/

/* Includes */
#include <Arch.h>
#include <Process.h>
#include <Vfs/VfsWrappers.h>
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

/* Externs */
extern List_t *GlbProcesses;
extern PhxId_t GlbProcessId;

/* Kickstarter function for Process */
void PmStartProcess(void *Args)
{
	/* Cast */
	Addr_t BaseAddress = MEMORY_LOCATION_USER;
	MCoreProcess_t *Process = (MCoreProcess_t*)Args;
	Cpu_t CurrentCpu = ApicGetCpu();
	MCoreThread_t *cThread = ThreadingGetCurrentThread(CurrentCpu);

	/* Update this thread */
	Process->MainThread = cThread->Id;
	cThread->ProcessId = Process->Id;

	/* Allocate a open file list */
	Process->OpenFiles = ListCreate(KeyInteger, LIST_NORMAL);

	/* Save address space */
	Process->AddressSpace = AddressSpaceGetCurrent();

	/* Load Executable */
	Process->Executable = 
		PeLoadImage(NULL, Process->Name, Process->fBuffer, Process->fBufferLength, &BaseAddress);
	Process->NextBaseAddress = BaseAddress;

	/* Cleanup file buffer */
	kfree(Process->fBuffer);

	/* Create a heap */
	Process->Heap = BitmapCreate(MEMORY_LOCATION_USER_HEAP, MEMORY_LOCATION_USER_HEAP_END, PAGE_SIZE);
	Process->Shm = BitmapCreate(MEMORY_LOCATION_USER_SHM, MEMORY_LOCATION_USER_SHM_END, PAGE_SIZE);

	/* Map in arguments */
	AddressSpaceMap(AddressSpaceGetCurrent(), 
		MEMORY_LOCATION_USER_ARGS, PAGE_SIZE, MEMORY_MASK_DEFAULT, ADDRESS_SPACE_FLAG_USER);

	/* Copy arguments */
	memcpy((void*)MEMORY_LOCATION_USER_ARGS,
		MStringRaw(Process->Arguments), MStringSize(Process->Arguments));

	/* Create in */
	Process->Pipe = PipeCreate(PROCESS_PIPE_SIZE);

	/* Map Stack */
	BaseAddress = ((MEMORY_LOCATION_USER_STACK - 0x1) & PAGE_MASK);
	AddressSpaceMap(AddressSpaceGetCurrent(), BaseAddress, PROCESS_STACK_INIT, MEMORY_MASK_DEFAULT, ADDRESS_SPACE_FLAG_USER);
	BaseAddress += (MEMORY_LOCATION_USER_STACK & ~(PAGE_MASK));
	Process->StackStart = BaseAddress;

	/* Setup signalling */
	Process->SignalQueue = ListCreate(KeyInteger, LIST_NORMAL);

	/* Go to user-land */
	ThreadingEnterUserMode(Process);

	/* Catch */
	IThreadYield();

	/* SHOULD NEVER reach this point */
	for (;;);
}

/* Create Process */
PhxId_t PmCreateProcess(MString_t *Path, MString_t *Arguments)
{
	/* Vars */
	MCoreProcess_t *Process = NULL;
	MCoreFileInstance_t *File = NULL;
	uint8_t *fBuffer = NULL;
	size_t fSize = 0;
	MString_t *PathCopy = NULL;
	DataKey_t Key;
	int Index = 0;

	/* Sanity */
	if (Path == NULL)
		return PROCESS_NO_PROCESS;

	/* Open File */
	File = VfsWrapperOpen(MStringRaw(Path), Read);

	/* Sanity */
	if (File->Code != VfsOk
		|| File->File == NULL) {
		VfsWrapperClose(File);
		return PROCESS_NO_PROCESS;
	}

	/* Allocate a buffer */
	fSize = (size_t)File->File->Size;
	fBuffer = (uint8_t*)kmalloc(fSize);

	/* Read */
	VfsWrapperRead(File, fBuffer, fSize);

	/* Copy path */
	PathCopy = MStringCreate((void*)MStringRaw(File->File->Path), StrUTF8);

	/* Close */
	VfsWrapperClose(File);

	/* Validate File */
	if (!PeValidate(fBuffer, fSize)) {
		/* Bail Out */
		MStringDestroy(PathCopy);
		kfree(fBuffer);
		return PROCESS_NO_PROCESS;
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
	Process->fBufferLength = fSize;

	/* Save arguments */
	if (Arguments != NULL
		&& MStringSize(Arguments) != 0) {
		Process->Arguments = PathCopy;
		MStringAppendCharacter(Process->Arguments, ' ');
		MStringAppendString(Process->Arguments, Arguments);
	}
	else
		Process->Arguments = PathCopy;

	/* Add process to list */
	Key.Value = (int)Process->Id;
	ListAppend(GlbProcesses, ListCreateNode(Key, Key, Process));

	/* Create the loader thread */
	ThreadingCreateThread((char*)MStringRaw(Process->Name), PmStartProcess, Process, THREADING_USERMODE);

	/* Done */
	return Process->Id;
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
		VfsWrapperClose(fHandle);
	}

	/* Destroy list */
	ListDestroy(Process->OpenFiles);

	/* Destroy signal stuff */
	_foreach(fNode, Process->SignalQueue) {
		/* Free the signal */
		kfree(fNode->Data);
	}

	/* Destroy the signal list */
	ListDestroy(Process->SignalQueue);

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

/* Get Process 
 * This function looks up a process structure 
 * by id, if either PROCESS_CURRENT or PROCESS_NO_PROCESS 
 * is passed, it retrieves the current process */
MCoreProcess_t *PmGetProcess(PhxId_t ProcessId)
{
	/* Variables */
	ListNode_t *pNode = NULL;
	Cpu_t CurrentCpu = 0;

	/* Sanity */
	if (GlbProcesses == NULL
		|| GlbProcesses->Length == 0) {
		return NULL;
	}

	/* Sanitize the process id */
	if (ProcessId == PROCESS_CURRENT
		|| ProcessId == PROCESS_NO_PROCESS) 
	{
		/* Get current cpu id */
		CurrentCpu = ApicGetCpu();

		/* Sanitize threading is up */
		if (ThreadingGetCurrentThread(CurrentCpu) != NULL) {
			ProcessId = ThreadingGetCurrentThread(CurrentCpu)->ProcessId;
		}
		else {
			return NULL;
		}
	}

	/* Iterate */
	_foreach(pNode, GlbProcesses)
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

/* Get the working directory 
 * This function looks up the working directory for a process 
 * by id, if either PROCESS_CURRENT or PROCESS_NO_PROCESS 
 * is passed, it retrieves the current process's working directory */
MString_t *PmGetWorkingDirectory(PhxId_t ProcessId)
{
	/* Variables */
	MCoreProcess_t *Process = PmGetProcess(ProcessId);

	/* Sanitize result */
	if (Process != NULL) {
		return Process->WorkingDirectory;
	}
	else {
		return NULL;
	}
}

/* Get the base directory 
 * This function looks up the base directory for a process 
 * by id, if either PROCESS_CURRENT or PROCESS_NO_PROCESS 
 * is passed, it retrieves the current process's base directory */
MString_t *PmGetBaseDirectory(PhxId_t ProcessId)
{
	/* Variables */
	MCoreProcess_t *Process = PmGetProcess(ProcessId);

	/* Sanitize result */
	if (Process != NULL) {
		return Process->BaseDirectory;
	}
	else {
		return NULL;
	}
}

/* Queries the given process for information
 * which kind of information is determined by <Function> */
int PmQueryProcess(MCoreProcess_t *Process, 
	ProcessQueryFunction_t Function, void *Buffer, size_t Length)
{
	/* Sanity */
	if (Process == NULL
		|| Buffer == NULL
		|| Length == 0)
		return -1;

	/* Now.. What do you want to know? */
	switch (Function)
	{
		/* Query name? */
		case ProcessQueryName:
		{
			/* Never copy more data than user can contain */
			size_t BytesToCopy = MIN(MStringSize(Process->Name), Length);

			/* Cooopy */
			memcpy(Buffer, MStringRaw(Process->Name), BytesToCopy);

		} break;

		/* Query memory? in bytes of course */
		case ProcessQueryMemory:
		{
			/* For now, we only support querying of 
			 * how much memory has been allocated for the heap */
			*((size_t*)Buffer) = (Process->Heap->BlocksAllocated * PAGE_SIZE);

		} break;

		/* Query immediate parent? */
		case ProcessQueryParent:
		{
			/* Get a pointer */
			PhxId_t *bPtr = (PhxId_t*)Buffer;

			/* There we go */
			*bPtr = Process->Parent;

		} break;

		/* Query topmost parent? */
		case ProcessQueryTopMostParent:
		{
			/* Get a pointer */
			PhxId_t *bPtr = (PhxId_t*)Buffer;

			/* Set initial value */
			*bPtr = PROCESS_NO_PROCESS;

			/* While parent has a valid parent */
			MCoreProcess_t *Parent = Process;
			while (Parent->Parent != PROCESS_NO_PROCESS) {
				*bPtr = Parent->Parent;
				Parent = PmGetProcess(Parent->Parent);
			}

		} break;

		default:
			break;
	}

	/* Done! */
	return 0;
}
