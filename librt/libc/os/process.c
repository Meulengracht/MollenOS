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
 * Process Definitions & Structures
 * - This header describes the base process-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <assert.h>
#include <errno.h>
#include <internal/_syscalls.h>
#include <internal/_ipc.h>
#include <internal/_io.h>
#include <os/process.h>
#include <os/context.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <threads.h>

void
ProcessConfigurationInitialize(
    _In_ ProcessConfiguration_t* Configuration)
{
    memset(Configuration, 0, sizeof(ProcessConfiguration_t));

    // Reset handles
    Configuration->StdOutHandle = STDOUT_FILENO;
    Configuration->StdInHandle  = STDIN_FILENO;
    Configuration->StdErrHandle = STDERR_FILENO;
}

OsStatus_t
ProcessSpawn(
    _In_     const char* Path,
    _In_Opt_ const char* Arguments,
    _Out_    UUId_t*     HandleOut)
{
    ProcessConfiguration_t Configuration;

    // Sanitize parameters
    if (Path == NULL) {
        _set_errno(EINVAL);
        return UUID_INVALID;
    }

    // Setup information block
    ProcessConfigurationInitialize(&Configuration);
    Configuration.InheritFlags = PROCESS_INHERIT_NONE;
    return ProcessSpawnEx(Path, Arguments, &Configuration, HandleOut);
}

OsStatus_t
ProcessSpawnEx(
    _In_     const char*             Path,
    _In_Opt_ const char*             Arguments,
    _In_     ProcessConfiguration_t* Configuration,
    _Out_    UUId_t*                 HandleOut)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetProcessService());
    void*                    inheritationBlock       = NULL;
    size_t                   inheritationBlockLength = 0;
    OsStatus_t               status;
    
    if (!Path || !Configuration || !HandleOut) {
        return OsInvalidParameters;
    }
    
    StdioCreateInheritanceBlock(Configuration, &inheritationBlock, &inheritationBlockLength);
    
    svc_process_spawn(GetGrachtClient(), &msg.base, Path,
        Arguments, inheritationBlock, inheritationBlockLength, Configuration);
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GetGrachtBuffer());
    svc_process_spawn_result(GetGrachtClient(), &msg.base, &status, HandleOut);
    
    if (inheritationBlock) {
        free(inheritationBlock);
    }
    return status;
}

OsStatus_t 
ProcessJoin(
	_In_  UUId_t Handle,
    _In_  size_t Timeout,
    _Out_ int*   ExitCode)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetProcessService());
    OsStatus_t               status;
    
    svc_process_join(GetGrachtClient(), &msg.base, Handle, Timeout);
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GetGrachtBuffer());
    svc_process_join_result(GetGrachtClient(), &msg.base, &status, ExitCode);
    return status;
}

OsStatus_t
ProcessKill(
	_In_ UUId_t Handle)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetProcessService());
    OsStatus_t               status;
    
    svc_process_kill(GetGrachtClient(), &msg.base, ProcessGetCurrentId(), Handle);
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GetGrachtBuffer());
    svc_process_kill_result(GetGrachtClient(), &msg.base, &status);
    return status;
}

UUId_t
ProcessGetCurrentId(void)
{
    return *GetInternalProcessId();
}

OsStatus_t
ProcessGetTickBase(
    _Out_ clock_t* tickOut)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetProcessService());
    OsStatus_t               status;
    LargeUInteger_t          tick;
    
    svc_process_get_tick_base(GetGrachtClient(), &msg.base, ProcessGetCurrentId());
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GetGrachtBuffer());
    svc_process_get_tick_base_result(GetGrachtClient(), &msg.base, &status, &tick.u.LowPart, &tick.u.HighPart);
    
    *tickOut = (clock_t)tick.QuadPart;
    return status;
}

OsStatus_t
GetProcessCommandLine(
    _In_    char*   buffer,
    _InOut_ size_t* maxLength)
{
    const char* commandLine = GetInternalCommandLine();
	size_t      length      = strlen(&commandLine[0]);
	
	memcpy(buffer, commandLine, MIN(*maxLength, length));
	*maxLength = length;
	return OsSuccess;
}

OsStatus_t
ProcessGetCurrentName(
    _In_ char*  Buffer,
    _In_ size_t MaxLength)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetProcessService());
    OsStatus_t               status;
    
    if (IsProcessModule()) {
        return Syscall_ModuleName(Buffer, MaxLength);
    }
    
    svc_process_get_name(GetGrachtClient(), &msg.base, ProcessGetCurrentId(), MaxLength);
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GetGrachtBuffer());
    svc_process_get_name_result(GetGrachtClient(), &msg.base, &status, Buffer);
    return status;
}

OsStatus_t
ProcessGetAssemblyDirectory(
    _In_ UUId_t Handle,
    _In_ char*  Buffer,
    _In_ size_t MaxLength)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetProcessService());
    OsStatus_t               status;
    
    if (Handle == UUID_INVALID) {
        if (IsProcessModule()) {
            return Syscall_GetAssemblyDirectory(Buffer, MaxLength);
        }
        Handle = ProcessGetCurrentId();
    }
    
    svc_process_get_assembly_directory(GetGrachtClient(), &msg.base, Handle, MaxLength);
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GetGrachtBuffer());
    svc_process_get_assembly_directory_result(GetGrachtClient(), &msg.base, &status, Buffer);
    return status;
}

OsStatus_t
ProcessGetWorkingDirectory(
    _In_ UUId_t Handle,
    _In_ char*  Buffer,
    _In_ size_t MaxLength)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetProcessService());
    OsStatus_t               status;
    
    if (Handle == UUID_INVALID) {
        if (IsProcessModule()) {
            return Syscall_GetWorkingDirectory(Buffer, MaxLength);
        }
        Handle = ProcessGetCurrentId();
    }
	
    svc_process_get_working_directory(GetGrachtClient(), &msg.base, Handle, MaxLength);
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GetGrachtBuffer());
    svc_process_get_working_directory_result(GetGrachtClient(), &msg.base, &status, Buffer);
    return status;
}

OsStatus_t
ProcessSetWorkingDirectory(
    _In_ const char* Path)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetProcessService());
    OsStatus_t               status;
    
    if (IsProcessModule()) {
        return Syscall_SetWorkingDirectory(Path);
    }
	
    svc_process_set_working_directory(GetGrachtClient(), &msg.base, ProcessGetCurrentId(), Path);
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GetGrachtBuffer());
    svc_process_set_working_directory_result(GetGrachtClient(), &msg.base, &status);
    return status;
}
