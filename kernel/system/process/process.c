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
 * MollenOS MCore - Processes Implementation
 */

/* Includes 
 * - System */
#include <process/process.h>
#include <garbagecollector.h>
#include <threading.h>
#include <semaphore.h>
#include <scheduler.h>
#include <heap.h>
#include <log.h>

/* Includes
 * - C-Library */
#include <stddef.h>
#include <ds/list.h>
#include <ds/mstring.h>
#include <string.h>

/* Externs */
__EXTERN List_t *GlbAshes;

/* This is the finalizor function for starting
 * up a new Ash Process, it finishes setting up the environment
 * and memory mappings, must be called on it's own thread */
void PhoenixBootProcess(void *Args)
{
	/* Cast the arguments */
	MCoreProcess_t *Process = (MCoreProcess_t*)Args;

	/* Finish the standard setup of the ash */
	PhoenixFinishAsh(&Process->Base);

	/* Map in arguments */
	AddressSpaceMap(AddressSpaceGetCurrent(), MEMORY_LOCATION_RING3_ARGS,
		PAGE_SIZE, MEMORY_MASK_DEFAULT, ADDRESS_SPACE_FLAG_APPLICATION);

	/* Copy arguments */
	memcpy((void*)MEMORY_LOCATION_RING3_ARGS,
		MStringRaw(Process->Arguments), MStringSize(Process->Arguments));

	/* Go to user-land */
	ThreadingEnterUserMode(Process);
}

/* This function loads the executable and
 * prepares the ash-process-environment, at this point
 * it won't be completely running yet, it needs
 * its own thread for that */
UUId_t PhoenixCreateProcess(MString_t *Path, MString_t *Arguments)
{
	/* Vars */
	MCoreProcess_t *Process = NULL;
	DataKey_t Key;
	int Index = 0;

	/* Allocate the structure */
	Process = (MCoreProcess_t*)kmalloc(sizeof(MCoreProcess_t));

	/* Sanitize the created Ash */
	if (PhoenixInitializeAsh(&Process->Base, Path)) {
		kfree(Process);
		return PHOENIX_NO_ASH;
	}

	/* Set type of base to process */
	Process->Base.Type = AshProcess;

	/* Split path and setup working directory
	 * but also base directory for the exe */
	Index = MStringFindReverse(Process->Base.Path, '/');
	Process->WorkingDirectory = MStringSubString(Process->Base.Path, 0, Index);
	Process->BaseDirectory = MStringSubString(Process->Base.Path, 0, Index);

	/* Save arguments */
	if (Arguments != NULL
		&& MStringSize(Arguments) != 0) {
		Process->Arguments = MStringCreate((void*)MStringRaw(Process->Base.Path), StrUTF8);
		MStringAppendCharacter(Process->Arguments, ' ');
		MStringAppendString(Process->Arguments, Arguments);
	}
	else
		Process->Arguments = MStringCreate((void*)MStringRaw(Process->Base.Path), StrUTF8);

	/* Add process to list */
	Key.Value = (int)Process->Base.Id;
	ListAppend(GlbAshes, ListCreateNode(Key, Key, Process));

	/* Create the loader thread */
	ThreadingCreateThread((char*)MStringRaw(Process->Base.Name), 
		PhoenixBootProcess, Process, THREADING_USERMODE);

	/* Done */
	return Process->Base.Id;
}

/* Cleans up all the process-specific resources allocated
 * this this AshProcess, and afterwards call the base-cleanup */
void PhoenixCleanupProcess(MCoreProcess_t *Process)
{
	/* Cleanup Strings */
	MStringDestroy(Process->Arguments);
	MStringDestroy(Process->WorkingDirectory);
	MStringDestroy(Process->BaseDirectory);

	/* Now that we have cleaned up all 
	 * process-specifics, we want to just use the base
	 * cleanup */
	PhoenixCleanupAsh((MCoreAsh_t*)Process);
}

/* Get Process 
 * This function looks up a process structure 
 * by id, if either PROCESS_CURRENT or PROCESS_NO_PROCESS 
 * is passed, it retrieves the current process */
MCoreProcess_t *PhoenixGetProcess(UUId_t ProcessId)
{
	/* Use the default Ash lookup */
	MCoreAsh_t *Ash = PhoenixGetAsh(ProcessId);

	/* Other than null check, we do a process-check */
	if (Ash != NULL
		&& Ash->Type != AshProcess) {
		return NULL;
	}

	/* Return the result, but cast it to 
	 * the process structure */
	return (MCoreProcess_t*)Ash;
}

/* Get the working directory 
 * This function looks up the working directory for a process 
 * by id, if either PROCESS_CURRENT or PROCESS_NO_PROCESS 
 * is passed, it retrieves the current process's working directory */
MString_t *PhoenixGetWorkingDirectory(UUId_t ProcessId)
{
	/* Variables */
	MCoreProcess_t *Process = PhoenixGetProcess(ProcessId);

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
MString_t *PhoenixGetBaseDirectory(UUId_t ProcessId)
{
	/* Variables */
	MCoreProcess_t *Process = PhoenixGetProcess(ProcessId);

	/* Sanitize result */
	if (Process != NULL) {
		return Process->BaseDirectory;
	}
	else {
		return NULL;
	}
}
