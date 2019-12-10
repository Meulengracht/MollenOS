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
 * Process Service Definitions & Structures
 * - This header describes the base process-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <assert.h>
#include <ddk/services/process.h>
#include <errno.h>
#include <internal/_syscalls.h>
#include <internal/_utils.h>
#include <internal/_io.h>
#include <os/services/process.h>
#include <os/context.h>
#include <os/ipc.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <threads.h>

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
	IpcMessage_t Request;
    UUId_t       Handle                  = UUID_INVALID;
    void*        InheritationBlock       = NULL;
    size_t       InheritationBlockLength = 0;
    OsStatus_t   Status;
	void*        Result;
	
    assert(Path != NULL);
    assert(StartupInformation != NULL);

    StdioCreateInheritanceBlock(StartupInformation, &InheritationBlock, &InheritationBlockLength);
    
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __PROCESSMANAGER_CREATE_PROCESS);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	IPC_SET_UNTYPED_STRING(&Request, 0, Path);
	IpcSetUntypedArgument(&Request, 1, StartupInformation, sizeof(ProcessStartupInformation_t));
	if (InheritationBlock != NULL) {
	    IpcSetUntypedArgument(&Request, 2, InheritationBlock, InheritationBlockLength);
	}
	if (Arguments != NULL) {
	    IPC_SET_UNTYPED_STRING(&Request, 3, Arguments);
	}
	
	Status = IpcInvoke(GetProcessService(), &Request, 0, 0, &Result);
	if (Status == OsSuccess) {
	    Handle = IPC_CAST_AND_DEREF(Result, UUId_t);
	}
	
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
    JoinProcessPackage_t* Package;
	IpcMessage_t          Request;
	OsStatus_t            Status;
	
    if (Handle == UUID_INVALID || ExitCode == NULL) {
        _set_errno(EINVAL);
        return OsError;
    }

	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __PROCESSMANAGER_JOIN_PROCESS);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	IPC_SET_TYPED(&Request, 2, Handle);
	IPC_SET_TYPED(&Request, 3, Timeout);
	
	Status = IpcInvoke(GetProcessService(), &Request, 0, 0, (void**)&Package);
	if (Status != OsSuccess) {
	    return Status;
	}
	
    *ExitCode = Package->ExitCode;
    return Package->Status;
}

OsStatus_t
ProcessKill(
	_In_ UUId_t Handle)
{
	IpcMessage_t Request;
	OsStatus_t   Status;
	void*        Result;
	
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __PROCESSMANAGER_KILL_PROCESS);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	IPC_SET_TYPED(&Request, 2, Handle);
	
	Status = IpcInvoke(GetProcessService(), &Request, 0, 0, &Result);
	if (Status != OsSuccess) {
	    return Status;
	}
	return IPC_CAST_AND_DEREF(Result, OsStatus_t);
}

UUId_t
ProcessGetCurrentId(void)
{
    IpcMessage_t Request;
    UUId_t       ProcessId = *GetInternalProcessId();
    OsStatus_t   Status;
	void*        Result;
    
    if (ProcessId == UUID_INVALID) {
        if (IsProcessModule()) {
            Syscall_ModuleId(&ProcessId);
        }
        else {
        	IpcInitialize(&Request);
        	IPC_SET_TYPED(&Request, 0, __PROCESSMANAGER_GET_PROCESS_ID);
        	Status = IpcInvoke(GetProcessService(), &Request, 0, 0, &Result);
        	if (Status != OsSuccess) {
        	    return UUID_INVALID;
        	}
        	ProcessId = IPC_CAST_AND_DEREF(Result, UUId_t);
        }
        *GetInternalProcessId() = ProcessId;
    }
    return ProcessId;
}

OsStatus_t
ProcessGetTickBase(
    _Out_ clock_t* Tick)
{
	IpcMessage_t Request;
	OsStatus_t   Status;
	void*        Result;
	
	if (!Tick) {
	    return OsInvalidParameters;
	}
	
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __PROCESSMANAGER_GET_PROCESS_TICK);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	
	Status = IpcInvoke(GetProcessService(), &Request, 0, 0, &Result);
	if (Status != OsSuccess) {
	    return Status;
	}
	
	*Tick = IPC_CAST_AND_DEREF(Result, clock_t);
	return OsSuccess;
}

