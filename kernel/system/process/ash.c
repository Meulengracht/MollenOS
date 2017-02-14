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
 * MollenOS MCore - Server & Process Management
 * - The process/server manager is known as Phoenix
 * - In this file we implement Ash functions
 */

/* Includes 
 * - System */
#include <process/phoenix.h>
#include <modules/modules.h>
#include <vfs/vfswrappers.h>
#include <scheduler.h>
#include <threading.h>
#include <log.h>

/* Includes
 * - Library */
#include <stddef.h>

/* Externs */
__EXTERN UUId_t *GlbAliasMap;
__EXTERN UUId_t GlbAshIdGenerator;
__EXTERN List_t *GlbAshes;

/* This is the finalizor function for starting
 * up a new base Ash, it finishes setting up the environment
 * and memory mappings, must be called on it's own thread */
void PhoenixFinishAsh(MCoreAsh_t *Ash)
{
	/* Cast */
	Cpu_t CurrentCpu = ApicGetCpu();
	MCoreThread_t *Thread = ThreadingGetCurrentThread(CurrentCpu);
	Addr_t BaseAddress = 0;
	int LoadedFromInitRD = 0;

	/* Sanitize the loaded path, if we were
	 * using the initrd set flags accordingly */
	if (MStringFindChars(Ash->Path, "rd:/") != MSTRING_NOT_FOUND) {
		LoadedFromInitRD = 1;
	}

	/* Update this thread */
	Ash->MainThread = Thread->Id;
	Thread->AshId = Ash->Id;

	/* Save address space */
	Ash->AddressSpace = AddressSpaceGetCurrent();

	/* Never translate here */
	BaseAddress = MEMORY_LOCATION_RING3_CODE;

	/* Load Executable */
	Ash->Executable = PeLoadImage(NULL, Ash->Name, Ash->FileBuffer, 
		Ash->FileBufferLength, &BaseAddress, LoadedFromInitRD);
	Ash->NextLoadingAddress = BaseAddress;

	/* Cleanup file buffer */
	if (!LoadedFromInitRD) {
		kfree(Ash->FileBuffer);
	}
	Ash->FileBuffer = NULL;

	/* Create the memory allocators */
	Ash->Heap = BitmapCreate(AddressSpaceTranslate(Ash->AddressSpace, MEMORY_LOCATION_RING3_HEAP),
		AddressSpaceTranslate(Ash->AddressSpace, MEMORY_LOCATION_RING3_SHM), PAGE_SIZE);
	Ash->Shm = BitmapCreate(AddressSpaceTranslate(Ash->AddressSpace, MEMORY_LOCATION_RING3_SHM),
		AddressSpaceTranslate(Ash->AddressSpace, MEMORY_LOCATION_RING3_IOSPACE), PAGE_SIZE);

	/* Map Stack */
	AddressSpaceMap(AddressSpaceGetCurrent(), (MEMORY_SEGMENT_STACK_BASE & PAGE_MASK),
		ASH_STACK_INIT, MEMORY_MASK_DEFAULT, ADDRESS_SPACE_FLAG_APPLICATION);
	Ash->StackStart = MEMORY_SEGMENT_STACK_BASE;

	/* Setup signalling */
	Ash->SignalQueue = ListCreate(KeyInteger, LIST_NORMAL);
}

/* This is the standard ash-boot function
 * which simply sets up the ash and jumps to userland */
void PhoenixBootAsh(void *Args)
{
	/* Cast the argument */
	MCoreAsh_t *Ash = (MCoreAsh_t*)Args;

	/* Finish boot */
	PhoenixFinishAsh(Ash);

	/* Go to user-land */
	ThreadingEnterUserMode(Ash);
}

/* This function loads the executable and
 * prepares the ash-environment, at this point
 * it won't be completely running yet, it needs
 * its own thread for that. Returns 0 on success */
