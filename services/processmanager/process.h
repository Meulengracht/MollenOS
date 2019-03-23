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
#include <os/services/process.h>
#include <os/spinlock.h>
#include <ds/collection.h>
#include <ddk/ipc/ipc.h>
#include <time.h>

// Forward declarations
DECL_STRUCT(PeExecutable);
DECL_STRUCT(MString);

#define PROCESS_RUNNING     0
#define PROCESS_TERMINATING 1

typedef struct _Process {
    CollectionItem_t            Header;
    UUId_t                      PrimaryThreadId;
    clock_t                     StartedAt;
    atomic_int                  References;
    int                         State;
    Spinlock_t                  SyncObject;

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

typedef struct _ProcessJoiner {
    CollectionItem_t     Header;
    MRemoteCallAddress_t Address;
    Process_t*           Process;
    UUId_t               EventHandle;
} ProcessJoiner_t;

/* InitializeProcessManager
 * Initializes the subsystems for managing the running processes, providing manipulations and optimizations. */
__EXTERN OsStatus_t
InitializeProcessManager(void);

/* CreateProcess
 * Spawns a new process, which can be configured through the parameters. */
__EXTERN OsStatus_t
CreateProcess(
    _In_  UUId_t                       Owner,
    _In_  const char*                  Path,
    _In_  ProcessStartupInformation_t* Parameters,
    _In_  const char*                  Arguments,
    _In_  size_t                       ArgumentsLength,
    _In_  void*                        InheritationBlock,
    _In_  size_t                       InheritationBlockLength,
    _Out_ UUId_t*                      Handle);

/* JoinProcess
 * Waits for the process to exit and returns the exit code. A timeout can optionally be specified. */
__EXTERN OsStatus_t
JoinProcess(
    _In_  Process_t*            Process,
    _In_  MRemoteCallAddress_t* Address,
    _In_  size_t                Timeout);

/* KillProcess
 * Request to kill a process. If security checks pass the processmanager will shutdown the process. */
__EXTERN OsStatus_t
KillProcess(
    _In_ Process_t* Killer,
    _In_ Process_t* Target);

/* TerminateProcess
 * Terminates the calling process. Immediately after this the calling process must exit on its own. */
__EXTERN OsStatus_t
TerminateProcess(
    _In_ Process_t* Process,
    _In_ int        ExitCode);

/* LoadProcessLibrary
 * Try to dynamically load a library for the calling process into its memory space. */
__EXTERN OsStatus_t
LoadProcessLibrary(
    _In_  Process_t*  Process,
    _In_  const char* Path,
    _Out_ Handle_t*   HandleOut);

/* ResolveProcessLibraryFunction
 * Resolves a function address with the given name. */
__EXTERN uintptr_t
ResolveProcessLibraryFunction(
    _In_ Process_t*  Process,
    _In_ Handle_t    Handle,
    _In_ const char* Function);

/* UnloadProcessLibrary
 * Unloads a previously dynamically loaded library. */
__EXTERN OsStatus_t
UnloadProcessLibrary(
    _In_ Process_t* Process,
    _In_ Handle_t   Handle);

/* GetProcessLibraryHandles
 * Retrieves a list of loaded module handles currently loaded for the process. */
__EXTERN OsStatus_t
GetProcessLibraryHandles(
    _In_  Process_t* Process,
    _Out_ Handle_t   LibraryList[PROCESS_MAXMODULES]);

/* GetProcessLibraryEntryPoints
 * Retrieves a list of loaded module entry points currently loaded for the process. */
__EXTERN OsStatus_t
GetProcessLibraryEntryPoints(
    _In_  Process_t* Process,
    _Out_ Handle_t   LibraryList[PROCESS_MAXMODULES]);

/* HandleProcessCrashReport
 * Finds the module, and the relevant offset into that module. */
__EXTERN OsStatus_t
HandleProcessCrashReport(
    _In_ Process_t* Process,
    _In_ Context_t* CrashContext,
    _In_ int        CrashReason);

/* AcquireProcess
 * Acquires a reference to a process and allows safe access to the structure. */
__EXTERN Process_t*
AcquireProcess(
    _In_ UUId_t Handle);

/* ReleaseProcess
 * Releases a reference to a process and unlocks the process structure for other threads. */
void
ReleaseProcess(
    _In_ Process_t* Process);

/* GetProcessByPrimaryThread
 * Looks up a process instance by its primary thread. This can be used by the
 * primary thread to obtain its process id. */
__EXTERN Process_t*
GetProcessByPrimaryThread(
    _In_ UUId_t ThreadId);

#endif //!__PROCESS_INTERFACE__
