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
#include <os/spinlock.h>
#include <ds/list.h>
#include <gracht/server.h>
#include <threads.h>
#include <time.h>

// Forward declarations
DECL_STRUCT(PeExecutable);
DECL_STRUCT(MString);

#define PROCESS_RUNNING     0
#define PROCESS_TERMINATING 1

typedef struct Process {
    element_t              Header;
    UUId_t                 PrimaryThreadId;
    clock_t                StartedAt;
    atomic_int             References;
    int                    State;
    spinlock_t             SyncObject;

    MString_t*             Name;
    MString_t*             Path;
    MString_t*             WorkingDirectory;
    MString_t*             AssemblyDirectory;
    const char*            Arguments;
    size_t                 ArgumentsLength;
    void*                  InheritationBlock;
    size_t                 InheritationBlockLength;
    ProcessConfiguration_t Configuration;

    PeExecutable_t*        Executable;
    int                    ExitCode;
} Process_t;

typedef struct ProcessJoiner {
    element_t             Header;
    Process_t*            Process;
    UUId_t                EventHandle;
    struct gracht_message DeferredResponse[];
} ProcessJoiner_t;

/**
 * Initializes the subsystems for managing the running processes, providing manipulations and optimizations.
 */
__EXTERN OsStatus_t
InitializeProcessManager(void);

/**
 * DebuggerInitialize
 * Initializes the debugger functionality in the process manager
 */
__EXTERN void
DebuggerInitialize(void);

/* AcquireProcess
 * Acquires a reference to a process and allows safe access to the structure. */
__EXTERN Process_t*
AcquireProcess(
    _In_ UUId_t Handle);

/* ReleaseProcess
 * Releases a reference to a process and unlocks the process structure for other threads. */
void
ReleaseProcess(
    _In_ Process_t* process);

/* GetProcessByPrimaryThread
 * Looks up a process instance by its primary thread. This can be used by the
 * primary thread to obtain its process id. */
__EXTERN Process_t*
GetProcessByPrimaryThread(
    _In_ UUId_t ThreadId);

#endif //!__PROCESS_INTERFACE__
