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
 * SessionManager Definitions & Structures
 * - This file describes the session-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <internal/_syscalls.h>
#include <internal/_utils.h>
#include <os/sessions.h>
#include <os/ipc/ipc.h>
#include <os/service.h>
#include <os/process.h>
#include <threads.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include "../../stdio/local.h"

/* SessionCheckDisk
 * Notifies the sessionmanager if a new accessible system disk. */
OsStatus_t
SessionCheckDisk(
	_In_ const char* DiskIdentifier)
{
	MRemoteCall_t Request;

	RPCInitialize(&Request, __SESSIONMANAGER_TARGET, 1, __SESSIONMANAGER_CHECKUP);
	RPCSetArgument(&Request, 0, (const void*)DiskIdentifier, strlen(DiskIdentifier) + 1);
	return RPCEvent(&Request);
}

/* SessionLoginRequest
 * Sends a login-request to the session-manager. The sessionmanager will respond
 * with a SessionObject structure containing information about success/failure. */
OsStatus_t
SessionLoginRequest(
	_In_ const char*      User,
    _In_ const char*      Password,
    _In_ SessionObject_t* Result)
{
	MRemoteCall_t Request;

	RPCInitialize(&Request, __SESSIONMANAGER_TARGET, 1, __SESSIONMANAGER_LOGIN);
	RPCSetArgument(&Request, 0, (const void*)User, strlen(User) + 1);
    RPCSetArgument(&Request, 1, (const void*)Password, strlen(Password) + 1);
    RPCSetResult(&Request, (const void*)Result, sizeof(SessionObject_t));
	return RPCExecute(&Request);
}

/* SessionLogoutRequest
 * Sends a logout-request to the session-manager. The acquired session-id from
 * the login must be used to logout the correct user. */
OsStatus_t
SessionLogoutRequest(
	_In_ const char* SessionId)
{
	MRemoteCall_t Request;
    OsStatus_t    Result = OsError;

	RPCInitialize(&Request, __SESSIONMANAGER_TARGET, 1, __SESSIONMANAGER_LOGOUT);
	RPCSetArgument(&Request, 0, (const void*)SessionId, strlen(SessionId) + 1);
    RPCSetResult(&Request, (const void*)&Result, sizeof(OsStatus_t));
	if (RPCExecute(&Request) != OsSuccess) {
        return OsError;
    }
    return Result;
}

void
InitializeStartupInformation(
    _In_ ProcessStartupInformation_t* StartupInformation)
{
    memset(StartupInformation, 0, sizeof(ProcessStartupInformation_t));

    // Reset handles
    StartupInformation->StdOutHandle = STDOUT_FILENO;
    StartupInformation->StdInHandle  = STDIN_FILENO;
    StartupInformation->StdErrHandle = STDERR_FILENO;
}

UUId_t 
ProcessSpawn(
    _In_     const char* Path,
    _In_Opt_ const char* Arguments)
{
    ProcessStartupInformation_t StartupInformation;

    // Sanitize parameters
    if (Path == NULL) {
        _set_errno(EINVAL);
        return UUID_INVALID;
    }

    // Setup information block
    InitializeStartupInformation(&StartupInformation);
    StartupInformation.InheritFlags = PROCESS_INHERIT_NONE;
    return ProcessSpawnEx(Path, Arguments, &StartupInformation);
}

UUId_t
ProcessSpawnEx(
    _In_     const char*                  Path,
    _In_Opt_ const char*                  Arguments,
    _In_     ProcessStartupInformation_t* StartupInformation)
{
    MRemoteCall_t Request;
    UUId_t        Handle            = 0;
    void*         InheritationBlock = NULL;
    size_t        InheritationBlockLength;
    OsStatus_t    Status = StdioCreateInheritanceBlock(StartupInformation, &InheritationBlock, &InheritationBlockLength);

    RPCInitialize(&Request, __SESSIONMANAGER_TARGET, 1, __SESSIONMANAGER_CREATE_PROCESS);
    RPCSetArgument(&Request, 0, (const void*)Path, strlen(Path) + 1);
    RPCSetArgument(&Request, 1, (const void*)StartupInformation, sizeof(ProcessStartupInformation_t));
    if (InheritationBlock != NULL) {
        RPCSetArgument(&Request, 2, (const void*)InheritationBlock, InheritationBlockLength);
    }
    if (Arguments != NULL) {
        RPCSetArgument(&Request, 3, (const void*)Arguments, strlen(Arguments) + 1);
    }
    RPCSetResult(&Request, (const void*)&Handle, sizeof(UUId_t));
    Status = RPCExecute(&Request);

    if (InheritationBlock != NULL) {
        free(InheritationBlock);
    }
    return Handle;
}