int PhoenixInitializeAsh(MCoreAsh_t *Ash, MString_t *Path)
{ 
	/* Variables needed for init */
	MCoreFileInstance_t *File = NULL;
	uint8_t *fBuffer = NULL;
	size_t fSize = 0;
	int Index = 0;

	/* Sanity */
	if (Path == NULL) {
		return -1;
	}

	/* Reset the Ash structure to be safe
	 * we don't want random data there */
	memset(Ash, 0, sizeof(MCoreAsh_t));

	/* Open File 
	 * We have a special case here
	 * in case we are loading from RD */
	if (MStringFindChars(Path, "rd:/") != MSTRING_NOT_FOUND) { 
		Ash->Path = MStringCreate((void*)MStringRaw(Path), StrUTF8);
		if (ModulesQueryPath(Path, &fBuffer, &fSize) != OsNoError) {
			return -2;
		}
	}
	else {
		File = VfsWrapperOpen(MStringRaw(Path), Read);

		/* Sanity */
		if (File->Code != VfsOk
			|| File->File == NULL) {
			VfsWrapperClose(File);
			return -2;
		}

		/* Allocate a buffer */
		fSize = (size_t)File->File->Size;
		fBuffer = (uint8_t*)kmalloc(fSize);

		/* Read */
		VfsWrapperRead(File, fBuffer, fSize);

		/* Save a copy of the path */
		Ash->Path = MStringCreate(
			(void*)MStringRaw(File->File->Path), StrUTF8);

		/* Close */
		VfsWrapperClose(File);
	}

	/* Validate File */
	if (!PeValidate(fBuffer, fSize)) {
		kfree(fBuffer);
		return -3;
	}

	/* Set initial */
	Ash->Id = GlbAshIdGenerator++;
	Ash->Parent = ThreadingGetCurrentThread(ApicGetCpu())->AshId;
	Ash->Type = AshBase;

	/* Split path, even if a / is not found
	 * it won't fail, since -1 + 1 = 0, so we just copy
	 * the entire string */
	Index = MStringFindReverse(Ash->Path, '/');
	Ash->Name = MStringSubString(Ash->Path, Index + 1, -1);

	/* Save file buffer */
	Ash->FileBuffer = fBuffer;
	Ash->FileBufferLength = fSize;

	/* Create the pipe list, there are as a default
	 * no pipes open for a process, the process must
	 * open the pipes it need */
	Ash->Pipes = ListCreate(KeyInteger, LIST_SAFE);

	/* Return success! */
	return 0;
}

/* This is a wrapper for starting up a base Ash
 * and uses <PhoenixInitializeAsh> to setup the env
 * and do validation before starting */
UUId_t PhoenixStartupAsh(MString_t *Path)
{
	/* Variables needed for this */
	MCoreAsh_t *Ash = NULL;
	DataKey_t Key;

	/* Allocate the structure */
	Ash = (MCoreAsh_t*)kmalloc(sizeof(MCoreAsh_t));

	/* Sanitize the created Ash */
	if (PhoenixInitializeAsh(Ash, Path)) {
		kfree(Ash);
		return PHOENIX_NO_ASH;
	}

	/* Add process to list */
	Key.Value = (int)Ash->Id;
	ListAppend(GlbAshes, ListCreateNode(Key, Key, Ash));

	/* Create the loader thread */
	ThreadingCreateThread((char*)MStringRaw(Ash->Name),
		PhoenixBootAsh, Ash, THREADING_USERMODE);

	/* Done */
	return Ash->Id;
}

/* These function manipulate pipes on the given port
 * there are some pre-defined ports on which pipes
 * can be opened, window manager etc */
int PhoenixOpenAshPipe(MCoreAsh_t *Ash, int Port, Flags_t Flags)
{
	/* Variables */
	MCorePipe_t *Pipe = NULL;
	DataKey_t Key;
	Key.Value = Port;

	/* Sanitize the parameters */
	if (Ash == NULL || Port < 0) {
		return -1;
	}

	/* Make sure that a pipe on the given Port 
	 * doesn't already exist! */
	if (ListGetDataByKey(Ash->Pipes, Key, 0) != NULL) {
		return -2;
	}

	/* Open a new pipe */
	Pipe = PipeCreate(ASH_PIPE_SIZE, Flags);

	/* Add it to the list */
	ListAppend(Ash->Pipes, ListCreateNode(Key, Key, Pipe));
	SchedulerWakeupAllThreads((Addr_t*)Ash->Pipes);

	/* The pipe is now created and ready
	 * for use by the process */
	return 0;
}

