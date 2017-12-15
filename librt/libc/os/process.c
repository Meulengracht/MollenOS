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

/* Includes 
 * - System */
#include <os/process.h>
#include <os/syscall.h>

/* Includes
 * - Library */
#include <stddef.h>
#include <string.h>

/* ProcessSpawn
 * Spawns a new process by the given path and
 * optionally the given parameters are passed 
 * returns UUID_INVALID in case of failure unless Asynchronous is set
 * then this call will always result in UUID_INVALID. */
UUId_t 
ProcessSpawn(
	_In_ __CONST char *Path,
	_In_Opt_ __CONST char *Arguments,
	_In_ int Asynchronous)
{
    // Variables
    ProcessStartupInformation_t StartupInformation;

	// Sanitize parameters
	if (Path == NULL) {
		return UUID_INVALID;
	}

    // Setup information block
    memset(&StartupInformation, 0, sizeof(ProcessStartupInformation_t));
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
	_In_ __CONST char *Path,
	_In_ __CONST ProcessStartupInformation_t *StartupInformation,
	_In_ int Asynchronous) {
	return Syscall_ProcessSpawn(Path, StartupInformation, Asynchronous);
}

/* ProcessJoin
 * Waits for the given process to terminate and
 * returns the return-code the process exit'ed with */
int 
ProcessJoin(
	_In_ UUId_t Process)
{
	/* Sanitize the given id */
	if (Process == UUID_INVALID) {
		return -1;
	}
	return Syscall_ProcessJoin(Process);
}

/* ProcessKill
 * Terminates the process with the given id */
OsStatus_t 
ProcessKill(
	_In_ UUId_t Process)
{
	/* Sanitize the given id */
	if (Process == UUID_INVALID) {
		return OsError;
	}
	return Syscall_ProcessKill(Process);
}

/* ProcessQuery
 * Queries information about the given process
 * based on the function it returns the requested information */
OsStatus_t 
ProcessQuery(
	_In_ UUId_t Process, 
	_In_ ProcessQueryFunction_t Function, 
	_In_ void *Buffer, 
	_In_ size_t Length)
{
	return Syscall_ProcessQuery(Process, Function, Buffer, Length);
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
