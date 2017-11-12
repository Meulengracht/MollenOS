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
 * MollenOS MCore - Server & Process Management
 * - The process/server manager is known as Phoenix
 */

#ifndef _MCORE_PROCESS_H_
#define _MCORE_PROCESS_H_

/* Includes
* - Library */
#include <os/osdefs.h>
#include <os/process.h>

/* Includes 
 * - System */
#include <process/phoenix.h>

/* The base of an process, processes
 * are derived from Ashes, and merely extensions
 * to support userland stuff */
typedef struct _MCoreProcess {
	MCoreAsh_t                   Base;
	ProcessStartupInformation_t  StartupInformation;
	MString_t                   *WorkingDirectory;
	MString_t                   *BaseDirectory;
} MCoreProcess_t;

/* PhoenixCreateProcess
 * This function loads the executable and
 * prepares the ash-process-environment, at this point
 * it won't be completely running yet, it needs
 * its own thread for that */
KERNELAPI
UUId_t
KERNELABI
PhoenixCreateProcess(
    _In_ MString_t                   *Path,
    _In_ ProcessStartupInformation_t *StartupInformation);

/* PhoenixCleanupProcess
 * Cleans up all the process-specific resources allocated
 * this this AshProcess, and afterwards call the base-cleanup */
KERNELAPI
void
KERNELABI
PhoenixCleanupProcess(
    _In_ MCoreProcess_t *Process);

/* PhoenixGetProcess
 * This function looks up a server structure by id */
KERNELAPI
MCoreProcess_t*
KERNELABI
PhoenixGetProcess(
	_In_ UUId_t ProcessId);

/* PhoenixGetCurrentProcess
 * If the current running process is a server then it
 * returns the server structure, otherwise NULL */
KERNELAPI
MCoreProcess_t*
KERNELABI
PhoenixGetCurrentProcess(void);

/* PhoenixGetWorkingDirectory
 * This function looks up the working directory for a process 
 * by id, if either PROCESS_CURRENT or PROCESS_NO_PROCESS 
 * is passed, it retrieves the current process's working directory */
KERNELAPI
MString_t*
KERNELABI
PhoenixGetWorkingDirectory(
    _In_ UUId_t ProcessId);

/* PhoenixGetBaseDirectory
 * This function looks up the base directory for a process 
 * by id, if either PROCESS_CURRENT or PROCESS_NO_PROCESS 
 * is passed, it retrieves the current process's base directory */
MString_t*
PhoenixGetBaseDirectory(
    _In_ UUId_t ProcessId);

#endif //!_MCORE_PROCESS_H_