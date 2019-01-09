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

#define __PROCESSMANAGER_CREATE_PROCESS    IPC_DECL_FUNCTION(0)
#define __PROCESSMANAGER_JOIN_PROCESS      IPC_DECL_FUNCTION(1)
#define __PROCESSMANAGER_KILL_PROCESS      IPC_DECL_FUNCTION(2)
#define __PROCESSMANAGER_TERMINATE_PROCESS IPC_DECL_FUNCTION(3)
#define __PROCESSMANAGER_GET_PROCESS_ID    IPC_DECL_FUNCTION(4)
#define __PROCESSMANAGER_GET_ARGUMENTS     IPC_DECL_FUNCTION(5)
#define __PROCESSMANAGER_GET_INHERIT_BLOCK IPC_DECL_FUNCTION(6)
#define __PROCESSMANAGER_GET_PROCESS_NAME  IPC_DECL_FUNCTION(7)
#define __PROCESSMANAGER_GET_PROCESS_TICK  IPC_DECL_FUNCTION(8)

#define __PROCESSMANAGER_GET_ASSEMBLY_DIRECTORY IPC_DECL_FUNCTION(9)
#define __PROCESSMANAGER_GET_WORKING_DIRECTORY  IPC_DECL_FUNCTION(10)
#define __PROCESSMANAGER_SET_WORKING_DIRECTORY  IPC_DECL_FUNCTION(11)

#define __PROCESSMANAGER_GET_LIBRARY_HANDLES IPC_DECL_FUNCTION(12)
#define __PROCESSMANAGER_GET_LIBRARY_ENTRIES IPC_DECL_FUNCTION(13)
#define __PROCESSMANAGER_LOAD_LIBRARY        IPC_DECL_FUNCTION(14)
#define __PROCESSMANAGER_RESOLVE_FUNCTION    IPC_DECL_FUNCTION(15)
#define __PROCESSMANAGER_UNLOAD_LIBRARY      IPC_DECL_FUNCTION(16)

#define PROCESS_MAXMODULES          128

#define PROCESS_INHERIT_NONE        0x00000000
#define PROCESS_INHERIT_STDOUT      0x00000001
#define PROCESS_INHERIT_STDIN       0x00000002
#define PROCESS_INHERIT_STDERR      0x00000004
#define PROCESS_INHERIT_FILES       0x00000008
#define PROCESS_INHERIT_ALL         (PROCESS_INHERIT_STDOUT | PROCESS_INHERIT_STDIN | PROCESS_INHERIT_STDERR | PROCESS_INHERIT_FILES)

PACKED_TYPESTRUCT(JoinProcessPackage, {
    int ExitCode;
    int Timeout;
});

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

/* ProcessTerminate
 * Terminates the current process that is registered with the process manager.
 * This invalidates every functionality available to this process. */
CRTDECL(OsStatus_t,
ProcessTerminate(
	_In_ int ExitCode));

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

/* GetProcessInheritationBlock
 * Retrieves startup information about the process. 
 * Data buffers must be supplied with a max length. */
CRTDECL(OsStatus_t,
GetProcessInheritationBlock(
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

/* ProcessGetLibraryHandles
 * Retrieves a list of loaded library handles. Handles can be queried
 * for various application-image data. */
CRTDECL(OsStatus_t,
ProcessGetLibraryHandles(
    _Out_ Handle_t LibraryList[PROCESS_MAXMODULES]));

/* ProcessLoadLibrary 
 * Dynamically loads an application extensions for current process. A handle for the new
 * library is set in Handle if OsSuccess is returned. */
CRTDECL(OsStatus_t,
ProcessLoadLibrary(
    _In_  const char* Path,
    _Out_ Handle_t*   Handle));

/* ProcessGetLibraryFunction 
 * Resolves the address of the library function name given, a pointer to the function will
 * be set in Address if OsSuccess is returned. */
CRTDECL(OsStatus_t,
ProcessGetLibraryFunction(
    _In_  Handle_t    Handle,
    _In_  const char* FunctionName,
    _Out_ uintptr_t*  Address));

/* ProcessUnloadLibrary 
 * Unloads the library handle, and renders all functions resolved invalid. */
CRTDECL(OsStatus_t,
ProcessUnloadLibrary(
    _In_ Handle_t Handle));
_CODE_END

#endif //!_PROCESS_INTERFACE_H_
