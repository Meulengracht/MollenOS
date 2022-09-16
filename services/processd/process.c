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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Process Manager
 * - Contains the implementation of the process-manager which keeps track
 *   of running applications.
 */

#define __TRACE

#include <assert.h>
#include <ds/mstring.h>
#include <ds/hashtable.h>
#include <ddk/handle.h>
#include <ddk/utils.h>
#include <internal/_syscalls.h> // for Syscall_ThreadCreate
#include <internal/_io.h>
#include <os/threads.h>
#include "pe.h"
#include "process.h"

#include "sys_library_service_server.h"
#include "sys_process_service_server.h"
#include "requests.h"

struct thread_mapping {
    uuid_t     process_id;
    uuid_t     thread_id;
};

struct process_entry {
    uuid_t     process_id;
    Process_t* process;
};

struct process_history_entry {
    uuid_t process_id;
    int    exit_code;
};

static hashtable_t       g_threadmappings;
static hashtable_t       g_processes;
static struct usched_mtx g_processesLock;
static hashtable_t       g_processHistory;
static struct usched_mtx g_processHistoryLock;

static uint64_t mapping_hash(const void* element);
static int      mapping_cmp(const void* element1, const void* element2);
static uint64_t process_hash(const void* element);
static int      process_cmp(const void* element1, const void* element2);
static uint64_t processhistory_hash(const void* element);
static int      processhistory_cmp(const void* element1, const void* element2);

static void
DestroyProcess(
        _In_ Process_t* process)
{
    TRACE("DestroyProcess(process=%u)", process->handle);

    // remove us from list of processes, remember we hold the process lock already
    // so nowhere in this code can we do the opposite acq/rel pattern
    usched_mtx_lock(&g_processesLock);
    hashtable_remove(&g_threadmappings, &(struct thread_mapping){ .thread_id = process->primary_thread_id });
    hashtable_remove(&g_processes, &(struct process_entry){ .process_id = process->handle });
    usched_mtx_unlock(&g_processesLock);

    mstr_delete(process->name);
    mstr_delete(process->path);
    mstr_delete(process->working_directory);
    mstr_delete(process->assembly_directory);
    if (process->image) {
        PeUnloadImage(process->image);
    }
    handle_destroy(process->handle);
    free(process);
}

void
UnregisterProcessRequest(
        _In_ Process_t* process,
        _In_ Request_t* request)
{
    if (!process) {
        return;
    }

    usched_mtx_lock(&process->lock);
    list_remove(&process->requests, &request->leaf);
    if (process->state == ProcessState_TERMINATING) {
        // check for outstanding requests
        if (!list_count(&process->requests)) {
            DestroyProcess(process);
            return; // no need to free that mutex
        }
    }
    usched_mtx_unlock(&process->lock);
}

Process_t*
RegisterProcessRequest(
        _In_ uuid_t     handle,
        _In_ Request_t* request)
{
    struct process_entry* entry;

    usched_mtx_lock(&g_processesLock);
    entry = hashtable_get(&g_processes, &(struct process_entry) { .process_id = handle });
    usched_mtx_unlock(&g_processesLock);

    if (entry) {
        usched_mtx_lock(&entry->process->lock);
        if (entry->process->state == ProcessState_RUNNING) {
            list_append(&entry->process->requests, &request->leaf);
            usched_mtx_unlock(&entry->process->lock);
            return entry->process;
        }
        usched_mtx_unlock(&entry->process->lock);
    }
    return NULL;
}

Process_t*
PmGetProcessByHandle(
        _In_ uuid_t handle)
{
    struct process_entry* entry;
    usched_mtx_lock(&g_processesLock);
    entry = hashtable_get(&g_processes, &(struct process_entry) { .process_id = handle });
    usched_mtx_unlock(&g_processesLock);
    return entry != NULL ? entry->process : NULL;
}

static inline Process_t*
GetProcessByThread(
        _In_ uuid_t handle)
{
    struct thread_mapping* mapping;
    struct process_entry*  entry = NULL;
    usched_mtx_lock(&g_processesLock);
    mapping = hashtable_get(&g_threadmappings, &(struct thread_mapping) { .thread_id = handle });
    if (mapping) {
        entry = hashtable_get(&g_processes, &(struct process_entry) { .process_id = mapping->process_id });
    }
    usched_mtx_unlock(&g_processesLock);
    return entry != NULL ? entry->process : NULL;
}

