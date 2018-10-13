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
 * MollenOS MCore - Process Definitions & Structures
 * - This header describes the base process-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <os/process.h>
#include <os/syscall.h>
#include <os/utils.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include "../stdio/local.h"

/* InitializeStartupInformation
 * Resets all values of the startup information structure to default values. */
void
InitializeStartupInformation(
	_In_ ProcessStartupInformation_t* StartupInformation)
{
    memset(StartupInformation, 0, sizeof(ProcessStartupInformation_t));

    // Reset handles
    StartupInformation->StdOutHandle    = STDOUT_FILENO;
    StartupInformation->StdInHandle     = STDIN_FILENO;
    StartupInformation->StdErrHandle    = STDERR_FILENO;
}

/* ProcessSpawn
 * Spawns a new process by the given path and optionally the given parameters are passed 
 * returns UUID_INVALID in case of failure unless Asynchronous is set
 * then this call will always result in UUID_INVALID. */
UUId_t 
ProcessSpawn(
	_In_     const char*    Path,
	_In_Opt_ const char*    Arguments,
	_In_     int            Asynchronous)
{
    ProcessStartupInformation_t StartupInformation;

	// Sanitize parameters
	if (Path == NULL) {
        _set_errno(EINVAL);
		return UUID_INVALID;
	}

    // Setup information block
    InitializeStartupInformation(&StartupInformation);
    StartupInformation.InheritFlags = PROCESS_INHERIT_NONE;
    if (Arguments != NULL) {
        StartupInformation.ArgumentPointer = Arguments;
        StartupInformation.ArgumentLength = strlen(Arguments);
    }
    return ProcessSpawnEx(Path, &StartupInformation, Asynchronous);
}

/* ProcessSpawnEx
 * Spawns a new process by the given path and the given startup information block. 
 * Returns UUID_INVALID in case of failure unless Asynchronous is set
 * then this call will always result in UUID_INVALID. */
UUId_t
ProcessSpawnEx(
	_In_ const char*                        Path,
	_In_ const ProcessStartupInformation_t* StartupInformation,
	_In_ int                                Asynchronous)
{
    OsStatus_t Cleanup  = StdioCreateInheritanceBlock((ProcessStartupInformation_t*)StartupInformation);
	UUId_t Result       = Syscall_ProcessSpawn(Path, StartupInformation, Asynchronous);
    if (Cleanup == OsSuccess && StartupInformation->InheritanceBlockPointer != NULL) {
        free((void*)StartupInformation->InheritanceBlockPointer);
    }
    return Result;
}

/* ProcessJoin
 * Waits for the given process to terminate and
 * returns the return-code the process exit'ed with */
OsStatus_t 
ProcessJoin(
	_In_  UUId_t    ProcessId,
    _In_  size_t    Timeout,
    _Out_ int*      ExitCode)
{
	if (ProcessId == UUID_INVALID || ExitCode == NULL) {
        _set_errno(EINVAL);
		return OsError;
	}
	return Syscall_ProcessJoin(ProcessId, Timeout, ExitCode);
}

/* ProcessKill
 * Terminates the process with the given id */
OsStatus_t 
ProcessKill(
	_In_ UUId_t Process)
{
	if (Process == UUID_INVALID) {
        _set_errno(EINVAL);
		return OsError;
	}
	return Syscall_ProcessKill(Process);
}

/* ProcessGetCurrentId 
 * Retrieves the current process identifier. */
UUId_t
ProcessGetCurrentId(void)
{
    UUId_t ProcessId;
	if (Syscall_ProcessId(&ProcessId) != OsSuccess) {
        return UUID_INVALID;
    }
    return ProcessId;
}

/* ProcessGetCurrentName
 * Retrieves the current process identifier. */
OsStatus_t
ProcessGetCurrentName(const char *Buffer, size_t MaxLength) {
    return Syscall_ProcessName(Buffer, MaxLength);
}

/* GetStartupInformation
 * Retrieves startup information about the process. 
 * Data buffers must be supplied with a max length. */
OsStatus_t
GetStartupInformation(
    _InOut_ ProcessStartupInformation_t *StartupInformation) {
    return Syscall_ProcessGetStartupInfo(StartupInformation);
}

/* ProcessGetModuleEntryPoints
 * Retrieves a list of loaded modules for the process and
 * their entry points. */
OsStatus_t
ProcessGetModuleEntryPoints(
    _Out_ Handle_t ModuleList[PROCESS_MAXMODULES]) {
    return Syscall_ProcessGetModuleEntryPoints(ModuleList);
}

/* ProcessGetModuleHandles
 * Retrieves a list of loaded module handles. Handles can be queried
 * for various application-image data. */
OsStatus_t
ProcessGetModuleHandles(
    _Out_ Handle_t ModuleList[PROCESS_MAXMODULES]) {
    return Syscall_ProcessGetModuleHandles(ModuleList);
}
