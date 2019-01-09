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
 * Process Manager
 * - Contains the implementation of the process-manager which keeps track
 *   of running applications.
 */

#ifndef __PROCESS_INTERFACE__
#define __PROCESS_INTERFACE__

#include <os/osdefs.h>
#include <os/process.h>
#include <ds/collection.h>
#include <time.h>

// Forward declarations
DECL_STRUCT(PeExecutable);
DECL_STRUCT(MString);

typedef struct _Process {
    CollectionItem_t            Header;
    UUId_t                      PrimaryThreadId;
    clock_t                     StartedAt;

    MString_t*                  Name;
    MString_t*                  Path;
    MString_t*                  WorkingDirectory;
    MString_t*                  AssemblyDirectory;
    const char*                 Arguments;
    size_t                      ArgumentsLength;
    void*                       InheritationBlock;
    size_t                      InheritationBlockLength;
    ProcessStartupInformation_t StartupInformation;

    PeExecutable_t*             Executable;
    int                         ExitCode;
} Process_t;

/* CreateProcess
 * Spawns a new process, which can be configured through the parameters. */
OsStatus_t
CreateProcess(
    _In_  const char*                  Path,
    _In_  ProcessStartupInformation_t* Parameters,
    _In_  const char*                  Arguments,
    _In_  size_t                       ArgumentsLength,
    _In_  void*                        InheritationBlock,
    _In_  size_t                       InheritationBlockLength,
    _Out_ UUId_t*                      Handle);


/* GetProcess
 * Retrieve a process instance from its handle. */
Process_t*
GetProcess(
    _In_ UUId_t Handle);

/* GetProcessByPrimaryThread
 * Looks up a process instance by its primary thread. This can be used by the
 * primary thread to obtain its process id. */
Process_t*
GetProcessByPrimaryThread(
    _In_ UUId_t ThreadId);

#endif //!__PROCESS_INTERFACE__