/* Waits for a pipe to be opened on the given
 * ash instance, be careful as you can wait forever
 * if you don't know what you're doing */
int PhoenixWaitAshPipe(MCoreAsh_t *Ash, int Port)
{
	/* Variables */
	DataKey_t Key;
	int Run = 1;

	/* Sanitize parameters */
	if (Ash == NULL) {
		return -1;
	}

	/* Set key */
	Key.Value = Port;

	/* Sleep on the given pipes */
	while (Run) {
		if (ListGetDataByKey(Ash->Pipes, Key, 0) != NULL) {
			break;
		}
		SchedulerSleepThread((Addr_t*)Ash->Pipes, 0);
		IThreadYield();
 	}

	/* Done! */
	return 0;
}

/* Closes the pipe for the given Ash, and cleansup
 * resources allocated by the pipe. This shutsdown
 * any communication on the port */
int PhoenixCloseAshPipe(MCoreAsh_t *Ash, int Port)
{
	/* Variables */
	MCorePipe_t *Pipe = NULL;
	DataKey_t Key;
	Key.Value = Port;

	/* Sanitize the parameters */
	if (Ash == NULL || Port < 0) {
		return -1;
	}

	/* There must exist a pipe on the port, otherwise
	 * we'll throw an error! */
	Pipe = (MCorePipe_t*)ListGetDataByKey(Ash->Pipes, Key, 0);
	if (Pipe == NULL) {
		return -2;
	}

	/* Cleanup the pipe */
	PipeDestroy(Pipe);

	/* Remove entry from list */
	return ListRemoveByKey(Ash->Pipes, Key) == 1 ? 0 : -1;
}

/* Retrieves a pipe for the given port, if it doesn't
 * exist, it returns NULL, otherwise a pointer to the
 * given pipe is returned */
MCorePipe_t *PhoenixGetAshPipe(MCoreAsh_t *Ash, int Port)
{
	/* Variables */
	DataKey_t Key;
	Key.Value = Port;

	/* Sanitize the parameters */
	if (Ash == NULL || Port < 0) {
		return NULL;
	}

	/* Return the data by the given key */
	return (MCorePipe_t*)ListGetDataByKey(Ash->Pipes, Key, 0);
}

/* Queries the given ash for information
 * which kind of information is determined by <Function> */
int PhoenixQueryAsh(MCoreAsh_t *Ash,
	AshQueryFunction_t Function, void *Buffer, size_t Length)
{
	/* Sanitize the parameters */
	if (Ash == NULL
		|| Buffer == NULL
		|| Length == 0)
		return -1;

	/* Now.. What do you want to know? */
	switch (Function)
	{
		/* Query name? */
		case AshQueryName:
		{
			/* Never copy more data than user can contain */
			size_t BytesToCopy = MIN(MStringSize(Ash->Name), Length);

			/* Cooopy */
			memcpy(Buffer, MStringRaw(Ash->Name), BytesToCopy);

		} break;

		/* Query memory? in bytes of course */
		case AshQueryMemory:
		{
			/* For now, we only support querying of
			 * how much memory has been allocated for the heap */
			*((size_t*)Buffer) = (Ash->Heap->BlocksAllocated * PAGE_SIZE);

		} break;

		/* Query immediate parent? */
		case AshQueryParent:
		{
			/* Get a pointer */
			UUId_t *bPtr = (UUId_t*)Buffer;

			/* There we go */
			*bPtr = Ash->Parent;

		} break;

		/* Query topmost parent? */
		case AshQueryTopMostParent:
		{
			/* Get a pointer */
			UUId_t *bPtr = (UUId_t*)Buffer;

			/* Set initial value */
			*bPtr = PHOENIX_NO_ASH;

			/* While parent has a valid parent */
			MCoreAsh_t *Parent = Ash;
			while (Parent->Parent != PHOENIX_NO_ASH) {
				*bPtr = Parent->Parent;
				Parent = PhoenixGetAsh(Parent->Parent);
			}

		} break;

		default:
			break;
	}

	/* Done! */
	return 0;
}