OsStatus_t 
ProcessJoin(
	_In_  UUId_t Handle,
    _In_  size_t Timeout,
    _Out_ int*   ExitCode)
{
    JoinProcessPackage_t Package;
    MRemoteCall_t        Request;
    OsStatus_t           Status;

    if (Handle == UUID_INVALID || ExitCode == NULL) {
        _set_errno(EINVAL);
        return OsError;
    }

    RPCInitialize(&Request, __SESSIONMANAGER_TARGET, 1, __SESSIONMANAGER_JOIN_PROCESS);
    RPCSetArgument(&Request, 0, (const void*)&Handle, sizeof(Handle));
    RPCSetArgument(&Request, 1, (const void*)&Timeout, sizeof(size_t));
    RPCSetResult(&Request, (const void*)&Package, sizeof(JoinProcessPackage_t));
    Status = RPCExecute(&Request);
    if (Status != OsSuccess || Package.Timeout) {
        if (Package.Timeout) {
            return OsTimeout;
        }
        return Status;
    }
    *ExitCode = Package.ExitCode;
    return OsSuccess;
}

OsStatus_t
ProcessKill(
	_In_ UUId_t Handle)
{
    MRemoteCall_t Request;
    OsStatus_t    Status = OsSuccess;
    OsStatus_t    Result = OsSuccess;

    if (Handle == UUID_INVALID ) {
        _set_errno(EINVAL);
        return OsError;
    }

    RPCInitialize(&Request, __SESSIONMANAGER_TARGET, 1, __SESSIONMANAGER_KILL_PROCESS);
    RPCSetArgument(&Request, 0, (const void*)&Handle, sizeof(Handle));
    RPCSetResult(&Request, (const void*)&Result, sizeof(OsStatus_t));
    Status = RPCExecute(&Request);
    if (Status != OsSuccess) {
        return Status;
    }
    return Result;
}

OsStatus_t
ProcessTerminate(
	_In_ int ExitCode)
{
    MRemoteCall_t Request;
    OsStatus_t    Status = OsSuccess;
    OsStatus_t    Result = OsSuccess;

    if (IsProcessModule()) {
        return Syscall_ModuleExit(ExitCode);
    }

    RPCInitialize(&Request, __SESSIONMANAGER_TARGET, 1, __SESSIONMANAGER_TERMINATE_PROCESS);
    RPCSetArgument(&Request, 0, (const void*)&ExitCode, sizeof(int));
    RPCSetResult(&Request, (const void*)&Result, sizeof(OsStatus_t));
    Status = RPCExecute(&Request);
    if (Status != OsSuccess) {
        return Status;
    }

    if (Result == OsSuccess) {
        thrd_exit(ExitCode);
    }
    return Result;
}

UUId_t
ProcessGetCurrentId(void)
{
    MRemoteCall_t Request;
    UUId_t        ProcessId = *GetInternalProcessId();
    if (ProcessId == UUID_INVALID) { 
        if (IsProcessModule()) {
            Syscall_ModuleId(&ProcessId);
        }
        else {
            RPCInitialize(&Request, __SESSIONMANAGER_TARGET, 1, __SESSIONMANAGER_GET_PROCESS_ID);
            RPCSetResult(&Request, (const void*)&ProcessId, sizeof(UUId_t));
            assert(RPCExecute(&Request) == OsSuccess);
        }
    }
    return ProcessId;
}

OsStatus_t
GetProcessCommandLine(
    _In_    const char* Buffer,
    _InOut_ size_t*     Length)
{
    MRemoteCall_t Request;
    OsStatus_t    Status = OsSuccess;

    if (IsProcessModule()) {
        return Syscall_ModuleGetStartupInfo(NULL, NULL, Buffer, Length);
    }

    RPCInitialize(&Request, __SESSIONMANAGER_TARGET, 1, __SESSIONMANAGER_GET_ARGUMENTS);
    RPCSetResult(&Request, (const void*)Buffer, *Length);
    Status = RPCExecute(&Request);
    *Length = Request.Result.Length;
    return Status;
}

OsStatus_t
GetProcessInheritationBlock(
    _In_    const char* Buffer,
    _InOut_ size_t*     Length)
{
    MRemoteCall_t Request;
    OsStatus_t    Status = OsSuccess;
    
    if (IsProcessModule()) {
        return Syscall_ModuleGetStartupInfo(Buffer, Length, NULL, NULL);
    }

    RPCInitialize(&Request, __SESSIONMANAGER_TARGET, 1, __SESSIONMANAGER_GET_INHERIT_BLOCK);
    RPCSetResult(&Request, (const void*)Buffer, *Length);
    Status = RPCExecute(&Request);
    *Length = Request.Result.Length;
    return Status;
}

OsStatus_t
ProcessGetCurrentName(
    _In_ const char* Buffer,
    _In_ size_t      MaxLength)
{
    MRemoteCall_t Request;
    
    if (IsProcessModule()) {
        return Syscall_ModuleName(Buffer, MaxLength);
    }

    RPCInitialize(&Request, __SESSIONMANAGER_TARGET, 1, __SESSIONMANAGER_GET_PROCESS_NAME);
    RPCSetResult(&Request, (const void*)Buffer, MaxLength);
    return RPCExecute(&Request);
}

