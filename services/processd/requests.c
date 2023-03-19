/**
 * Copyright 2021, Philip Meulengracht
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

#include <ddk/convert.h>
#include <ddk/utils.h>
#include <os/context.h>
#include <os/handle.h>
#include <os/shm.h>
#include <stdlib.h>
#include <string.h>
#include <process.h>

#include "sys_library_service_server.h"
#include "sys_process_service_server.h"

extern oserr_t PmGetProcessStartupInformation(uuid_t threadHandle, uuid_t bufferHandle, size_t bufferOffset, uuid_t* processHandleOut);
extern oserr_t PmJoinProcess(uuid_t processHandle, unsigned int timeout, int* exitCodeOut);
extern oserr_t PmTerminateProcess(uuid_t processHandle, int exitCode);
extern oserr_t PmSignalProcess(uuid_t processHandle, uuid_t victimHandle, int signal);
extern oserr_t PmLoadLibrary(uuid_t processHandle, const char* cpath, Handle_t* handleOut, uintptr_t* entryPointOut);
extern oserr_t PmGetLibraryFunction(uuid_t processHandle, Handle_t libraryHandle, const char* name, uintptr_t* functionAddressOut);
extern oserr_t PmUnloadLibrary(uuid_t processHandle, Handle_t libraryHandle);
extern oserr_t PmGetModules(uuid_t processHandle, Handle_t* modules, int* modulesCount);
extern oserr_t PmGetName(uuid_t processHandle, mstring_t** nameOut);
extern oserr_t PmGetTickBase(uuid_t processHandle, UInteger64_t* tick);
extern oserr_t PmGetWorkingDirectory(uuid_t processHandle, mstring_t** pathOut);
extern oserr_t PmSetWorkingDirectory(uuid_t processHandle, const char* path);
extern oserr_t PmGetAssemblyDirectory(uuid_t processHandle, mstring_t** pathOut);
extern oserr_t HandleProcessCrashReport(Process_t* process, uuid_t threadHandle, const Context_t* crashContext, int crashReason);

static void
__from_sys_process_configuration(
        _In_ const struct sys_process_configuration* in,
        _In_ struct ProcessOptions*                  out)
{
    out->Scope = in->scope;
    out->WorkingDirectory = in->working_directory;
    out->MemoryLimit.QuadPart = in->memory_limit;
    out->InheritationBlockLength = in->inherit_block_length;
    out->EnvironmentBlockLength = in->environ_block_length;
    out->DataBuffer = NULL;
}

void sys_process_spawn_invocation(
        struct gracht_message*                  message,
        const char*                             path,
        const char*                             arguments,
        const struct sys_process_configuration* configuration)
{
    struct ProcessOptions options;
    uuid_t                handle = UUID_INVALID;
    oserr_t               oserr;
    TRACE("void sys_process_spawn_invocation(()");

    if (path == NULL || !strlen(path)) {
        sys_process_spawn_response(message, OS_EINVALPARAMS, UUID_INVALID);
        return;
    }
    __from_sys_process_configuration(configuration, &options);

    oserr = SHMAttach(configuration->data_buffer, &options.DataBufferHandle);
    if (oserr != OS_EOK) {
        sys_process_spawn_response(message, oserr, UUID_INVALID);
        return;
    }

    // We only need read access to the buffer
    oserr = SHMMap(
            &options.DataBufferHandle,
            0,
            SHMBufferCapacity(&options.DataBufferHandle),
            SHM_ACCESS_READ
    );
    if (oserr != OS_EOK) {
        OSHandleDestroy(&options.DataBufferHandle);
        sys_process_spawn_response(message, oserr, UUID_INVALID);
        return;
    }

    oserr = PmCreateProcess(path, arguments, &options, &handle);
    OSHandleDestroy(&options.DataBufferHandle);
    sys_process_spawn_response(message, oserr, handle);
}

void sys_process_get_startup_information_invocation(struct gracht_message* message, const uuid_t handle,
                                                    const uuid_t bufferHandle, const size_t offset)
{
    TRACE("sys_process_get_startup_information_invocation()");
    uuid_t  processHandle = UUID_INVALID;
    oserr_t oserr = PmGetProcessStartupInformation(
            handle,
            bufferHandle,
            offset,
            &processHandle
    );
    sys_process_get_startup_information_response(message, oserr, processHandle);
}

void sys_process_join_invocation(struct gracht_message* message, const uuid_t handle, const unsigned int timeout)
{
    TRACE("sys_process_join_invocation()");
    int     exitCode = 0;
    oserr_t oserr = PmJoinProcess(handle, timeout, &exitCode);
    sys_process_join_response(message, oserr, exitCode);
}

void sys_process_terminate_invocation(struct gracht_message* message, const uuid_t handle, const int exitCode)
{
    TRACE("sys_process_terminate_invocation()");
    oserr_t oserr = PmTerminateProcess(handle, exitCode);
    sys_process_terminate_response(message, oserr);
}

void sys_process_signal_invocation(struct gracht_message* message, const uuid_t processId, const uuid_t handle, const int signal)
{
    TRACE("sys_process_signal_invocation()");
    oserr_t oserr = PmSignalProcess(processId, handle, signal);
    sys_process_signal_response(message, oserr);
}

void sys_library_load_invocation(struct gracht_message* message, const uuid_t processId, const char* path)
{
    TRACE("sys_library_load_invocation()");
    Handle_t  handle;
    uintptr_t address;
    oserr_t   oserr = PmLoadLibrary(processId, path, &handle, &address);
    sys_library_load_response(message, oserr, (uintptr_t)handle, address);
}

void sys_library_get_function_invocation(struct gracht_message* message, const uuid_t processId,
                                         const uintptr_t handle, const char* name)
{
    TRACE("sys_library_get_function_invocation()");
    uintptr_t functionAddress;
    oserr_t   oserr = PmGetLibraryFunction(processId, (Handle_t)handle, name, &functionAddress);
    sys_library_get_function_response(message, oserr, functionAddress);
}

void sys_library_unload_invocation(struct gracht_message* message, const uuid_t processId, const uintptr_t handle)
{
    TRACE("sys_library_unload_invocation()");
    oserr_t oserr = PmUnloadLibrary(processId, (Handle_t)handle);
    sys_library_unload_response(message, oserr);
}

void sys_process_get_modules_invocation(struct gracht_message* message, const uuid_t handle)
{
    TRACE("sys_process_get_modules_invocation()");
    int      moduleCount = PROCESS_MAXMODULES;
    Handle_t buffer[PROCESS_MAXMODULES];
    oserr_t  oserr = PmGetModules(handle, &buffer[0], &moduleCount);
    if (oserr != OS_EOK) {
        sys_process_get_modules_response(message, NULL, 0, 0);
        return;
    }
    sys_process_get_modules_response(message, (uintptr_t*)&buffer[0], moduleCount, moduleCount);
}

void sys_process_get_tick_base_invocation(struct gracht_message* message, const uuid_t handle)
{
    TRACE("sys_process_get_tick_base_invocation()");
    UInteger64_t tick;
    oserr_t      oserr = PmGetTickBase(handle, &tick);
    sys_process_get_tick_base_response(message, oserr, tick.u.LowPart, tick.u.HighPart);
}

void sys_process_get_assembly_directory_invocation(struct gracht_message* message, const uuid_t handle)
{
    TRACE("sys_process_get_assembly_directory_invocation()");
    mstring_t* directory;
    oserr_t    oserr = PmGetAssemblyDirectory(handle, &directory);
    char*      cdirectory;
    if (oserr != OS_EOK) {
        sys_process_get_assembly_directory_response(message, oserr, "");
        return;
    }
    cdirectory = mstr_u8(directory);
    if (cdirectory == NULL) {
        sys_process_get_assembly_directory_response(message, OS_EOOM, "");
        return;
    }
    sys_process_get_assembly_directory_response(message, OS_EOK, cdirectory);
    free(cdirectory);
}

void sys_process_get_working_directory_invocation(struct gracht_message* message, const uuid_t handle)
{
    TRACE("sys_process_get_working_directory_invocation()");
    mstring_t* directory;
    oserr_t    oserr = PmGetWorkingDirectory(handle, &directory);
    char*      cdirectory;
    if (oserr != OS_EOK) {
        sys_process_get_working_directory_response(message, oserr, "");
        return;
    }
    cdirectory = mstr_u8(directory);
    if (cdirectory == NULL) {
        sys_process_get_working_directory_response(message, OS_EOOM, "");
        return;
    }
    sys_process_get_working_directory_response(message, OS_EOK, cdirectory);
    free(cdirectory);
}

void sys_process_set_working_directory_invocation(struct gracht_message* message, const uuid_t handle, const char* path)
{
    TRACE("sys_process_set_working_directory_invocation()");
    oserr_t oserr = PmSetWorkingDirectory(handle, path);
    sys_process_set_working_directory_response(message, oserr);
}

void sys_process_get_name_invocation(struct gracht_message* message, const uuid_t handle)
{
    TRACE("sys_process_get_name_invocation()");
    mstring_t* name;
    oserr_t    oserr = PmGetName(handle, &name);
    char*      cname;
    if (oserr != OS_EOK) {
        sys_process_get_name_response(message, oserr, "");
        return;
    }
    cname = mstr_u8(name);
    if (cname == NULL) {
        sys_process_get_name_response(message, OS_EOOM, "");
        return;
    }
    sys_process_get_name_response(message, OS_EOK, cname);
    free(cname);
}

void sys_process_report_crash_invocation(struct gracht_message* message, const uuid_t threadId,
                                         const uuid_t processId, const uint8_t* crashContext,
                                         const uint32_t crashContext_count, const int reason)
{
    Process_t* process;
    oserr_t    oserr;

    TRACE("sys_process_report_crash_invocation(crashContext_count=%u)", crashContext_count);
    process = PmGetProcessByHandle(processId);
    if (!process) {
        // what the *?
        sys_process_report_crash_response(message, OS_ENOENT);
        return;
    }

    oserr = HandleProcessCrashReport(
            process,
            threadId,
            (const Context_t*)crashContext,
            reason
    );
    sys_process_report_crash_response(message, oserr);
}