/* Cleans up a given Ash, freeing all it's allocated resources
 * and unloads it's executables, memory space is not cleaned up
 * must be done by external thread */
void PhoenixCleanupAsh(MCoreAsh_t *Ash)
{
	/* Vars */
	ListNode_t *fNode = NULL;

	/* Cleanup Strings */
	MStringDestroy(Ash->Name);
	MStringDestroy(Ash->Path);

	/* Destroy signal stuff */
	_foreach(fNode, Ash->SignalQueue) {
		/* Free the signal */
		kfree(fNode->Data);
	}

	/* Destroy the signal list */
	ListDestroy(Ash->SignalQueue);

	/* Destroy all open pipies */
	_foreach(fNode, Ash->Pipes) {
		PipeDestroy((MCorePipe_t*)fNode->Data);
	}

	/* Now destroy the pipe-list */
	ListDestroy(Ash->Pipes);

	/* Cleanup memory allocators */
	BitmapDestroy(Ash->Shm);
	BitmapDestroy(Ash->Heap);

	/* Cleanup executable data */
	PeUnloadImage(Ash->Executable);

	/* Rest of memory is cleaned during
	* address space cleanup */
	kfree(Ash);
}

/* PhoenixRegisterAlias
 * Allows a server to register an alias for its id
 * which means that id (must be above SERVER_ALIAS_BASE)
 * will always refer the calling process */
OsStatus_t PhoenixRegisterAlias(MCoreAsh_t *Ash, UUId_t Alias)
{
	/* Sanitize both the server and alias */
	if (Ash == NULL
		|| Alias < PHOENIX_ALIAS_BASE
		|| GlbAliasMap[PHOENIX_ALIAS_BASE - Alias] != PHOENIX_NO_ASH) {
		return OsError;
	}

	/* Register */
	GlbAliasMap[PHOENIX_ALIAS_BASE - Alias] = Ash->Id;

	/* Return no error */
	return OsNoError;
}

/* Lookup Ash
 * This function looks up a ash structure
 * by id, if either PHOENIX_CURRENT or PHOENIX_NO_ASH
 * is passed, it retrieves the current process */
MCoreAsh_t *PhoenixGetAsh(UUId_t AshId)
{
	/* Variables */
	ListNode_t *pNode = NULL;
	Cpu_t CurrentCpu = 0;

	/* Sanity the list, no need to check in
	 * this case */
	if (ListLength(GlbAshes) == 0) {
		return NULL;
	}
	
	/* Sanitize the process id */
	if (AshId == PHOENIX_CURRENT
		|| AshId == PHOENIX_NO_ASH)
	{
		/* Get current cpu id */
		CurrentCpu = ApicGetCpu();

		/* Sanitize threading is up */
		if (ThreadingGetCurrentThread(CurrentCpu) != NULL) {
			AshId = ThreadingGetCurrentThread(CurrentCpu)->AshId;
		}
		else {
			return NULL;
		}
	}

	/* Now we can sanitize the extra stuff,
	 * like alias */
	if (AshId >= PHOENIX_ALIAS_BASE
		&& AshId < (PHOENIX_ALIAS_BASE + PHOENIX_MAX_ASHES)) {
		AshId = GlbAliasMap[PHOENIX_ALIAS_BASE - AshId];
	}

	/* Iterate the list and try
	 * to locate the ash we have */
	_foreach(pNode, GlbAshes) {
		MCoreAsh_t *Ash = (MCoreAsh_t*)pNode->Data;
		if (Ash->Id == AshId) {
			return Ash;
		}
	}

	/* Found? NO! */
	return NULL;
}
