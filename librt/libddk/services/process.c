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

#include <ddk/services/process.h>
#include <os/services/targets.h>
#include <os/context.h>

OsStatus_t
ProcessTerminate(
	_In_ int ExitCode)
{
    MRemoteCall_t Request;
    OsStatus_t    Status = OsSuccess;
    OsStatus_t    Result = OsSuccess;

    RPCInitialize(&Request, __PROCESSMANAGER_TARGET, 1, __PROCESSMANAGER_TERMINATE_PROCESS);
    RPCSetArgument(&Request, 0, (const void*)&ExitCode, sizeof(int));
    RPCSetResult(&Request, (const void*)&Result, sizeof(OsStatus_t));
    Status = RPCExecute(&Request);
    if (Status != OsSuccess) {
        return Status;
    }
    return Result;
}

OsStatus_t
GetProcessInheritationBlock(
    _In_    const char* Buffer,
    _InOut_ size_t*     Length)
{
    MRemoteCall_t Request;
    OsStatus_t    Status = OsSuccess;
    
    RPCInitialize(&Request, __PROCESSMANAGER_TARGET, 1, __PROCESSMANAGER_GET_INHERIT_BLOCK);
    RPCSetResult(&Request, (const void*)Buffer, *Length);
    Status = RPCExecute(&Request);
    *Length = Request.Result.Length;
    return Status;
}

OsStatus_t
ProcessGetLibraryHandles(
    _Out_ Handle_t LibraryList[PROCESS_MAXMODULES])
{
    MRemoteCall_t Request;
    
    RPCInitialize(&Request, __PROCESSMANAGER_TARGET, 1, __PROCESSMANAGER_GET_LIBRARY_HANDLES);
    RPCSetResult(&Request, (const void*)&LibraryList[0], sizeof(Handle_t) * PROCESS_MAXMODULES);
    return RPCExecute(&Request);
}

OsStatus_t
ProcessReportCrash(
    _In_ Context_t* CrashContext,
    _In_ int        CrashReason)
{
    MRemoteCall_t Request;
    OsStatus_t    Result;

    RPCInitialize(&Request, __PROCESSMANAGER_TARGET, 1, __PROCESSMANAGER_CRASH_REPORT);
    RPCSetArgument(&Request, 0, (const void*)CrashContext, sizeof(Context_t));
    RPCSetArgument(&Request, 1, (const void*)&CrashReason, sizeof(int));
    RPCSetResult(&Request, (const void*)&Result, sizeof(OsStatus_t));
    return RPCExecute(&Request);
}

OsStatus_t
ProcessLoadLibrary(
    _In_  const char* Path,
    _Out_ Handle_t*   Handle)
{
    MRemoteCall_t Request;
    
    RPCInitialize(&Request, __PROCESSMANAGER_TARGET, 1, __PROCESSMANAGER_LOAD_LIBRARY);
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
    
    RPCInitialize(&Request, __PROCESSMANAGER_TARGET, 1, __PROCESSMANAGER_RESOLVE_FUNCTION);
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
    
    RPCInitialize(&Request, __PROCESSMANAGER_TARGET, 1, __PROCESSMANAGER_UNLOAD_LIBRARY);
    RPCSetArgument(&Request, 0, (const void*)&Handle, sizeof(Handle_t));
    RPCSetResult(&Request, (const void*)&Result, sizeof(OsStatus_t));
    Status = RPCExecute(&Request);
    if (Status != OsSuccess) {
        return Status;
    }
    return Result;
}

