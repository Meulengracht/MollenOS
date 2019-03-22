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
 * Process Service (Protected) Definitions & Structures
 * - This header describes the base process-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __DDK_SERVICES_PROCESS_H__
#define __DDK_SERVICES_PROCESS_H__

#include <ddk/ddkdefs.h>
#include <os/types/process.h>

DECL_STRUCT(Context);

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

#define __PROCESSMANAGER_CRASH_REPORT        IPC_DECL_FUNCTION(17)

#define PROCESS_MAXMODULES          128

PACKED_TYPESTRUCT(JoinProcessPackage, {
    OsStatus_t Status;
    int        ExitCode;
});

_CODE_BEGIN
/* ProcessTerminate
 * Terminates the current process that is registered with the process manager.
 * This invalidates every functionality available to this process. */
DDKDECL(OsStatus_t,
ProcessTerminate(
	_In_ int ExitCode));

/* GetProcessInheritationBlock
 * Retrieves startup information about the process. 
 * Data buffers must be supplied with a max length. */
DDKDECL(OsStatus_t,
GetProcessInheritationBlock(
    _In_    const char* Buffer,
    _InOut_ size_t*     Length));

/* ProcessGetLibraryHandles
 * Retrieves a list of loaded library handles. Handles can be queried
 * for various application-image data. */
DDKDECL(OsStatus_t,
ProcessGetLibraryHandles(
    _Out_ Handle_t LibraryList[PROCESS_MAXMODULES]));

/* ProcessLoadLibrary 
 * Dynamically loads an application extensions for current process. A handle for the new
 * library is set in Handle if OsSuccess is returned. */
DDKDECL(OsStatus_t,
ProcessLoadLibrary(
    _In_  const char* Path,
    _Out_ Handle_t*   Handle));

/* ProcessGetLibraryFunction 
 * Resolves the address of the library function name given, a pointer to the function will
 * be set in Address if OsSuccess is returned. */
DDKDECL(OsStatus_t,
ProcessGetLibraryFunction(
    _In_  Handle_t    Handle,
    _In_  const char* FunctionName,
    _Out_ uintptr_t*  Address));

/* ProcessUnloadLibrary 
 * Unloads the library handle, and renders all functions resolved invalid. */
DDKDECL(OsStatus_t,
ProcessUnloadLibrary(
    _In_ Handle_t Handle));

/* ProcessReportCrash 
 * Reports a crash to the process manager, and gives opportunity for debugging. While
 * this call is not responded too, the application is kept alive. */
DDKDECL(OsStatus_t,
ProcessReportCrash(
    _In_ Context_t* CrashContext,
    _In_ int        CrashReason));
_CODE_END

#endif //!__DDK_SERVICES_PROCESS_H__
