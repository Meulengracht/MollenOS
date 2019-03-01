/* MollenOS
 *
 * Copyright 2018, Philip Meulengracht
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
 * Process Manager Interface
 * - Part of the SDK. Provides process related functionality through the session manager.
 */

#ifndef __PROCESS_INTERFACE_H__
#define __PROCESS_INTERFACE_H__

#include <os/osdefs.h>
#include <time.h>

#define PROCESS_INHERIT_NONE        0x00000000
#define PROCESS_INHERIT_STDOUT      0x00000001
#define PROCESS_INHERIT_STDIN       0x00000002
#define PROCESS_INHERIT_STDERR      0x00000004
#define PROCESS_INHERIT_FILES       0x00000008
#define PROCESS_INHERIT_ALL         (PROCESS_INHERIT_STDOUT | PROCESS_INHERIT_STDIN | PROCESS_INHERIT_STDERR | PROCESS_INHERIT_FILES)

/* ProcessStartupInformation
 * Contains information about the process startup. Can be queried
 * from the operating system during process startup. */
typedef struct _ProcessStartupInformation {
    Flags_t         InheritFlags;
    int             StdOutHandle;
    int             StdInHandle;
    int             StdErrHandle;
    size_t          MemoryLimit;
} ProcessStartupInformation_t;

_CODE_BEGIN
/* InitializeStartupInformation
 * Resets all values of the startup information structure to default values. */
CRTDECL(void,
InitializeStartupInformation(
	_In_ ProcessStartupInformation_t* StartupInformation));

/* ProcessSpawn
 * Spawns a new process by the given path and optionally the given parameters are passed 
 * returns UUID_INVALID in case of failure. */
CRTDECL(UUId_t,
ProcessSpawn(
	_In_     const char* Path,
	_In_Opt_ const char* Arguments));

/* ProcessSpawnEx
 * Spawns a new process by the given path and the given startup information block. 
 * Returns UUID_INVALID in case of failure. */
CRTDECL(UUId_t,
ProcessSpawnEx(
    _In_     const char*                  Path,
    _In_Opt_ const char*                  Arguments,
    _In_     ProcessStartupInformation_t* StartupInformation));

/* ProcessJoin
 * Waits for the given process to terminate and
 * returns the return-code the process exit'ed with */
CRTDECL(OsStatus_t,
ProcessJoin(
	_In_  UUId_t Handle,
    _In_  size_t Timeout,
    _Out_ int*   ExitCode));

/* ProcessKill
 * Terminates the process with the given id */
CRTDECL(OsStatus_t,
ProcessKill(
	_In_ UUId_t Handle));

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
    _In_    const char* Buffer,
    _InOut_ size_t*     Length));

/* ProcessGetCurrentName
 * Retrieves the current process identifier. */
CRTDECL(OsStatus_t, 
ProcessGetCurrentName(
    _In_ const char* Buffer,
    _In_ size_t      MaxLength));

/* ProcessGetAssemblyDirectory
 * Retrieves the current assembly directory of a process handle. Use UUID_INVALID for the
 * current process. */
CRTDECL(OsStatus_t, 
ProcessGetAssemblyDirectory(
    _In_ UUId_t      Handle,
    _In_ const char* Buffer,
    _In_ size_t      MaxLength));

/* ProcessGetWorkingDirectory
 * Retrieves the current working directory of a process handle. Use UUID_INVALID for the
 * current process. */
CRTDECL(OsStatus_t, 
ProcessGetWorkingDirectory(
    _In_ UUId_t      Handle,
    _In_ const char* Buffer,
    _In_ size_t      MaxLength));

/* ProcessSetWorkingDirectory
 * Sets the working directory of the current process. */
CRTDECL(OsStatus_t, 
ProcessSetWorkingDirectory(
    _In_ const char* Path));
_CODE_END

#endif //!_PROCESS_INTERFACE_H_
