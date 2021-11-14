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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Virtual File Request Definitions & Structures
 * - This header describes the base virtual file-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#define __TRACE

#include <assert.h>
#include <ddk/utils.h>
#include <gracht/server.h>
#include <os/usched/usched.h>
#include <stdlib.h>
#include <string.h>
#include <ddk/convert.h>
#include "requests.h"

#include "sys_library_service_server.h"
#include "sys_process_service_server.h"

extern void PmCreateProcess(Request_t* request, void*);
extern void PmGetProcessStartupInformation(Request_t* request, void*);
extern void PmJoinProcess(Request_t* request, void*);
extern void PmTerminateProcess(Request_t* request, void*);
extern void PmKillProcess(Request_t* request, void*);
extern void PmLoadLibrary(Request_t* request, void*);
extern void PmGetLibraryFunction(Request_t* request, void*);
extern void PmUnloadLibrary(Request_t* request, void*);
extern void PmGetModules(Request_t* request, void*);
extern void PmGetName(Request_t* request, void*);
extern void PmGetTickBase(Request_t* request, void*);
extern void PmGetWorkingDirectory(Request_t* request, void*);
extern void PmSetWorkingDirectory(Request_t* request, void*);
extern void PmGetAssemblyDirectory(Request_t* request, void*);
extern void PmHandleCrash(Request_t* request, void*);

static _Atomic(UUId_t) g_requestId = ATOMIC_VAR_INIT(1);

static inline const void* memdup(const void* source, size_t count) {
    void* dest;
    if (!count) {
        return NULL;
    }
    dest = malloc(count);
    if (!dest) {
        return NULL;
    }
    memcpy(dest, source, count);
    return dest;
}

static Request_t*
CreateRequest(struct gracht_message* message)
{
    Request_t* request;

    request = malloc(sizeof(Request_t) + GRACHT_MESSAGE_DEFERRABLE_SIZE(message));
    if (!request) {
        ERROR("CreateRequest out of memory for message allocation!");
        return NULL;
    }

    request->id = atomic_fetch_add(&g_requestId, 1);
    request->state = RequestState_CREATED;
    gracht_server_defer_message(message, &request->message[0]);
    usched_cnd_init(&request->signal);
    ELEMENT_INIT(&request->leaf, 0, request);
    return request;
}

void RequestDestroy(Request_t* request)
{
    assert(request != NULL);

    free(request);
}

void RequestSetState(Request_t* request, enum RequestState state)
{
    assert(request != NULL);

    request->state = state;
}

void sys_process_spawn_invocation(struct gracht_message* message, const char* path, const char* arguments,
                                  const uint8_t* inheritBlock, const uint32_t inheritBlock_count,
                                  const struct sys_process_configuration* configuration)
{
    Request_t* request;
    TRACE("sys_process_spawn_invocation()");

    if (!strlen(path)) {
        sys_process_spawn_response(message, OsInvalidParameters, UUID_INVALID);
        return;
    }

    request = CreateRequest(message);
    if (!request) {
        sys_process_spawn_response(message, OsOutOfMemory, UUID_INVALID);
        return;
    }

    // initialize parameters
    request->parameters.spawn.path = strdup(path);
    request->parameters.spawn.args = strdup(arguments);
    request->parameters.spawn.inherit = memdup(inheritBlock, inheritBlock_count);
    from_sys_process_configuration(configuration, &request->parameters.spawn.conf);
    usched_task_queue((usched_task_fn)PmCreateProcess, request);
}

void sys_process_get_startup_information_invocation(struct gracht_message* message, const UUId_t handle,
                                                    const UUId_t bufferHandle, const size_t offset)
{
    Request_t* request;
    TRACE("sys_process_get_startup_information_invocation()");

    request = CreateRequest(message);
    if (!request) {
        sys_process_get_startup_information_response(message, OsOutOfMemory, UUID_INVALID,
                                                     0, 0, 0);
        return;
    }

    // initialize parameters
    request->parameters.get_initblock.threadHandle = handle;
    request->parameters.get_initblock.bufferHandle = bufferHandle;
    request->parameters.get_initblock.bufferOffset = offset;
    usched_task_queue((usched_task_fn)PmGetProcessStartupInformation, request);
}

void sys_process_join_invocation(struct gracht_message* message, const UUId_t handle, const unsigned int timeout)
{
    Request_t* request;
    TRACE("sys_process_join_invocation()");

    request = CreateRequest(message);
    if (!request) {
        sys_process_join_response(message, OsOutOfMemory, 0);
        return;
    }

    // initialize parameters
    request->parameters.join.handle  = handle;
    request->parameters.join.timeout = timeout;
    usched_task_queue((usched_task_fn)PmJoinProcess, request);
}

void sys_process_terminate_invocation(struct gracht_message* message, const UUId_t handle, const int exitCode)
{
    Request_t* request;
    TRACE("sys_process_terminate_invocation()");

    request = CreateRequest(message);
    if (!request) {
        sys_process_terminate_response(message, OsOutOfMemory);
        return;
    }

    // initialize parameters
    request->parameters.terminate.handle    = handle;
    request->parameters.terminate.exit_code = exitCode;
    usched_task_queue((usched_task_fn)PmTerminateProcess, request);
}

void sys_process_kill_invocation(struct gracht_message* message, const UUId_t processId, const UUId_t handle)
{
    Request_t* request;
    TRACE("sys_process_kill_invocation()");

    request = CreateRequest(message);
    if (!request) {
        sys_process_kill_response(message, OsOutOfMemory);
        return;
    }

    // initialize parameters
    request->parameters.kill.killer_handle = processId;
    request->parameters.kill.victim_handle = handle;
    usched_task_queue((usched_task_fn)PmKillProcess, request);
}