void
PmInitialize(void)
{
    hashtable_construct(&g_threadmappings, HASHTABLE_MINIMUM_CAPACITY,
                        sizeof(struct thread_mapping), mapping_hash,
                        mapping_cmp);
    hashtable_construct(&g_processes, HASHTABLE_MINIMUM_CAPACITY,
                        sizeof(struct process_entry), process_hash,
                        process_cmp);
    usched_mtx_init(&g_processesLock);
    hashtable_construct(&g_processHistory, HASHTABLE_MINIMUM_CAPACITY,
                        sizeof(struct process_history_entry), processhistory_hash,
                        processhistory_cmp);
    usched_mtx_init(&g_processHistoryLock);
}

static oserr_t
__BuildArguments(
        _In_ Process_t*  process,
        _In_ const char* argsIn)
{
    size_t pathLength;
    size_t argumentsLength = 0;
    char*  argumentsPointer;
    char*  processPath;

    // check for null or empty
    if (argsIn && strlen(argsIn)) {
        // include zero terminator
        argumentsLength = strlen(argsIn) + 1;
    }

    processPath = mstr_u8(process->path);
    if (processPath == NULL) {
        return OsOutOfMemory;
    }

    // handle arguments, we need to prepend the full path of the executable
    pathLength       = strlen(processPath);
    argumentsPointer = malloc(pathLength + 1 + argumentsLength);
    if (!argumentsPointer) {
        free(processPath);
        return OsOutOfMemory;
    }

    // Build the argument string, remember to null terminate.
    memcpy(&argumentsPointer[0], processPath, pathLength);
    if (argumentsLength != 0) {
        argumentsPointer[pathLength] = ' ';

        // argumentsLength includes zero termination, so no need to set explict
        memcpy(&argumentsPointer[pathLength + 1], (void *)argsIn, argumentsLength);
    }
    else {
        argumentsPointer[pathLength] = '\0';
    }

    process->arguments        = (const char *)argumentsPointer;
    process->arguments_length = pathLength + 1 + argumentsLength;
    free(processPath);
    return OsOK;
}

static oserr_t
__BuildInheritationBlock(
        _In_ Process_t*  process,
        _In_ const void* inheritationBuffer)
{
    size_t inheritationBlockLength;

    if (inheritationBuffer != NULL) {
        const stdio_inheritation_block_t* block = inheritationBuffer;

        inheritationBlockLength = sizeof(stdio_inheritation_block_t) +
                                  (block->handle_count * sizeof(struct stdio_handle));
        process->inheritation_block = malloc(inheritationBlockLength);
        if (!process->inheritation_block) {
            return OsOutOfMemory;
        }

        process->inheritation_block_length = inheritationBlockLength;
        memcpy(process->inheritation_block, block, inheritationBlockLength);
    }
    return OsOK;
}

static oserr_t
__LoadProcessImage(
        _In_  const char*      path,
        _Out_ PeExecutable_t** image)
{
    oserr_t    osStatus;
    mstring_t* pathUnified;

    ENTRY("__LoadProcessImage(path=%s)", path);

    pathUnified = mstr_new_u8(path);
    if (pathUnified == NULL) {
        return OsOutOfMemory;
    }

    osStatus = PeLoadImage(UUID_INVALID, NULL, pathUnified, image);

    EXIT("__LoadProcessImage");
    return osStatus;
}