OsStatus_t
GetProcessCommandLine(
    _In_    char*   Buffer,
    _InOut_ size_t* Length)
{
	IpcMessage_t Request;
	OsStatus_t   Status;
	char*        Result;
	
	if (!Buffer) {
	    return OsInvalidParameters;
	}
	
    if (IsProcessModule()) {
        return Syscall_ModuleGetStartupInfo(NULL, NULL, Buffer, Length);
    }
	
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __PROCESSMANAGER_GET_ARGUMENTS);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	
	Status = IpcInvoke(GetProcessService(), &Request, 0, 0, (void**)&Result);
	if (Status != OsSuccess) {
	    return Status;
	}
	
	memcpy(Buffer, Result, MIN(*Length, strlen(&Result[0])));
	*Length = strlen(&Result[0]);
	return OsSuccess;
}

OsStatus_t
ProcessGetCurrentName(
    _In_ char*  Buffer,
    _In_ size_t MaxLength)
{
	IpcMessage_t Request;
	OsStatus_t   Status;
	char*        Result;
	
	if (!Buffer) {
	    return OsInvalidParameters;
	}
	
    if (IsProcessModule()) {
        return Syscall_ModuleName(Buffer, MaxLength);
    }
	
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __PROCESSMANAGER_GET_PROCESS_NAME);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	
	Status = IpcInvoke(GetProcessService(), &Request, 0, 0, (void**)&Result);
	if (Status != OsSuccess) {
	    return Status;
	}
	
	memcpy(Buffer, Result, MIN(MaxLength, strlen(&Result[0])));
	return OsSuccess;
}

OsStatus_t
ProcessGetAssemblyDirectory(
    _In_ UUId_t Handle,
    _In_ char*  Buffer,
    _In_ size_t MaxLength)
{
	IpcMessage_t Request;
	OsStatus_t   Status;
	char*        Result;
	
	if (!Buffer) {
	    return OsInvalidParameters;
	}
	
    if (IsProcessModule()) {
        return OsNotSupported;
    }
	
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __PROCESSMANAGER_GET_ASSEMBLY_DIRECTORY);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	IPC_SET_TYPED(&Request, 2, Handle);
	
	Status = IpcInvoke(GetProcessService(), &Request, 0, 0, (void**)&Result);
	if (Status != OsSuccess) {
	    return Status;
	}
	
	memcpy(Buffer, Result, MIN(MaxLength, strlen(&Result[0])));
	return OsSuccess;
}

OsStatus_t
ProcessGetWorkingDirectory(
    _In_ UUId_t Handle,
    _In_ char*  Buffer,
    _In_ size_t MaxLength)
{
	IpcMessage_t Request;
	OsStatus_t   Status;
	char*        Result;
	
	if (!Buffer) {
	    return OsInvalidParameters;
	}
	
    if (IsProcessModule()) {
        return OsNotSupported;
    }
	
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __PROCESSMANAGER_GET_WORKING_DIRECTORY);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	IPC_SET_TYPED(&Request, 2, Handle);
	
	Status = IpcInvoke(GetProcessService(), &Request, 0, 0, (void**)&Result);
	if (Status != OsSuccess) {
	    return Status;
	}
	
	memcpy(Buffer, Result, MIN(MaxLength, strlen(&Result[0])));
	return OsSuccess;
}

OsStatus_t
ProcessSetWorkingDirectory(
    _In_ const char* Path)
{
	IpcMessage_t Request;
	OsStatus_t   Status;
	void*        Result;
	
	if (!Path) {
	    return OsInvalidParameters;
	}
	
    if (IsProcessModule()) {
        return OsNotSupported;
    }
	
	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __PROCESSMANAGER_SET_WORKING_DIRECTORY);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	IPC_SET_UNTYPED_STRING(&Request, 0, Path);
	
	Status = IpcInvoke(GetProcessService(), &Request, 0, 0, &Result);
	if (Status != OsSuccess) {
	    return Status;
	}
	
	return IPC_CAST_AND_DEREF(Result, OsStatus_t);
}

OsStatus_t
ProcessGetLibraryEntryPoints(
    _In_ Handle_t LibraryList[PROCESS_MAXMODULES])
{
	IpcMessage_t Request;
	OsStatus_t   Status;
	void*        Result;
	
	if (!LibraryList) {
	    return OsInvalidParameters;
	}
	
    if (IsProcessModule()) {
        return Syscall_ModuleGetModuleEntryPoints(LibraryList);
    }

	IpcInitialize(&Request);
	IPC_SET_TYPED(&Request, 0, __PROCESSMANAGER_GET_LIBRARY_ENTRIES);
	IPC_SET_TYPED(&Request, 1, ProcessGetCurrentId());
	
	Status = IpcInvoke(GetProcessService(), &Request, 0, 0, &Result);
	if (Status != OsSuccess) {
	    return Status;
	}
	
	memcpy(&LibraryList[0], Result, sizeof(Handle_t) * PROCESS_MAXMODULES);
	return OsSuccess;
}
