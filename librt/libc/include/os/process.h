/* MollenOS
 *
 * Copyright 2019, Philip Meulengracht
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
 * Process Service Definitions & Structures
 * - This header describes the base process-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __PROCESS_H__
#define __PROCESS_H__

#include <os/osdefs.h>
#include <os/types/process.h>
#include <time.h>

_CODE_BEGIN

/* ProcessConfigurationInitialize
 * Resets all values of the startup information structure to default values. */
CRTDECL(void,
ProcessConfigurationInitialize(
	_In_ ProcessConfiguration_t* Configuration));

/* ProcessSpawn
 * Spawns a new process by the given path and optionally the given parameters are passed 
 * returns UUID_INVALID in case of failure. */
CRTDECL(OsStatus_t,
ProcessSpawn(
	_In_     const char* Path,
	_In_Opt_ const char* Arguments,
    _Out_    UUId_t*     HandleOut));

/* ProcessSpawnEx
 * Spawns a new process by the given path and the given startup information block. 
 * Returns UUID_INVALID in case of failure. */
CRTDECL(OsStatus_t,
ProcessSpawnEx(
    _In_     const char*             Path,
    _In_Opt_ const char*             Arguments,
    _In_     ProcessConfiguration_t* Configuration,
    _Out_    UUId_t*                 HandleOut));

/* ProcessJoin
 * Waits for the given process to terminate and
 * returns the return-code the process exit'ed with */
CRTDECL(OsStatus_t,
ProcessJoin(
	_In_  UUId_t Handle,
    _In_  size_t Timeout,
    _Out_ int*   ExitCode));

/**
 * Dispatches a signal to the target process, the target process must be listening to asynchronous signals
 * otherwise the signal is ignored. Both SIGKILL and SIGQUIT will terminate the process in any event, if security
 * checks are passed.
 * @param Handle The handle of the target process
 * @param Signal The signal that should be sent to the process
 * @return       The status of the operation
 */
CRTDECL(OsStatus_t,
ProcessSignal(
    _In_ UUId_t Handle,
    _In_ int    Signal));

/* ProcessGetCurrentId
 * Retrieves the current process identifier. */
CRTDECL(UUId_t, 
ProcessGetCurrentId(void));

/* ProcessGetTickBase
 * Retrieves the current process tick base. The tick base is set upon process startup. */
CRTDECL(OsStatus_t, 
ProcessGetTickBase(
    _Out_ clock_t* Tick));

/* GetProcessCommandLine
 * Retrieves startup information about the process. 
 * Data buffers must be supplied with a max length. */
CRTDECL(OsStatus_t,
GetProcessCommandLine(
    _In_    char*   Buffer,
    _InOut_ size_t* Length));

/* ProcessGetCurrentName
 * Retrieves the current process identifier. */
CRTDECL(OsStatus_t, 
ProcessGetCurrentName(
    _In_ char*  Buffer,
    _In_ size_t MaxLength));

/* ProcessGetAssemblyDirectory
 * Retrieves the current assembly directory of a process handle. Use UUID_INVALID for the
 * current process. */
CRTDECL(OsStatus_t, 
ProcessGetAssemblyDirectory(
    _In_ UUId_t Handle,
    _In_ char*  Buffer,
    _In_ size_t MaxLength));

/* ProcessGetWorkingDirectory
 * Retrieves the current working directory of a process handle. Use UUID_INVALID for the
 * current process. */
CRTDECL(OsStatus_t, 
ProcessGetWorkingDirectory(
    _In_ UUId_t Handle,
    _In_ char*  Buffer,
    _In_ size_t MaxLength));

/* ProcessSetWorkingDirectory
 * Sets the working directory of the current process. */
CRTDECL(OsStatus_t, 
ProcessSetWorkingDirectory(
    _In_ const char* Path));

_CODE_END
#endif //!__PROCESS_H__
