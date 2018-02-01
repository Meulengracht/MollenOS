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
 * - This header describes the process structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _PROCESS_INTERFACE_H_
#define _PROCESS_INTERFACE_H_

/* Includes
 * - Library */
#include <os/osdefs.h>
#include <os/pe.h>

#define PROCESS_MAXMODULES          128

/* ProcessStartupInformation
 * Contains information about the process startup. Can be queried
 * from the operating system during process startup. */
typedef struct _ProcessStartupInformation {
    const char*     ArgumentPointer;
    size_t          ArgumentLength;
    const char*     InheritanceBlockPointer;
    size_t          InheritanceBlockLength;
    int             InheritStdHandles;
    size_t          MemoryLimit;
} ProcessStartupInformation_t;

_CODE_BEGIN
/* ProcessSpawn
 * Spawns a new process by the given path and optionally the given parameters are passed 
 * returns UUID_INVALID in case of failure unless Asynchronous is set
 * then this call will always result in UUID_INVALID. */
CRTDECL(
UUId_t,
ProcessSpawn(
	_In_     const char*    Path,
	_In_Opt_ const char*    Arguments,
	_In_     int            Asynchronous));

/* ProcessSpawnEx
 * Spawns a new process by the given path and the given startup information block. 
 * Returns UUID_INVALID in case of failure unless Asynchronous is set
 * then this call will always result in UUID_INVALID. */
CRTDECL(
UUId_t,
ProcessSpawnEx(
	_In_ const char*                        Path,
	_In_ const ProcessStartupInformation_t* StartupInformation,
	_In_ int                                Asynchronous));

/* ProcessJoin
 * Waits for the given process to terminate and
 * returns the return-code the process exit'ed with */
CRTDECL( 
OsStatus_t,
ProcessJoin(
	_In_  UUId_t    ProcessId,
    _In_  size_t    Timeout,
    _Out_ int*      ExitCode));

/* ProcessKill
 * Terminates the process with the given id */
CRTDECL( 
OsStatus_t,
ProcessKill(
	_In_ UUId_t Process));

/* GetStartupInformation
 * Retrieves startup information about the process. 
 * Data buffers must be supplied with a max length. */
CRTDECL(
OsStatus_t,
GetStartupInformation(
    _InOut_ ProcessStartupInformation_t *StartupInformation));

/* ProcessGetCurrentId 
 * Retrieves the current process identifier. */
CRTDECL(
UUId_t, 
ProcessGetCurrentId(void));

/* ProcessGetCurrentName
 * Retrieves the current process identifier. */
CRTDECL(
OsStatus_t, 
ProcessGetCurrentName(const char *Buffer, size_t MaxLength));

/* ProcessGetModuleHandles
 * Retrieves a list of loaded module handles. Handles can be queried
 * for various application-image data. */
CRTDECL(
OsStatus_t,
ProcessGetModuleHandles(
    _Out_ Handle_t ModuleList[PROCESS_MAXMODULES]));
_CODE_END

#endif //!_PROCESS_INTERFACE_H_
