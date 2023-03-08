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

//#define __TRACE
#define __need_minmax

#include <assert.h>
#include <errno.h>
#include <ddk/convert.h>
#include <ddk/service.h>
#include <ddk/utils.h>
#include <gracht/link/vali.h>
#include <internal/_io.h>
#include <internal/_tls.h>
#include <os/services/process.h>
#include <os/shm.h>

#include <sys_process_service_client.h>

typedef struct OSProcessOptions {
    uuid_t                    Scope;
    const char* const*        Environment;
    const char*               Arguments;
    const char*               WorkingDirectory;
    struct InheritanceOptions InheritanceOptions;
    UInteger64_t              MemoryLimit;
    uint32_t                  InheritationBlockLength;
    uint32_t                  EnvironmentBlockLength;
} OSProcessOptions_t;

void
__OSProcessOptionsConstruct(
        _In_ OSProcessOptions_t* options)
{
    options->Scope = UUID_INVALID;
    options->Environment = __crt_environment();
    options->Arguments = NULL;
    options->WorkingDirectory = NULL;
    options->InheritanceOptions.Flags = PROCESS_INHERIT_NONE;
    options->InheritanceOptions.StdOutHandle = STDOUT_FILENO;
    options->InheritanceOptions.StdInHandle  = STDIN_FILENO;
    options->InheritanceOptions.StdErrHandle = STDERR_FILENO;
    options->MemoryLimit.QuadPart = 0;

    // Calculated before passed on to protocol
    options->InheritationBlockLength = 0;
    options->EnvironmentBlockLength = 0;
}

OSProcessOptions_t*
OSProcessOptionsNew(void)
{
    OSProcessOptions_t* options;

    options = malloc(sizeof(OSProcessOptions_t));
    if (options == NULL) {
        return NULL;
    }
    __OSProcessOptionsConstruct(options);
    return options;
}

void
OSProcessOptionsDelete(
        _In_ OSProcessOptions_t* options)
{
    free(options);
}

void
OSProcessOptionsSetArguments(
        _In_ OSProcessOptions_t* options,
        _In_ const char*         arguments)
{
    options->Arguments = arguments;
}

void
OSProcessOptionsSetWorkingDirectory(
        _In_ OSProcessOptions_t* options,
        _In_ const char*         workingDirectory)
{
    options->WorkingDirectory = workingDirectory;
}

void
OSProcessOptionsSetEnvironment(
        _In_ OSProcessOptions_t* options,
        _In_ const char* const*  environment)
{
    options->Environment = environment;
}

void
OSProcessOptionsSetScope(
        _In_ OSProcessOptions_t* options,
        _In_ uuid_t              scope)
{
    options->Scope = scope;
}

void
OSProcessOptionsSetMemoryLimit(
        _In_ OSProcessOptions_t* options,
        _In_ size_t              memoryLimit)
{
    options->MemoryLimit.QuadPart = memoryLimit;
}

oserr_t
OSProcessSpawn(
        _In_     const char* path,
        _In_Opt_ const char* arguments,
        _Out_    uuid_t*     handleOut)
{
    OSProcessOptions_t options;

    if (path == NULL) {
        _set_errno(EINVAL);
        return UUID_INVALID;
    }

    __OSProcessOptionsConstruct(&options);
    OSProcessOptionsSetArguments(&options, arguments);
    return OSProcessSpawnOpts(
            path,
            &options,
            handleOut
    );
}

static void
__WriteFlattenedEnvironment(
        _In_  const char* const* environment,
        _In_  void*              buffer,
        _Out_ uint32_t*          bytesWrittenOut)
{
    char* flat = buffer;

    if (environment == NULL) {
        *bytesWrittenOut = 0;
        return;
    }

    for (int i = 0; environment[i]; i++) {
        size_t entryLength = strlen(environment[i]) + 1;
        memcpy(flat, environment[i], entryLength);
        flat += entryLength;
    }

    // write terminating null for the array
    flat[0] = '\0';
    flat++;
    *bytesWrittenOut = (uint32_t)(flat - (char*)buffer);
}

static void
__to_sys_process_configuration(
        _In_ OSProcessOptions_t* in,
        _In_ uuid_t              bufferIn,
        _In_ struct sys_process_configuration* out)
{
    out->scope = in->Scope;
    out->memory_limit = in->MemoryLimit.QuadPart;
    out->working_directory = (char*)in->WorkingDirectory;
    out->inherit_block_length = in->InheritationBlockLength;
    out->environ_block_length = in->EnvironmentBlockLength;
    out->data_buffer = bufferIn;
}

oserr_t
OSProcessSpawnOpts(
        _In_     const char*         path,
        _In_     OSProcessOptions_t* options,
        _Out_    uuid_t*             handleOut)
{
    struct vali_link_message         msg = VALI_MSG_INIT_HANDLE(GetProcessService());
    oserr_t                          oserr;
    struct sys_process_configuration gconfiguration;
    OSHandle_t*                      dmaBuffer;
    char*                            buffer;
    TRACE("OSProcessSpawnOpts(path=%s)", path);
    
    if (path == NULL || options == NULL || handleOut == NULL) {
        return OS_EINVALPARAMS;
    }

    // get the current TLS transfer buffer where we will store most
    // of the process setup data.
    dmaBuffer = __tls_current_dmabuf();
    buffer = SHMBuffer(dmaBuffer);

    CRTWriteInheritanceBlock(
            &options->InheritanceOptions,
            buffer,
            &options->InheritationBlockLength
    );
    buffer += options->InheritationBlockLength;
    __WriteFlattenedEnvironment(
            options->Environment,
            buffer,
            &options->EnvironmentBlockLength
    );
    buffer += options->EnvironmentBlockLength;

    __to_sys_process_configuration(options, dmaBuffer->ID, &gconfiguration);
    sys_process_spawn(GetGrachtClient(), &msg.base,
                      path,
                      options->Arguments,
                      &gconfiguration
    );
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_process_spawn_result(GetGrachtClient(), &msg.base, &oserr, handleOut);
    return oserr;
}