void sys_library_load_invocation(struct gracht_message* message, const UUId_t processId, const char* path)
{
    Request_t* request;
    TRACE("sys_library_load_invocation()");

    request = CreateRequest(message);
    if (!request) {
        sys_library_load_response(message, OsOutOfMemory, UUID_INVALID, 0);
        return;
    }

    // initialize parameters
    request->parameters.load_library.handle = processId;
    request->parameters.load_library.path   = strdup(path);
    usched_task_queue((usched_task_fn)PmLoadLibrary, request);
}

void sys_library_get_function_invocation(struct gracht_message* message, const UUId_t processId,
                                         const uintptr_t handle, const char* name)
{
    Request_t* request;
    TRACE("sys_library_get_function_invocation()");

    request = CreateRequest(message);
    if (!request) {
        sys_library_get_function_response(message, OsOutOfMemory, 0);
        return;
    }

    // initialize parameters
    request->parameters.get_function.handle         = processId;
    request->parameters.get_function.library_handle = handle;
    request->parameters.get_function.name           = strdup(name);
    usched_task_queue((usched_task_fn)PmGetLibraryFunction, request);
}

void sys_library_unload_invocation(struct gracht_message* message, const UUId_t processId, const uintptr_t handle)
{
    Request_t* request;
    TRACE("sys_library_unload_invocation()");

    request = CreateRequest(message);
    if (!request) {
        sys_library_unload_response(message, OsOutOfMemory);
        return;
    }

    // initialize parameters
    request->parameters.unload_library.handle         = processId;
    request->parameters.unload_library.library_handle = handle;
    usched_task_queue((usched_task_fn)PmUnloadLibrary, request);
}

void sys_process_get_modules_invocation(struct gracht_message* message, const UUId_t handle)
{
    Request_t* request;
    TRACE("sys_process_get_modules_invocation()");

    request = CreateRequest(message);
    if (!request) {
        sys_process_get_modules_response(message, NULL, 0, 0);
        return;
    }

    // initialize parameters
    request->parameters.stat_handle.handle = handle;
    usched_task_queue((usched_task_fn)PmGetModules, request);
}

void sys_process_get_tick_base_invocation(struct gracht_message* message, const UUId_t handle)
{
    Request_t* request;
    TRACE("sys_process_get_tick_base_invocation()");

    request = CreateRequest(message);
    if (!request) {
        sys_process_get_tick_base_response(message, OsOutOfMemory, 0, 0);
        return;
    }

    // initialize parameters
    request->parameters.stat_handle.handle = handle;
    usched_task_queue((usched_task_fn)PmGetTickBase, request);
}

void sys_process_get_assembly_directory_invocation(struct gracht_message* message, const UUId_t handle)
{
    Request_t* request;
    TRACE("sys_process_get_assembly_directory_invocation()");

    request = CreateRequest(message);
    if (!request) {
        sys_process_get_assembly_directory_response(message, OsOutOfMemory, "");
        return;
    }

    // initialize parameters
    request->parameters.stat_handle.handle = handle;
    usched_task_queue((usched_task_fn)PmGetAssemblyDirectory, request);
}

void sys_process_get_working_directory_invocation(struct gracht_message* message, const UUId_t handle)
{
    Request_t* request;
    TRACE("sys_process_get_working_directory_invocation()");

    request = CreateRequest(message);
    if (!request) {
        sys_process_get_working_directory_response(message, OsOutOfMemory, "");
        return;
    }

    // initialize parameters
    request->parameters.stat_handle.handle = handle;
    usched_task_queue((usched_task_fn)PmGetWorkingDirectory, request);
}

void sys_process_set_working_directory_invocation(struct gracht_message* message, const UUId_t handle, const char* path)
{
    Request_t* request;
    TRACE("sys_process_set_working_directory_invocation()");

    request = CreateRequest(message);
    if (!request) {
        sys_process_set_working_directory_response(message, OsOutOfMemory);
        return;
    }

    // initialize parameters
    request->parameters.set_cwd.handle = handle;
    request->parameters.set_cwd.path   = strdup(path);
    usched_task_queue((usched_task_fn)PmSetWorkingDirectory, request);
}

void sys_process_get_name_invocation(struct gracht_message* message, const UUId_t handle)
{
    Request_t* request;
    TRACE("sys_process_get_name_invocation()");

    request = CreateRequest(message);
    if (!request) {
        sys_process_get_name_response(message, OsOutOfMemory, "");
        return;
    }

    // initialize parameters
    request->parameters.stat_handle.handle = handle;
    usched_task_queue((usched_task_fn)PmGetName, request);
}

void sys_process_report_crash_invocation(struct gracht_message* message, const UUId_t threadId,
                                         const UUId_t processId, const uint8_t* crashContext,
                                         const uint32_t crashContext_count, const int reason)
{
    Request_t* request;
    TRACE("sys_process_report_crash_invocation(crashContext_count=%u)", crashContext_count);

    request = CreateRequest(message);
    if (!request) {
        sys_process_report_crash_response(message, OsOutOfMemory);
        return;
    }

    // initialize parameters
    request->parameters.crash.thread_handle = threadId;
    request->parameters.crash.process_handle = processId;
    request->parameters.crash.context = memdup(crashContext, crashContext_count);
    request->parameters.crash.reason = reason;
    usched_task_queue((usched_task_fn)PmHandleCrash, request);
}
