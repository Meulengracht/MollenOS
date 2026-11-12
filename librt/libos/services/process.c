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
#include <ddk/convert/grants.h>
#include <ddk/service.h>
#include <ddk/utils.h>
#include <gracht/link/vali.h>
#include <internal/_io.h>
#include <internal/_gracht.h>
#include <internal/_shm_transfer.h>
#include <internal/_tls.h>
#include <internal/_utils.h>
#include <os/services/process.h>
#include <os/shm.h>
#include <os/grants.h>
#include <os/handle.h>
#include <os/memory.h>

#include <sys_process_service_client.h>

typedef struct OSProcessOptions {
    uuid_t                    Scope;
    const char* const*        Environment;
    const char*               Arguments;
    const char*               WorkingDirectory;
    struct InheritanceOptions InheritanceOptions;
    UInteger64_t              MemoryLimit;
    Capabilities_t            Capabilities;
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
    options->Capabilities = 0;

    // Calculated before passed on to protocol
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
OSProcessOptionsSetInheritance(
    _In_ OSProcessOptions_t* options,
    _In_ unsigned int       flags,
    _In_ int                stdinDescriptor,
    _In_ int                stdoutDescriptor,
    _In_ int                stderrDescriptor)
{
    options->InheritanceOptions.Flags = flags;
    options->InheritanceOptions.StdInHandle = stdinDescriptor;
    options->InheritanceOptions.StdOutHandle = stdoutDescriptor;
    options->InheritanceOptions.StdErrHandle = stderrDescriptor;
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

void
OSProcessOptionsSetCapabilities(
    _In_ OSProcessOptions_t* options,
    _In_ Capabilities_t      capabilities)
{
    options->Capabilities = capabilities;
}

static oserr_t
__WriteFlattenedEnvironment(
    _In_  const char* const* environment,
    _In_  char*              buffer,
    _In_  size_t             capacity,
    _Out_ uint32_t*          bytesWrittenOut)
{
    size_t offset = 0;
    size_t length;

    if (environment == NULL) {
        *bytesWrittenOut = 0;
        return OS_EOK;
    }

    if (capacity == 0) {
        return OS_EBUFFER;
    }

    for (size_t i = 0; environment[i] != NULL; i++) {
        // Reserve the final zero that terminates the list of strings.
        length = strnlen(environment[i], capacity - offset);
        if (length + 1 >= capacity - offset) {
            return OS_EBUFFER;
        }
        if (length == 0) {
            return OS_EINVALPARAMS;
        }
        
        memcpy(buffer + offset, environment[i], length + 1);
        offset += length + 1;
    }

    // Terminate the list of environment strings with an additional zero byte.
    buffer[offset++] = '\0';
    
    *bytesWrittenOut = (uint32_t)offset;
    return OS_EOK;
}

// Creates the spawn buffer and writes the inheritance block followed by
// the flattened environment into it. The transfer always covers
// PROCESS_STARTUP_MAX_SIZE bytes, and the unused tail stays zeroed.
// On failure, exported may still need CRTReleaseInheritanceBlock.
static oserr_t
__CreateSpawnBuffer(
    _In_  OSProcessOptions_t*        options,
    _Out_ struct inheritance_export* exported,
    _Out_ OSHandle_t*                bufferOut,
    _Out_ SHMTransfer_t*             transferOut)
{
    OSHandle_t    buffer;
    SHMTransfer_t transfer;
    char*         data;
    oserr_t       oserr;

    oserr = __SHMTransferAllocate(
        PROCESS_STARTUP_MAX_SIZE,
        GrantTokenScopeRead,
        &buffer,
        &transfer
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    // Get a pointer to the shared memory buffer.
    data = SHMBuffer(&buffer);

    oserr = CRTWriteInheritanceBlock(
        &options->InheritanceOptions,
        data,
        PROCESS_STARTUP_MAX_SIZE,
        exported
    );
    if (oserr != OS_EOK) {
        goto error;
    }

    oserr = __WriteFlattenedEnvironment(
        options->Environment,
        data + exported->length,
        PROCESS_STARTUP_MAX_SIZE - exported->length,
        &options->EnvironmentBlockLength
    );
    if (oserr != OS_EOK) {
        goto error;
    }

    *bufferOut = buffer;
    *transferOut = transfer;
    return OS_EOK;

error:
    OSGrantTokenRevoke(transfer.Token.ControlHandleID);
    OSHandleDestroy(&buffer);
    return oserr;
}

static void
__to_sys_process_configuration(
    _In_  OSProcessOptions_t*               in,
    _In_  uint32_t                          inheritBlockLength,
    _Out_ struct sys_process_configuration* out)
{
    out->scope = in->Scope;
    out->memory_limit = in->MemoryLimit.QuadPart;
    out->working_directory = (char*)in->WorkingDirectory;
    out->inherit_block_length = inheritBlockLength;
    out->environ_block_length = in->EnvironmentBlockLength;
    out->capabilities = (uint32_t)in->Capabilities;
}

oserr_t
OSProcessSpawn(
    _In_     const char*          path,
    _In_Opt_ OSProcessOptions_t*  options,
    _Out_    OSProcessIdentity_t* identityOut)
{
    struct vali_link_message         msg = VALI_MSG_INIT_HANDLE(GetProcessService());
    struct sys_process_configuration configuration = { 0 };
    OSProcessIdentity_t              identity = { .Process = UUID_INVALID };
    OSProcessOptions_t               defaultOptions;
    SHMTransfer_t                    transfer;
    struct inheritance_export        exported = { 0 };
    OSHandle_t                       buffer;
    oserr_t                          oserr;
    int                              status;

    if (path == NULL || identityOut == NULL) {
        return OS_EINVALPARAMS;
    }

    if (options == NULL) {
        __OSProcessOptionsConstruct(&defaultOptions);
        options = &defaultOptions;
    }

    if (strnlen(path, PROCESS_STARTUP_MAX_SIZE) == PROCESS_STARTUP_MAX_SIZE ||
        (options->Arguments != NULL &&
         strnlen(options->Arguments, PROCESS_STARTUP_MAX_SIZE) == PROCESS_STARTUP_MAX_SIZE)) {
        return OS_EBUFFER;
    }

    oserr = __CreateSpawnBuffer(options, &exported, &buffer, &transfer);
    if (oserr != OS_EOK) {
        CRTReleaseInheritanceBlock(&exported);
        return oserr;
    }

    __to_sys_process_configuration(options, exported.length, &configuration);
    oserr = __ToSysSHMTransfer(&transfer, &configuration.data_buffer);
    if (oserr != OS_EOK) {
        goto cleanup;
    }

    status = sys_process_spawn(
        GetGrachtClient(),
        &msg.base,
        path,
        options->Arguments,
        &configuration
    );
    if (status != 0) {
        oserr = OS_EUNKNOWN;
        goto cleanup;
    }

    oserr = __GrachtAwaitCompleted(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    if (oserr != OS_EOK) {
        goto cleanup;
    }

    status = sys_process_spawn_result(
        GetGrachtClient(),
        &msg.base,
        &oserr,
        &identity.Process,
        &identity.Principal
    );
    if (status != 0) {
        oserr = OS_EPROTOCOL;
    }
    
    if (oserr == OS_EOK) {
        *identityOut = identity;
    }

cleanup:
    // A failed wait does not stop remote work. Its attachment keeps the buffer valid.
    OSGrantTokenRevoke(transfer.Token.ControlHandleID);
    OSHandleDestroy(&buffer);
    CRTReleaseInheritanceBlock(&exported);
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
    oserr_t                  oserr;
    
    sys_process_signal(GetGrachtClient(), &msg.base, OSProcessCurrentID(), handle, signal);
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_process_signal_result(GetGrachtClient(), &msg.base, &oserr);
    return oserr;
}

oserr_t
OSProcessTerminate(
        _In_ int exitCode)
{
    struct vali_link_message msg   = VALI_MSG_INIT_HANDLE(GetProcessService());
    oserr_t                  oserr = OS_EOK;

    if (!__crt_is_phoenix()) {
        sys_process_terminate(GetGrachtClient(), &msg.base, __crt_process_id(), exitCode);
        gracht_client_await(GetGrachtClient(), &msg.base, 0);
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