oserr_t
OSProcessJoin(
        _In_  uuid_t handle,
        _In_  size_t timeout,
        _Out_ int*   exitCodeOut)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetProcessService());
    oserr_t                  osStatus;
    
    sys_process_join(GetGrachtClient(), &msg.base, handle, timeout);
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_process_join_result(GetGrachtClient(), &msg.base, &osStatus, exitCodeOut);
    return osStatus;
}

oserr_t
OSProcessSignal(
        _In_ uuid_t handle,
        _In_ int    signal)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetProcessService());
    oserr_t               osStatus;
    
    sys_process_signal(GetGrachtClient(), &msg.base, OSProcessCurrentID(), handle, signal);
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_process_signal_result(GetGrachtClient(), &msg.base, &osStatus);
    return osStatus;
}

oserr_t OSProcessTerminate(int exitCode)
{
    struct vali_link_message msg   = VALI_MSG_INIT_HANDLE(GetProcessService());
    oserr_t                  oserr = OS_EOK;

    if (!__crt_is_phoenix()) {
        sys_process_terminate(GetGrachtClient(), &msg.base, __crt_process_id(), exitCode);
        gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
        sys_process_terminate_result(GetGrachtClient(), &msg.base, &oserr);
    }
    return oserr;
}

uuid_t
OSProcessCurrentID(void)
{
    return __crt_process_id();
}

oserr_t
OSProcessTickBase(
    _Out_ clock_t* tickOut)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetProcessService());
    oserr_t               status;
    UInteger64_t          tick;

    if (!tickOut) {
        return OS_EINVALPARAMS;
    }
    
    sys_process_get_tick_base(GetGrachtClient(), &msg.base, OSProcessCurrentID());
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_process_get_tick_base_result(GetGrachtClient(), &msg.base, &status, &tick.u.LowPart, &tick.u.HighPart);
    
    *tickOut = (clock_t)tick.QuadPart;
    return status;
}

oserr_t
OSProcessCommandLine(
    _In_    char*   buffer,
    _InOut_ size_t* length)
{
    const char* commandLine = __crt_cmdline();
	size_t      clLength    = strlen(&commandLine[0]);

    if (buffer == NULL) {
        if (length == NULL) {
            return OS_EINVALPARAMS;
        }
        *length = clLength;
        return OS_EOK;
    }

    memcpy(buffer, commandLine, MIN(*length, clLength));
	*length = clLength;
	return OS_EOK;
}

oserr_t
OSProcessCurrentName(
        _In_ char*  buffer,
        _In_ size_t maxLength)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetProcessService());
    oserr_t               status;

    if (buffer == NULL && maxLength == 0) {
        return OS_EINVALPARAMS;
    }

    assert(__crt_is_phoenix() == 0);

    sys_process_get_name(GetGrachtClient(), &msg.base, OSProcessCurrentID());
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_process_get_name_result(GetGrachtClient(), &msg.base,
                                &status,
                                buffer,
                                maxLength);
    return status;
}

oserr_t
OSProcessAssemblyDirectory(
        _In_ uuid_t handle,
        _In_ char*  buffer,
        _In_ size_t maxLength)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetProcessService());
    oserr_t               status;

    if (buffer == NULL || maxLength == 0) {
        return OS_EINVALPARAMS;
    }

    if (handle == UUID_INVALID) {
        assert(__crt_is_phoenix() == 0);
        handle = OSProcessCurrentID();
    }
    
    sys_process_get_assembly_directory(GetGrachtClient(), &msg.base, handle);
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_process_get_assembly_directory_result(GetGrachtClient(), &msg.base,
                                              &status,
                                              buffer,
                                              maxLength);
    return status;
}

oserr_t
OSProcessWorkingDirectory(
        _In_ uuid_t handle,
        _In_ char*  buffer,
        _In_ size_t maxLength)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetProcessService());
    oserr_t               status;

    if (buffer == NULL || maxLength == 0) {
        return OS_EINVALPARAMS;
    }
    
    if (handle == UUID_INVALID) {
        assert(__crt_is_phoenix() == 0);
        handle = OSProcessCurrentID();
    }
	
    sys_process_get_working_directory(GetGrachtClient(), &msg.base, handle);
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_process_get_working_directory_result(GetGrachtClient(), &msg.base,
                                             &status,
                                             buffer,
                                             maxLength);
    return status;
}

oserr_t
OSProcessSetWorkingDirectory(
    _In_ const char* path)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetProcessService());
    oserr_t               status;

    if (path == NULL) {
        return OS_EINVALPARAMS;
    }
    assert(__crt_is_phoenix() == 0);
	
    sys_process_set_working_directory(GetGrachtClient(), &msg.base, OSProcessCurrentID(), path);
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_process_set_working_directory_result(GetGrachtClient(), &msg.base, &status);
    return status;
}