static oserr_t
__ProcessNew(
        _In_  ProcessConfiguration_t* config,
        _In_  uuid_t                  handle,
        _In_  PeExecutable_t*         image,
        _Out_ Process_t**             processOut)
{
    Process_t* process;
    int        index;

    process = (Process_t *)malloc(sizeof(Process_t));
    if (!process) {
        return OsOutOfMemory;
    }
    memset(process, 0, sizeof(Process_t));

    process->handle = handle;
    process->primary_thread_id = UUID_INVALID;
    process->tick_base = clock();
    process->state = ProcessState_RUNNING;
    process->image = image;
    memcpy(&process->config, config, sizeof(ProcessConfiguration_t));
    usched_mtx_init(&process->lock);
    list_construct(&process->requests);

    process->path = mstr_clone(process->image->FullPath);
    if (!process->path) {
        free(process);
        return OsOutOfMemory;
    }

    index = mstr_rfind_u8(process->path, "/", -1);
    process->name               = mstr_substr(process->path, index + 1, -1);
    process->working_directory  = mstr_substr(process->path, 0, index);
    process->assembly_directory = mstr_substr(process->path, 0, index);
    if (!process->name || !process->working_directory || !process->assembly_directory) {
        mstr_delete(process->path);
        mstr_delete(process->name);
        mstr_delete(process->working_directory);
        mstr_delete(process->assembly_directory);
        free(process);
        return OsOutOfMemory;
    }

    *processOut = process;
    return OsOK;
}

static oserr_t
__StartProcess(
        _In_ Process_t* process)
{
    ThreadParameters_t threadParameters;
    oserr_t            osStatus;

    // Initialize threading paramaters for the new thread
    ThreadParametersInitialize(&threadParameters);
    threadParameters.MemorySpaceHandle = (uuid_t)(uintptr_t)process->image->MemorySpace;
    threadParameters.Name              = mstr_u8(process->name);
    if (threadParameters.Name == NULL) {
        return OsOutOfMemory;
    }

    osStatus = Syscall_ThreadCreate(
            process->image->EntryAddress,
            NULL, // Argument
            &threadParameters,
            &process->primary_thread_id);
    if (osStatus == OsOK) {
        osStatus = Syscall_ThreadDetach(process->primary_thread_id);
    }
    free((void*)threadParameters.Name);
    return osStatus;
}

oserr_t
PmCreateProcessInternal(
        _In_  const char*             path,
        _In_  const char*             args,
        _In_  const void*             inherit,
        _In_  ProcessConfiguration_t* processConfiguration,
        _Out_ uuid_t*                 handleOut)
{
    PeExecutable_t* image;
    Process_t*      process;
    uuid_t          handle;
    oserr_t         osStatus;
    ENTRY("PmCreateProcessInternal(path=%s, args=%s)", path, args);

    osStatus = __LoadProcessImage(path, &image);
    if (osStatus != OsOK) {
        goto exit;
    }

    osStatus = handle_create(&handle);
    if (osStatus != OsOK) {
        ERROR("PmCreateProcessInternal failed to allocate a system handle for process");
        PeUnloadImage(image);
        goto exit;
    }

    osStatus = __ProcessNew(processConfiguration, handle, image, &process);
    if (osStatus != OsOK) {
        PeUnloadImage(image);
        goto exit;
    }

    osStatus = __BuildArguments(process, args);
    if (osStatus != OsOK) {
        DestroyProcess(process);
        goto exit;
    }

    osStatus = __BuildInheritationBlock(process, inherit);
    if (osStatus != OsOK) {
        DestroyProcess(process);
        goto exit;
    }

    // acquire the processes lock before starting it as we need to add the process entry
    // to the hashtable, but we need the thread id before adding it. So to avoid any data races
    // in a multicore environment, hold the lock untill we've added it.
    usched_mtx_lock(&g_processesLock);
    osStatus = __StartProcess(process);
    if (osStatus != OsOK) {
        usched_mtx_unlock(&g_processesLock);
        DestroyProcess(process);
        goto exit;
    }

    // Add it to the list of processes
    TRACE("PmCreateProcessInternal registering process %u/%u", handle, process->primary_thread_id);
    hashtable_set(&g_threadmappings, &(struct thread_mapping) {
            .process_id = handle,
            .thread_id  = process->primary_thread_id
    });
    hashtable_set(&g_processes, &(struct process_entry) {
            .process_id = handle,
            .process    = process
    });
    usched_mtx_unlock(&g_processesLock);

    *handleOut = handle;
exit:
    EXIT("PmCreateProcessInternal");
    return osStatus;
}

