/**
 * Copyright 2022, Philip Meulengracht
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <assert.h>
#include <errno.h>
#include <internal/_ipc.h>
#include <internal/_io.h>
#include <os/process.h>
#include <ddk/convert.h>

void
ProcessConfigurationInitialize(
    _In_ ProcessConfiguration_t* configuration)
{
    memset(configuration, 0, sizeof(ProcessConfiguration_t));

    // Reset handles
    configuration->StdOutHandle = STDOUT_FILENO;
    configuration->StdInHandle  = STDIN_FILENO;
    configuration->StdErrHandle = STDERR_FILENO;
}

oserr_t
ProcessSpawn(
        _In_     const char* path,
        _In_Opt_ const char* arguments,
        _Out_    uuid_t*     handleOut)
{
    ProcessConfiguration_t configuration;

    if (path == NULL) {
        _set_errno(EINVAL);
        return UUID_INVALID;
    }

    ProcessConfigurationInitialize(&configuration);
    configuration.InheritFlags = PROCESS_INHERIT_NONE;
    return ProcessSpawnEx(
            path,
            arguments,
            __crt_environment(),
            &configuration,
            handleOut
    );
}

oserr_t
ProcessSpawnEx(
        _In_     const char*             path,
        _In_Opt_ const char*             arguments,
        _In_Opt_ const char* const*      environment,
        _In_     ProcessConfiguration_t* configuration,
        _Out_    uuid_t*                 handleOut)
{
    struct vali_link_message         msg = VALI_MSG_INIT_HANDLE(GetProcessService());
    void*                            inheritationBlock       = NULL;
    size_t                           inheritationBlockLength = 0;
    oserr_t                       status;
    struct sys_process_configuration gconfiguration;
    
    if (!path || !configuration || !handleOut) {
        return OsInvalidParameters;
    }
    
    StdioCreateInheritanceBlock(configuration, &inheritationBlock, &inheritationBlockLength);

    to_sys_process_configuration(configuration, &gconfiguration);
    sys_process_spawn(GetGrachtClient(), &msg.base, path,
                      arguments,
                      inheritationBlock,
                      inheritationBlockLength,
                      &gconfiguration);
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    sys_process_spawn_result(GetGrachtClient(), &msg.base, &status, handleOut);
    
    if (inheritationBlock) {
        free(inheritationBlock);
    }
    return status;
}

oserr_t
ProcessJoin(
        _In_  uuid_t handle,
        _In_  size_t timeout,
        _Out_ int*   exitCodeOut)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetProcessService());
    oserr_t               osStatus;
    
    sys_process_join(GetGrachtClient(), &msg.base, handle, timeout);
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    sys_process_join_result(GetGrachtClient(), &msg.base, &osStatus, exitCodeOut);
    return osStatus;
}

oserr_t
ProcessSignal(
        _In_ uuid_t handle,
        _In_ int    signal)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetProcessService());
    oserr_t               osStatus;
    
    sys_process_signal(GetGrachtClient(), &msg.base, ProcessGetCurrentId(), handle, signal);
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    sys_process_signal_result(GetGrachtClient(), &msg.base, &osStatus);
    return osStatus;
}

oserr_t ProcessTerminate(int exitCode)
{
    struct vali_link_message msg   = VALI_MSG_INIT_HANDLE(GetProcessService());
    oserr_t                  oserr = OsOK;

    if (!__crt_is_phoenix()) {
        sys_process_terminate(GetGrachtClient(), &msg.base, *__crt_processid_ptr(), exitCode);
        gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
        sys_process_terminate_result(GetGrachtClient(), &msg.base, &oserr);
    }
    return oserr;
}

uuid_t
ProcessGetCurrentId(void)
{
    return *__crt_processid_ptr();
}

oserr_t
ProcessGetTickBase(
    _Out_ clock_t* tickOut)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetProcessService());
    oserr_t               status;
    UInteger64_t          tick;

    if (!tickOut) {
        return OsInvalidParameters;
    }
    
    sys_process_get_tick_base(GetGrachtClient(), &msg.base, ProcessGetCurrentId());
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    sys_process_get_tick_base_result(GetGrachtClient(), &msg.base, &status, &tick.u.LowPart, &tick.u.HighPart);
    
    *tickOut = (clock_t)tick.QuadPart;
    return status;
}

oserr_t
GetProcessCommandLine(
    _In_    char*   buffer,
    _InOut_ size_t* length)
{
    const char* commandLine = __crt_cmdline();
	size_t      clLength    = strlen(&commandLine[0]);

    if (buffer == NULL) {
        if (length == NULL) {
            return OsInvalidParameters;
        }
        *length = clLength;
        return OsOK;
    }

    memcpy(buffer, commandLine, MIN(*length, clLength));
	*length = clLength;
	return OsOK;
}

oserr_t
ProcessGetCurrentName(
        _In_ char*  buffer,
        _In_ size_t maxLength)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetProcessService());
    oserr_t               status;

    if (buffer == NULL && maxLength == 0) {
        return OsInvalidParameters;
    }

    assert(__crt_is_phoenix() == 0);

    sys_process_get_name(GetGrachtClient(), &msg.base, ProcessGetCurrentId());
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    sys_process_get_name_result(GetGrachtClient(), &msg.base,
                                &status,
                                buffer,
                                maxLength);
    return status;
}

oserr_t
ProcessGetAssemblyDirectory(
        _In_ uuid_t handle,
        _In_ char*  buffer,
        _In_ size_t maxLength)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetProcessService());
    oserr_t               status;

    if (buffer == NULL || maxLength == 0) {
        return OsInvalidParameters;
    }

    if (handle == UUID_INVALID) {
        assert(__crt_is_phoenix() == 0);
        handle = ProcessGetCurrentId();
    }
    
    sys_process_get_assembly_directory(GetGrachtClient(), &msg.base, handle);
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    sys_process_get_assembly_directory_result(GetGrachtClient(), &msg.base,
                                              &status,
                                              buffer,
                                              maxLength);
    return status;
}

oserr_t
ProcessGetWorkingDirectory(
        _In_ uuid_t handle,
        _In_ char*  buffer,
        _In_ size_t maxLength)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetProcessService());
    oserr_t               status;

    if (buffer == NULL || maxLength == 0) {
        return OsInvalidParameters;
    }
    
    if (handle == UUID_INVALID) {
        assert(__crt_is_phoenix() == 0);
        handle = ProcessGetCurrentId();
    }
	
    sys_process_get_working_directory(GetGrachtClient(), &msg.base, handle);
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    sys_process_get_working_directory_result(GetGrachtClient(), &msg.base,
                                             &status,
                                             buffer,
                                             maxLength);
    return status;
}

oserr_t
ProcessSetWorkingDirectory(
    _In_ const char* path)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetProcessService());
    oserr_t               status;

    if (path == NULL) {
        return OsInvalidParameters;
    }
    assert(__crt_is_phoenix() == 0);
	
    sys_process_set_working_directory(GetGrachtClient(), &msg.base, ProcessGetCurrentId(), path);
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    sys_process_set_working_directory_result(GetGrachtClient(), &msg.base, &status);
    return status;
}
