/**
 * MollenOS
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
#include <os/context.h>
#include <os/ipc.h>
#include <os/services/process.h>

OsStatus_t
ProcessTerminate(
	_In_ int ExitCode)
{
	thrd_t       ServiceTarget = GetProcessService();
	IpcMessage_t Request;
	OsStatus_t   Status;
	void*        Result;
	
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __PROCESSMANAGER_TERMINATE_PROCESS);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	IPC_SET_TYPED(&Request, 2, ExitCode);
	
	Status = IpcInvoke(ServiceTarget, &Request, 0, 0, &Result);
	if (Status != OsSuccess) {
	    return Status;
	}
	return IPC_CAST_AND_DEREF(Result, OsStatus_t);
}

OsStatus_t
GetProcessInheritationBlock(
    _In_    char*   Buffer,
    _InOut_ size_t* Length)
{
	thrd_t       ServiceTarget = GetProcessService();
	IpcMessage_t Request;
	OsStatus_t   Status;
	void*        Result;
	size_t       ActualLength;
	
	if (!Buffer || !Length) {
	    return OsInvalidParameters;
	}
	
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __PROCESSMANAGER_GET_INHERIT_BLOCK);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	
	Status = IpcInvoke(ServiceTarget, &Request, 0, 0, &Result);
	if (Status != OsSuccess) {
	    return Status;
	}
	
	ActualLength = IPC_CAST_AND_DEREF(Result, size_t);
	memcpy(Buffer, (const char*)(((size_t*)Result) + 1), MIN(ActualLength, *Length));
    *Length = ActualLength;
    return Status;
}

OsStatus_t
ProcessGetLibraryHandles(
    _Out_ Handle_t LibraryList[PROCESS_MAXMODULES])
{
	thrd_t       ServiceTarget = GetProcessService();
	IpcMessage_t Request;
	OsStatus_t   Status;
	void*        Result;
	
	if (!LibraryList) {
	    return OsInvalidParameters;
	}
	
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __PROCESSMANAGER_GET_LIBRARY_HANDLES);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	
	Status = IpcInvoke(ServiceTarget, &Request, 0, 0, &Result);
	if (Status != OsSuccess) {
	    return Status;
	}
	
	memcpy(&LibraryList[0], Result, sizeof(Handle_t) * PROCESS_MAXMODULES);
	return OsSuccess;
}

OsStatus_t
ProcessReportCrash(
    _In_ Context_t* CrashContext,
    _In_ int        CrashReason)
{
	thrd_t       ServiceTarget = GetProcessService();
	IpcMessage_t Request;
	OsStatus_t   Status;
	void*        Result;
	
	if (!CrashContext) {
	    return OsInvalidParameters;
	}
	
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __PROCESSMANAGER_CRASH_REPORT);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	IPC_SET_TYPED(&Request, 2, CrashReason);
	IpcSetUntypedArgument(&Request, 0, CrashContext, sizeof(Context_t));
	
	Status = IpcInvoke(ServiceTarget, &Request, 0, 0, &Result);
	if (Status != OsSuccess) {
	    return Status;
	}
	
	return IPC_CAST_AND_DEREF(Result, OsStatus_t);
}

OsStatus_t
ProcessLoadLibrary(
    _In_  const char* Path,
    _Out_ Handle_t*   Handle)
{
	thrd_t       ServiceTarget = GetProcessService();
	IpcMessage_t Request;
	OsStatus_t   Status;
	void*        Result;
	
	if (!Path || !Handle) {
	    return OsInvalidParameters;
	}
	
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __PROCESSMANAGER_LOAD_LIBRARY);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	IPC_SET_UNTYPED_STRING(&Request, 0, Path);
	
	Status = IpcInvoke(ServiceTarget, &Request, 0, 0, &Result);
	if (Status != OsSuccess) {
	    return Status;
	}
	
	*Handle = IPC_CAST_AND_DEREF(Result, Handle_t);
	return OsSuccess;
}

OsStatus_t
ProcessGetLibraryFunction(
    _In_  Handle_t    Handle,
    _In_  const char* FunctionName,
    _Out_ uintptr_t*  Address)
{
	thrd_t       ServiceTarget = GetProcessService();
	IpcMessage_t Request;
	OsStatus_t   Status;
	void*        Result;
	
	if (!FunctionName || !Address) {
	    return OsInvalidParameters;
	}
	
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __PROCESSMANAGER_RESOLVE_FUNCTION);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	IPC_SET_TYPED(&Request, 2, Handle);
	IPC_SET_UNTYPED_STRING(&Request, 0, FunctionName);
	
	Status = IpcInvoke(ServiceTarget, &Request, 0, 0, &Result);
	if (Status != OsSuccess) {
	    return Status;
	}
	
	*Address = IPC_CAST_AND_DEREF(Result, uintptr_t);
	return OsSuccess;
}

OsStatus_t
ProcessUnloadLibrary(
    _In_ Handle_t Handle)
{
	thrd_t       ServiceTarget = GetProcessService();
	IpcMessage_t Request;
	OsStatus_t   Status;
	void*        Result;
	
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __PROCESSMANAGER_UNLOAD_LIBRARY);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	IPC_SET_TYPED(&Request, 2, Handle);
	
	Status = IpcInvoke(ServiceTarget, &Request, 0, 0, &Result);
	if (Status != OsSuccess) {
	    return Status;
	}
	
	return IPC_CAST_AND_DEREF(Result, OsStatus_t);
}