void PmCreateProcess(
        _In_ Request_t* request,
        _In_ void*      cancellationToken)
{
    uuid_t     handle;
    oserr_t osStatus;
    ENTRY("PmCreateProcess(path=%s, args=%s)",
          request->parameters.spawn.path,
          request->parameters.spawn.args);

    osStatus = PmCreateProcessInternal(
            request->parameters.spawn.path,
            request->parameters.spawn.args,
            request->parameters.spawn.inherit,
            &request->parameters.spawn.conf,
            &handle);
    if (osStatus != OsOK) {
        sys_process_spawn_response(request->message, osStatus, UUID_INVALID);
        goto cleanup;
    }

    sys_process_spawn_response(request->message, osStatus, handle);

cleanup:
    free((void*)request->parameters.spawn.path);
    free((void*)request->parameters.spawn.args);
    free((void*)request->parameters.spawn.inherit);
    RequestDestroy(request);
    EXIT("PmCreateProcess");
}

void PmGetProcessStartupInformation(
        _In_ Request_t* request,
        _In_ void*      cancellationToken)
{
    Process_t* process       = GetProcessByThread(request->parameters.get_initblock.threadHandle);
    oserr_t    osStatus      = OsNotExists;
    uuid_t     processHandle = UUID_INVALID;
    int        moduleCount   = PROCESS_MAXMODULES;
    TRACE("PmGetProcessStartupInformation(thread=%u)", request->parameters.get_initblock.threadHandle);

    if (process == NULL) {
        goto exit;
    }
    processHandle = process->handle;

    struct dma_attachment dmaAttachment;
    osStatus = dma_attach(request->parameters.get_initblock.bufferHandle, &dmaAttachment);
    if (osStatus != OsOK) {
        goto exit;
    }

    osStatus = dma_attachment_map(&dmaAttachment, DMA_ACCESS_WRITE);
    if (osStatus != OsOK) {
        goto detach;
    }

    ProcessStartupInformation_t* startupInfo = dmaAttachment.buffer;
    char*                        buffer      = (char*)dmaAttachment.buffer + sizeof(ProcessStartupInformation_t);
    memcpy(&buffer[0], process->arguments, process->arguments_length);
    if (process->inheritation_block_length) {
        memcpy(
                &buffer[process->arguments_length],
                process->inheritation_block,
                process->inheritation_block_length);
    }

    osStatus = PeGetModuleEntryPoints(
            process->image,
            (Handle_t *)&buffer[process->arguments_length + process->inheritation_block_length],
            &moduleCount);

    // fill in the startup header as we now have all info
    startupInfo->ArgumentsLength = process->arguments_length;
    startupInfo->InheritationLength = process->inheritation_block_length;
    startupInfo->LibraryEntriesLength = moduleCount * sizeof(Handle_t);
    startupInfo->EnvironmentBlockLength = 0;

    // unmap and cleanup
    dma_attachment_unmap(&dmaAttachment);

detach:
    dma_detach(&dmaAttachment);

exit:
    sys_process_get_startup_information_response(request->message, osStatus, processHandle);
    RequestDestroy(request);
}

static void
__get_timestamp_from_now(unsigned int ms, struct timespec* ts)
{
    timespec_get(ts, TIME_UTC);
    ts->tv_nsec += (NSEC_PER_MSEC * ms);
    if (ts->tv_nsec >= NSEC_PER_SEC) {
        ts->tv_sec++;
        ts->tv_nsec -= NSEC_PER_SEC;
    }
}

