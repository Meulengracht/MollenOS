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
    
    status = svc_process_spawn_sync(GetGrachtClient(), &msg, Path,
        Arguments, inheritationBlock, inheritationBlockLength, Configuration,
        &status, HandleOut);
    gracht_vali_message_finish(&msg);
    
    if (inheritationBlock != NULL) {
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
    
    status = svc_process_join_sync(GetGrachtClient(), &msg, Handle,
        Timeout, &status, ExitCode);
    gracht_vali_message_finish(&msg);
    return status;
}

OsStatus_t
ProcessKill(
	_In_ UUId_t Handle)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetProcessService());
    OsStatus_t               status;
    
    status = svc_process_kill_sync(GetGrachtClient(), &msg, Handle, &status);
    gracht_vali_message_finish(&msg);
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
    
    status = svc_process_get_tick_base_sync(GetGrachtClient(), &msg, ProcessGetCurrentId(),
        &status, &tick.u.LowPart, &tick.u.HighPart);
    gracht_vali_message_finish(&msg);
    
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
    
    status = svc_process_get_name_sync(GetGrachtClient(), &msg, ProcessGetCurrentId(),
        &status, Buffer, MaxLength);
    gracht_vali_message_finish(&msg);
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
    
    status = svc_process_get_assembly_directory_sync(GetGrachtClient(), &msg, Handle,
        &status, Buffer, MaxLength);
    gracht_vali_message_finish(&msg);
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
	
    status = svc_process_get_working_directory_sync(GetGrachtClient(), &msg, Handle,
        &status, Buffer, MaxLength);
    gracht_vali_message_finish(&msg);
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
	
    status = svc_process_set_working_directory_sync(GetGrachtClient(), &msg,
        ProcessGetCurrentId(), Path, &status);
    gracht_vali_message_finish(&msg);
    return status;
}