OsStatus_t
ProcessGetAssemblyDirectory(
    _In_ UUId_t      Handle,
    _In_ const char* Buffer,
    _In_ size_t      MaxLength)
{
    MRemoteCall_t Request;
    
    RPCInitialize(&Request, __SESSIONMANAGER_TARGET, 1, __SESSIONMANAGER_GET_ASSEMBLY_DIRECTORY);
    RPCSetArgument(&Request, 0, (const void*)&Handle, sizeof(UUId_t));
    RPCSetResult(&Request, (const void*)Buffer, MaxLength);
    return RPCExecute(&Request);
}

OsStatus_t
ProcessGetWorkingDirectory(
    _In_ UUId_t      Handle,
    _In_ const char* Buffer,
    _In_ size_t      MaxLength)
{
    MRemoteCall_t Request;
    
    RPCInitialize(&Request, __SESSIONMANAGER_TARGET, 1, __SESSIONMANAGER_GET_WORKING_DIRECTORY);
    RPCSetArgument(&Request, 0, (const void*)&Handle, sizeof(UUId_t));
    RPCSetResult(&Request, (const void*)Buffer, MaxLength);
    return RPCExecute(&Request);
}

OsStatus_t
ProcessSetWorkingDirectory(
    _In_ const char* Path)
{
    MRemoteCall_t Request;
    OsStatus_t    Status = OsSuccess;
    OsStatus_t    Result = OsSuccess;
    
    RPCInitialize(&Request, __SESSIONMANAGER_TARGET, 1, __SESSIONMANAGER_SET_WORKING_DIRECTORY);
    RPCSetArgument(&Request, 0, (const void*)Path, strlen(Path) + 1);
    RPCSetResult(&Request, (const void*)&Result, sizeof(OsStatus_t));
    Status = RPCExecute(&Request);
    if (Status != OsSuccess) {
        return Status;
    }
    return Result;
}

OsStatus_t
ProcessGetLibraryHandles(
    _Out_ Handle_t LibraryList[PROCESS_MAXMODULES])
{
    MRemoteCall_t Request;
    
    if (IsProcessModule()) {
        return Syscall_ModuleGetModuleHandles(LibraryList);
    }

    RPCInitialize(&Request, __SESSIONMANAGER_TARGET, 1, __SESSIONMANAGER_GET_LIBRARY_HANDLES);
    RPCSetResult(&Request, (const void*)&LibraryList[0], sizeof(Handle_t) * PROCESS_MAXMODULES);
    return RPCExecute(&Request);
}

OsStatus_t
ProcessGetLibraryEntryPoints(
    _Out_ Handle_t LibraryList[PROCESS_MAXMODULES])
{
    MRemoteCall_t Request;
    
    if (IsProcessModule()) {
        return Syscall_ModuleGetModuleEntryPoints(LibraryList);
    }

    RPCInitialize(&Request, __SESSIONMANAGER_TARGET, 1, __SESSIONMANAGER_GET_LIBRARY_ENTRIES);
    RPCSetResult(&Request, (const void*)&LibraryList[0], sizeof(Handle_t) * PROCESS_MAXMODULES);
    return RPCExecute(&Request);
}

OsStatus_t
ProcessLoadLibrary(
    _In_  const char* Path,
    _Out_ Handle_t*   Handle)
{
    MRemoteCall_t Request;
    
    RPCInitialize(&Request, __SESSIONMANAGER_TARGET, 1, __SESSIONMANAGER_LOAD_LIBRARY);
    RPCSetArgument(&Request, 0, (const void*)Path, strlen(Path) + 1);
    RPCSetResult(&Request, (const void*)Handle, sizeof(Handle_t));
    return RPCExecute(&Request);
}

OsStatus_t
ProcessGetLibraryFunction(
    _In_  Handle_t    Handle,
    _In_  const char* FunctionName,
    _Out_ uintptr_t*  Address)
{
    MRemoteCall_t Request;
    
    RPCInitialize(&Request, __SESSIONMANAGER_TARGET, 1, __SESSIONMANAGER_RESOLVE_FUNCTION);
    RPCSetArgument(&Request, 0, (const void*)&Handle, sizeof(Handle_t));
    RPCSetArgument(&Request, 1, (const void*)FunctionName, strlen(FunctionName) + 1);
    RPCSetResult(&Request, (const void*)Address, sizeof(uintptr_t));
    return RPCExecute(&Request);
}

OsStatus_t
ProcessUnloadLibrary(
    _In_ Handle_t Handle)
{
    MRemoteCall_t Request;
    OsStatus_t    Status = OsSuccess;
    OsStatus_t    Result = OsSuccess;
    
    RPCInitialize(&Request, __SESSIONMANAGER_TARGET, 1, __SESSIONMANAGER_UNLOAD_LIBRARY);
    RPCSetArgument(&Request, 0, (const void*)&Handle, sizeof(Handle_t));
    RPCSetResult(&Request, (const void*)&Result, sizeof(OsStatus_t));
    Status = RPCExecute(&Request);
    if (Status != OsSuccess) {
        return Status;
    }
    return Result;
}