void
PmJoinProcess(
        _In_ Request_t* request,
        _In_ void*      cancellationToken)
{
    Process_t* target;
    int        exitCode;

    TRACE("PmJoinProcess(process=%u, timeout=%u)",
            request->parameters.join.handle,
            request->parameters.join.timeout);

    target = RegisterProcessRequest(request->parameters.join.handle, request);
    if (!target) {
        struct process_history_entry* entry;

        // first check process history before responding negative
        usched_mtx_lock(&g_processHistoryLock);
        entry = hashtable_get(&g_processHistory, &(struct process_history_entry) {
                .process_id = request->parameters.join.handle
        });
        usched_mtx_unlock(&g_processHistoryLock);
        if (entry) {
            sys_process_join_response(request->message, OsOK, entry->exit_code);
            goto cleanup;
        }
        sys_process_join_response(request->message, OsNotExists, 0);
        goto cleanup;
    }

    // ok so the process is still running/terminating
    usched_mtx_lock(&target->lock);
    if (target->state == ProcessState_RUNNING) {
        struct timespec ts;
        int             status;
        __get_timestamp_from_now(request->parameters.join.timeout, &ts);
        status = usched_cnd_timedwait(&request->signal, &target->lock, &ts);
        if (status) {
            usched_mtx_unlock(&target->lock);
            sys_process_join_response(request->message, OsTimeout, 0);
            goto exit;
        }
    }
    exitCode = target->exit_code;
    usched_mtx_unlock(&target->lock);

    sys_process_join_response(request->message, OsOK, exitCode);

exit:
    UnregisterProcessRequest(target, request);

cleanup:
    RequestDestroy(request);
}

void PmTerminateProcess(
        _In_ Request_t* request,
        _In_ void*      cancellationToken)
{
    Process_t* process;
    element_t* i;

    TRACE("PmTerminateProcess process %u exitted with code: %i",
          request->parameters.terminate.handle,
          request->parameters.terminate.exit_code);

    process = PmGetProcessByHandle(request->parameters.terminate.handle);
    if (!process) {
        // what
        sys_process_terminate_response(request->message, OsNotExists);
        goto cleanup;
    }

    // Add a new entry in our history archive and then wake up all waiters
    usched_mtx_lock(&g_processHistoryLock);
    hashtable_set(&g_processHistory, &(struct process_history_entry) {
            .process_id = request->parameters.terminate.handle,
            .exit_code = request->parameters.terminate.exit_code
    });
    usched_mtx_unlock(&g_processHistoryLock);

    usched_mtx_lock(&process->lock);
    process->state     = ProcessState_TERMINATING;
    process->exit_code = request->parameters.terminate.exit_code;

    // go through all registered requests and wake them in case they were sleeping on
    // this process
    _foreach(i, &process->requests) {
        Request_t* subRequest = (Request_t*)i->value;
        usched_cnd_notify_one(&subRequest->signal);
    }
    usched_mtx_unlock(&process->lock);

    sys_process_terminate_response(request->message, OsOK);
cleanup:
    RequestDestroy(request);
}

void PmSignalProcess(
        _In_ Request_t* request,
        _In_ void*      cancellationToken)
{
    Process_t* victim;
    oserr_t osStatus;
    TRACE("PmSignalProcess(process=%u, signal=%i)",
          request->parameters.signal.victim_handle,
          request->parameters.signal.signal);

    // ok how to handle this, optimally we should check that the process
    // has license to kill before executing the signal
    victim = RegisterProcessRequest(request->parameters.signal.victim_handle, request);
    if (!victim) {
        osStatus = OsNotExists;
        goto exit;
    }

    // try to send a kill signal to primary thread
    usched_mtx_lock(&victim->lock);
    if (victim->state == ProcessState_RUNNING) {
        osStatus = Syscall_ThreadSignal(victim->primary_thread_id, request->parameters.signal.signal);
    }
    else {
        osStatus = OsInProgress;
    }
    usched_mtx_unlock(&victim->lock);
    UnregisterProcessRequest(victim, request);

exit:
    sys_process_signal_response(request->message, osStatus);
    RequestDestroy(request);
}

void PmLoadLibrary(
        _In_ Request_t* request,
        _In_ void*      cancellationToken)
{
    Process_t*      process;
    PeExecutable_t* executable;
    mstring_t*      path;
    oserr_t      osStatus = OsNotExists;
    Handle_t        handle   = HANDLE_INVALID;
    uintptr_t       entry    = 0;

    ENTRY("PmLoadLibrary(%u, %s)", request->parameters.load_library.handle,
          (request->parameters.load_library.path == NULL) ? "Global" : request->parameters.load_library.path);

    process = RegisterProcessRequest(request->parameters.load_library.handle, request);
    if (!process) {
        goto exit1;
    }

    if (!strlen(request->parameters.load_library.path)) {
        handle   = HANDLE_GLOBAL;
        entry    = process->image->EntryAddress;
        osStatus = OsOK;
        goto exit;
    }

    path     = mstr_new_u8((void *)request->parameters.load_library.path);
    osStatus = PeLoadImage(process->handle, process->image, path, &executable);
    if (osStatus == OsOK) {
        handle = executable;
        entry  = executable->EntryAddress;
    }
    mstr_delete(path);

exit:
    UnregisterProcessRequest(process, request);

exit1:
    if (request->parameters.load_library.path) {
        free((void*)request->parameters.load_library.path);
    }
    sys_library_load_response(request->message, osStatus, (uintptr_t)handle, entry);
    RequestDestroy(request);
    EXIT("LoadProcessLibrary");
}

