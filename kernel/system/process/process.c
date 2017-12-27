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
#define __MODULE "PCIF"
#define __TRACE

/* Includes 
 * - System */
#include <process/process.h>
#include <process/phoenix.h>
#include <garbagecollector.h>
#include <threading.h>
#include <semaphore.h>
#include <scheduler.h>
#include <debug.h>
#include <heap.h>

/* Includes
 * - Library */
#include <ds/mstring.h>
#include <stddef.h>
#include <string.h>

/* PhoenixCreateProcess
 * This function loads the executable and
 * prepares the ash-process-environment, at this point
 * it won't be completely running yet, it needs
 * its own thread for that */
UUId_t
PhoenixCreateProcess(
    _In_ MString_t                   *Path,
    _In_ ProcessStartupInformation_t *StartupInformation)
{
	// Variables
	MCoreProcess_t *Process = NULL;
    char *ArgumentBuffer    = NULL;
	int Index               = 0;

    // Debug
    TRACE("PhoenixCreateProcess()");

	// Initiate a new instance
	Process = (MCoreProcess_t*)kmalloc(sizeof(MCoreProcess_t));
	if (PhoenixInitializeAsh(&Process->Base, Path) != OsSuccess) {
        ERROR("Failed to initialize the base process");
		kfree(Process);
		return UUID_INVALID;
	}

	// Split path and setup working directory
	// but also base directory for the exe 
	Process->Base.Type = AshProcess;
	Index = MStringFindReverse(Process->Base.Path, '/');
	Process->WorkingDirectory = MStringSubString(Process->Base.Path, 0, Index);
	Process->BaseDirectory = MStringSubString(Process->Base.Path, 0, Index);

	// Handle startup information
	if (StartupInformation->ArgumentPointer != NULL 
        && StartupInformation->ArgumentLength != 0) {
        ArgumentBuffer = (char*)kmalloc(MStringSize(Process->Base.Path) + 1 + StartupInformation->ArgumentLength);
        memcpy(ArgumentBuffer, MStringRaw(Process->Base.Path), MStringSize(Process->Base.Path));
        ArgumentBuffer[MStringSize(Process->Base.Path)] = ' ';
        memcpy(ArgumentBuffer + MStringSize(Process->Base.Path) + 1,
            StartupInformation->ArgumentPointer, StartupInformation->ArgumentLength);
        kfree((void*)StartupInformation->ArgumentPointer);
        StartupInformation->ArgumentPointer = ArgumentBuffer;
        StartupInformation->ArgumentLength = MStringSize(Process->Base.Path) + 1 + StartupInformation->ArgumentLength;
	}
	else {
        ArgumentBuffer = (char*)kmalloc(MStringSize(Process->Base.Path));
        memcpy(ArgumentBuffer, MStringRaw(Process->Base.Path), MStringSize(Process->Base.Path));
        StartupInformation->ArgumentPointer = ArgumentBuffer;
        StartupInformation->ArgumentLength = MStringSize(Process->Base.Path);
    }

    // Debug
    TRACE("Arguments: %s", ArgumentBuffer);

    // Copy data over
    memcpy(&Process->StartupInformation, 
        StartupInformation, sizeof(ProcessStartupInformation_t));

	// Register ash and spawn it
	PhoenixRegisterAsh(&Process->Base);
	ThreadingCreateThread((char*)MStringRaw(Process->Base.Name), 
		PhoenixStartupEntry, Process, THREADING_USERMODE);
	return Process->Base.Id;
}

/* PhoenixCleanupProcess
 * Cleans up all the process-specific resources allocated
 * this this AshProcess, and afterwards call the base-cleanup */
void
PhoenixCleanupProcess(
    _In_ MCoreProcess_t *Process)
{
	// Cleanup resources
    if (Process->StartupInformation.ArgumentPointer != NULL) {
	    kfree((void*)Process->StartupInformation.ArgumentPointer);
    }
    if (Process->StartupInformation.InheritanceBlockPointer != NULL) {
        kfree((void*)Process->StartupInformation.InheritanceBlockPointer);
    }
	MStringDestroy(Process->WorkingDirectory);
	MStringDestroy(Process->BaseDirectory);

    // Base cleanup
	PhoenixCleanupAsh(&Process->Base);
}

/* PhoenixGetProcess
 * This function looks up a server structure by id */
MCoreProcess_t*
PhoenixGetProcess(
	_In_ UUId_t ProcessId)
{
	// Use the default ash-lookup
	MCoreAsh_t *Ash = PhoenixGetAsh(ProcessId);
	if (Ash != NULL && Ash->Type != AshProcess) {
		return NULL;
	}
	return (MCoreProcess_t*)Ash;
}

/* PhoenixGetCurrentProcess
 * If the current running process is a process then it
 * returns the process structure, otherwise NULL */
MCoreProcess_t*
PhoenixGetCurrentProcess(void)
{
	// Use the default get current
	MCoreAsh_t *Ash = PhoenixGetCurrentAsh();
	if (Ash != NULL && Ash->Type != AshProcess) {
		return NULL;
	}
	return (MCoreProcess_t*)Ash;
}

/* PhoenixGetWorkingDirectory
 * This function looks up the working directory for a process 
 * by id, if either PROCESS_CURRENT or PROCESS_NO_PROCESS 
 * is passed, it retrieves the current process's working directory */
MString_t*
PhoenixGetWorkingDirectory(
    _In_ UUId_t ProcessId)
{
	MCoreProcess_t *Process = PhoenixGetProcess(ProcessId);
	if (Process != NULL) {
		return Process->WorkingDirectory;
	}
	else {
		return NULL;
	}
}

/* PhoenixGetBaseDirectory
 * This function looks up the base directory for a process 
 * by id, if either PROCESS_CURRENT or PROCESS_NO_PROCESS 
 * is passed, it retrieves the current process's base directory */
MString_t*
PhoenixGetBaseDirectory(
    _In_ UUId_t ProcessId)
{
	MCoreProcess_t *Process = PhoenixGetProcess(ProcessId);
	if (Process != NULL) {
		return Process->BaseDirectory;
	}
	else {
		return NULL;
	}
}