void PmGetLibraryFunction(
        _In_ Request_t* request,
        _In_ void*      cancellationToken)
{
    Process_t* process;
    oserr_t osStatus = OsNotExists;
    uintptr_t  address  = 0;
    TRACE("PmGetLibraryFunction(process=%u, func=%s)",
          request->parameters.get_function.handle,
          request->parameters.get_function.name);

    process = PmGetProcessByHandle(request->parameters.stat_handle.handle);
    if (process) {
        PeExecutable_t* executable;

        usched_mtx_lock(&process->lock);
        if (request->parameters.get_function.library_handle != HANDLE_GLOBAL) {
            executable = (PeExecutable_t*)request->parameters.get_function.library_handle;
        }
        else {
            executable = process->image;
        }
        address = PeResolveFunction(executable, request->parameters.get_function.name);
        if (address != 0) {
            osStatus = OsOK;
        }
        usched_mtx_unlock(&process->lock);
    }

    sys_library_get_function_response(request->message, osStatus, address);
    free((void*)request->parameters.get_function.name);
    RequestDestroy(request);
}

void PmUnloadLibrary(
        _In_ Request_t* request,
        _In_ void*      cancellationToken)
{
    Process_t* process;
    oserr_t osStatus = OsNotExists;
    TRACE("PmUnloadLibrary(process=%u)",
          request->parameters.unload_library.handle);

    if (request->parameters.unload_library.library_handle == HANDLE_GLOBAL) {
        osStatus = OsOK;
        goto respond;
    }

    process = PmGetProcessByHandle(request->parameters.stat_handle.handle);
    if (process) {
        usched_mtx_lock(&process->lock);
        osStatus = PeUnloadLibrary(process->image,
                                   (PeExecutable_t *)request->parameters.unload_library.library_handle);
        usched_mtx_unlock(&process->lock);
    }

respond:
    sys_library_unload_response(request->message, osStatus);
    RequestDestroy(request);
}

void PmGetModules(
        _In_ Request_t* request,
        _In_ void*      cancellationToken)
{
    Process_t* process;
    int        moduleCount = PROCESS_MAXMODULES;
    Handle_t   buffer[PROCESS_MAXMODULES];

    process = PmGetProcessByHandle(request->parameters.stat_handle.handle);
    if (process) {
        usched_mtx_lock(&process->lock);
        PeGetModuleHandles(process->image, &buffer[0], &moduleCount);
        usched_mtx_unlock(&process->lock);
    }

    // respond
    sys_process_get_modules_response(request->message, (uintptr_t*)&buffer[0], moduleCount, moduleCount);

    // cleanup
    RequestDestroy(request);
}

void PmGetName(
        _In_ Request_t* request,
        _In_ void*      cancellationToken)
{
    Process_t* process;

    process = PmGetProcessByHandle(request->parameters.stat_handle.handle);
    if (process) {
        char* name;
        usched_mtx_lock(&process->lock);
        name = mstr_u8(process->name);
        usched_mtx_unlock(&process->lock);
        if (name == NULL) {
            sys_process_get_name_response(request->message, OsOutOfMemory, "");
        } else {
            sys_process_get_name_response(request->message, OsOK, name);
            free(name);
        }
    } else {
        sys_process_get_name_response(request->message, OsInvalidParameters, "");
    }
    RequestDestroy(request);
}

void PmGetTickBase(
        _In_ Request_t* request,
        _In_ void*      cancellationToken)
{
    Process_t*   process;
    oserr_t      status = OsInvalidParameters;
    UInteger64_t tick;

    process = PmGetProcessByHandle(request->parameters.stat_handle.handle);
    if (process) {
        usched_mtx_lock(&process->lock);
        tick.QuadPart = clock() - process->tick_base;
        usched_mtx_unlock(&process->lock);
        status = OsOK;
    }

    // respond
    sys_process_get_tick_base_response(request->message, status, tick.u.LowPart, tick.u.HighPart);

    // cleanup
    RequestDestroy(request);
}

void PmGetWorkingDirectory(
        _In_ Request_t* request,
        _In_ void*      cancellationToken)
{
    Process_t* process;

    process = PmGetProcessByHandle(request->parameters.stat_handle.handle);
    if (process) {
        char* path;
        usched_mtx_lock(&process->lock);
        path = mstr_u8(process->working_directory);
        usched_mtx_unlock(&process->lock);
        if (path == NULL) {
            sys_process_get_working_directory_response(request->message, OsOutOfMemory, "");
        } else {
            TRACE("PmGetWorkingDirectory path=%s", path);
            sys_process_get_working_directory_response(request->message, OsOK, path);
            free(path);
        }
    } else {
        sys_process_get_working_directory_response(request->message, OsInvalidParameters, "");
    }
    RequestDestroy(request);
}

void PmSetWorkingDirectory(
        _In_ Request_t* request,
        _In_ void*      cancellationToken)
{
    Process_t* process;
    oserr_t status = OsInvalidParameters;

    process = PmGetProcessByHandle(request->parameters.set_cwd.handle);
    if (process) {
        usched_mtx_lock(&process->lock);
        TRACE("PmSetWorkingDirectory path=%s", request->parameters.set_cwd.path);
        mstr_delete(process->working_directory);
        process->working_directory = mstr_new_u8((void*)request->parameters.set_cwd.path);
        usched_mtx_unlock(&process->lock);
        status = OsOK;
    }

    // respond
    sys_process_set_working_directory_response(request->message, status);

    // cleanup
    free((void*)request->parameters.set_cwd.path);
    RequestDestroy(request);
}

void PmGetAssemblyDirectory(
        _In_ Request_t* request,
        _In_ void*      cancellationToken)
{
    Process_t* process;

    process = PmGetProcessByHandle(request->parameters.stat_handle.handle);
    if (process) {
        char* path;
        usched_mtx_lock(&process->lock);
        path = mstr_u8(process->assembly_directory);
        usched_mtx_unlock(&process->lock);
        if (path == NULL) {
            sys_process_get_assembly_directory_response(request->message, OsOutOfMemory, "");
        } else {
            sys_process_get_assembly_directory_response(request->message, OsOK, path);
        }
    } else {
        sys_process_get_assembly_directory_response(request->message, OsInvalidParameters, "");
    }
    RequestDestroy(request);
}

static uint64_t mapping_hash(const void* element)
{
    const struct thread_mapping* entry = element;
    return entry->thread_id; // already unique identifier
}

static int mapping_cmp(const void* element1, const void* element2)
{
    const struct thread_mapping* lh = element1;
    const struct thread_mapping* rh = element2;

    // we accept both comparisons on thread id and process id
    // return 0 on true, 1 on false
    return (lh->thread_id == rh->thread_id) ? 0 : 1;
}

static uint64_t process_hash(const void* element)
{
    const struct process_entry* entry = element;
    return entry->process_id; // already unique identifier
}

static int process_cmp(const void* element1, const void* element2)
{
    const struct process_entry* lh = element1;
    const struct process_entry* rh = element2;

    // we accept both comparisons on thread id and process id
    // return 0 on true, 1 on false
    return (lh->process_id == rh->process_id) ? 0 : 1;
}

static uint64_t processhistory_hash(const void* element)
{
    const struct process_history_entry* entry = element;
    return entry->process_id; // already unique identifier
}

static int processhistory_cmp(const void* element1, const void* element2)
{
    const struct process_history_entry* lh = element1;
    const struct process_history_entry* rh = element2;

    // return 0 on true, 1 on false
    return lh->process_id == rh->process_id ? 0 : 1;
}
