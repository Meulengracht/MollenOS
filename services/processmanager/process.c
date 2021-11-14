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
#include <internal/_ipc.h>
#include "../../librt/libds/pe/pe.h"
#include <os/mollenos.h>
#include "process.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>

#include "sys_library_service_server.h"
#include "sys_process_service_server.h"
#include "requests.h"

struct thread_mapping {
    UUId_t     process_id;
    UUId_t     thread_id;
};

struct process_entry {
    UUId_t     process_id;
    Process_t* process;
};

struct process_history_entry {
    UUId_t process_id;
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

    MStringDestroy(process->name);
    MStringDestroy(process->path);
    MStringDestroy(process->working_directory);
    MStringDestroy(process->assembly_directory);
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
        _In_ UUId_t     handle,
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

static inline Process_t*
GetProcessByHandle(
        _In_ UUId_t handle)
{
    struct process_entry* entry;
    usched_mtx_lock(&g_processesLock);
    entry = hashtable_get(&g_processes, &(struct process_entry) { .process_id = handle });
    usched_mtx_unlock(&g_processesLock);
    return entry != NULL ? entry->process : NULL;
}

static inline Process_t*
GetProcessByThread(
        _In_ UUId_t handle)
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

static inline OsStatus_t
TestFilePath(
        _In_ MString_t* path)
{
    OsFileDescriptor_t fileDescriptor;
    return GetFileInformationFromPath(MStringRaw(path), &fileDescriptor);
}

static OsStatus_t
GuessBasePath(
        _In_  UUId_t      processHandle,
        _In_  MString_t*  path,
        _Out_ MString_t** fullPathOut)
{
    // Check the working directory, if it fails iterate the environment defaults
    Process_t* process = GetProcessByHandle(processHandle);
    MString_t* result;
    int        isApp;
    int        isDll;

    // Start by testing against the loaders current working directory,
    // however this won't work for the base process
    if (process != NULL) {
        result = MStringClone(process->working_directory);
        MStringAppendCharacter(result, '/');
        MStringAppend(result, path);
        if (TestFilePath(result) == OsSuccess) {
            *fullPathOut = result;
            return OsSuccess;
        }
    }
    else {
        result = MStringCreate(NULL, StrUTF8);
    }

    // At this point we have to run through all PATH values
    // Look at the type of file we are trying to load. .app? .dll? 
    // for other types its most likely resource load
    isApp = MStringFindCString(path, ".run");
    isDll = MStringFindCString(path, ".dll");
    if (isApp != MSTRING_NOT_FOUND || isDll != MSTRING_NOT_FOUND) {
        MStringReset(result, "$bin/", StrUTF8);
    }
    else {
        MStringReset(result, "$data/", StrUTF8);
    }
    MStringAppend(result, path);
    if (TestFilePath(result) == OsSuccess) {
        *fullPathOut = result;
        return OsSuccess;
    }
    else {
        MStringDestroy(result);
        return OsError;
    }
}

OsStatus_t
ResolveFilePath(
        _In_  UUId_t      processId,
        _In_  MString_t*  path,
        _Out_ MString_t** fullPathOut)
{
    OsStatus_t osStatus        = OsSuccess;
    MString_t* temporaryResult = path;
    ENTRY("ResolveFilePath(processId=%u, path=%s)", processId, MStringRaw(path));

    if (MStringFind(path, ':', 0) == MSTRING_NOT_FOUND) {
        // If we don't even have an environmental identifier present, we
        // have to get creative and guess away
        if (MStringFind(path, '$', 0) == MSTRING_NOT_FOUND) {
            osStatus = GuessBasePath(processId, path, &temporaryResult);

            TRACE("ResolveFilePath basePath=%s", MStringRaw(temporaryResult));

            // If we already deduced an absolute path skip the canonicalizing moment
            if (osStatus == OsSuccess && MStringFind(temporaryResult, ':', 0) != MSTRING_NOT_FOUND) {
                *fullPathOut = temporaryResult;
                EXIT("ResolveFilePath");
                return osStatus;
            }
        }

        // Take into account we might have failed to guess base path
        if (osStatus == OsSuccess) {
            char* canonicalizedPath = (char *)malloc(_MAXPATH);
            if (!canonicalizedPath) {
                ERROR("ResolveFilePath failed to allocate memory buffer for the canonicalized path");
                EXIT("ResolveFilePath");
                return OsOutOfMemory;
            }
            memset(canonicalizedPath, 0, _MAXPATH);

            osStatus = PathCanonicalize(MStringRaw(temporaryResult), canonicalizedPath, _MAXPATH);
            TRACE("ResolveFilePath canonicalizedPath=%s", canonicalizedPath);
            if (osStatus == OsSuccess) {
                *fullPathOut = MStringCreate(canonicalizedPath, StrUTF8);
            }
            free(canonicalizedPath);
        }
    }
    else {
        // Assume absolute path
        *fullPathOut = MStringClone(temporaryResult);
    }

    EXIT("ResolveFilePath");
    return osStatus;
}

OsStatus_t
LoadFile(
        _In_  MString_t* fullPath,
        _Out_ void**     bufferOut,
        _Out_ size_t*    lengthOut)
{
    FILE*      file;
    long       fileSize;
    void*      fileBuffer;
    size_t     bytesRead;
    OsStatus_t osStatus = OsSuccess;
    ENTRY("LoadFile %s", MStringRaw(fullPath));

    file = fopen(MStringRaw(fullPath), "rb");
    if (!file) {
        ERROR("LoadFile fopen failed: %i", errno);
        osStatus = OsDoesNotExist;
        goto exit;
    }

    fseek(file, 0, SEEK_END);
    fileSize = ftell(file);
    rewind(file);

    TRACE("[load_file] size %" PRIuIN, fileSize);
    fileBuffer = malloc(fileSize);
    if (!fileBuffer) {
        ERROR("LoadFile null");
        fclose(file);
        osStatus = OsOutOfMemory;
        goto exit;
    }

    bytesRead = fread(fileBuffer, 1, fileSize, file);
    fclose(file);

    TRACE("LoadFile read %" PRIuIN " bytes from file", bytesRead);

    *bufferOut = fileBuffer;
    *lengthOut = fileSize;

exit:
    EXIT("LoadFile");
    return osStatus;
}

void
UnloadFile(
        _In_ MString_t* FullPath,
        _In_ void*      Buffer)
{
    // So right now we will simply free the buffer, 
    // but when we implement caching we will check if it should stay cached
    _CRT_UNUSED(FullPath);
    free(Buffer);
}

void
InitializeProcessManager(void)
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

static OsStatus_t
BuildArguments(
        _In_ Process_t* process,
        _In_ Request_t* request)
{
    const char* argsIn = request->parameters.spawn.args;
    size_t      pathLength;
    size_t      argumentsLength = 0;
    char*       argumentsPointer;

    // check for null or empty
    if (argsIn && strlen(argsIn)) {
        // include zero terminator
        argumentsLength = strlen(argsIn) + 1;
    }

    // handle arguments, we need to prepend the full path of the executable
    pathLength       = strlen(MStringRaw(process->path));
    argumentsPointer = malloc(pathLength + 1 + argumentsLength);
    if (!argumentsPointer) {
        return OsOutOfMemory;
    }

    // Build the argument string, remember to null terminate.
    memcpy(&argumentsPointer[0], (const void *)MStringRaw(process->path), pathLength);
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
    return OsSuccess;
}

static OsStatus_t
BuildInheritationBlock(
        _In_ Process_t* process,
        _In_ Request_t* request)
{
    size_t inheritationBlockLength;

    if (request->parameters.spawn.inherit != NULL) {
        const stdio_inheritation_block_t* block = request->parameters.spawn.inherit;

        inheritationBlockLength = sizeof(stdio_inheritation_block_t) +
                                  (block->handle_count * sizeof(struct stdio_handle));
        process->inheritation_block = malloc(inheritationBlockLength);
        if (!process->inheritation_block) {
            return OsOutOfMemory;
        }

        process->inheritation_block_length = inheritationBlockLength;
        memcpy(process->inheritation_block, block, inheritationBlockLength);
    }
    return OsSuccess;
}

static OsStatus_t
LoadProcessImage(
        _In_  Request_t*       request,
        _Out_ PeExecutable_t** image)
{
    OsStatus_t osStatus;
    MString_t* path;

    ENTRY("LoadProcessImage(path=%s, args=%s)",
          request->parameters.spawn.path,
          request->parameters.spawn.args);

    path     = MStringCreate((void *)request->parameters.spawn.path, StrUTF8);
    osStatus = PeLoadImage(UUID_INVALID, NULL, path, image);

    EXIT("LoadProcessImage");
    return osStatus;
}

static OsStatus_t
PmProcessNew(
        _In_  Request_t*      request,
        _In_  UUId_t          handle,
        _In_  PeExecutable_t* image,
        _Out_ Process_t**     processOut)
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
    memcpy(&process->config, &request->parameters.spawn.conf, sizeof(ProcessConfiguration_t));
    usched_mtx_init(&process->lock);
    list_construct(&process->requests);

    process->path = MStringClone(process->image->FullPath);
    if (!process->path) {
        free(process);
        return OsOutOfMemory;
    }

    index = MStringFindReverse(process->path, '/', 0);
    process->name              = MStringSubString(process->path, index + 1, -1);
    process->working_directory  = MStringSubString(process->path, 0, index);
    process->assembly_directory = MStringSubString(process->path, 0, index);
    if (!process->name || !process->working_directory || !process->assembly_directory) {
        MStringDestroy(process->path);
        MStringDestroy(process->name);
        MStringDestroy(process->working_directory);
        MStringDestroy(process->assembly_directory);
        free(process);
        return OsOutOfMemory;
    }

    *processOut = process;
    return OsSuccess;
}

static OsStatus_t
StartProcess(
        _In_ Process_t* process)
{
    ThreadParameters_t threadParameters;
    OsStatus_t         osStatus;

    // Initialize threading paramaters for the new thread
    InitializeThreadParameters(&threadParameters);
    threadParameters.Name              = MStringRaw(process->name);
    threadParameters.MemorySpaceHandle = (UUId_t)(uintptr_t)process->image->MemorySpace;

    osStatus = Syscall_ThreadCreate(
            process->image->EntryAddress,
            NULL, // Argument
            &threadParameters,
            &process->primary_thread_id);
    if (osStatus == OsSuccess) {
        osStatus = Syscall_ThreadDetach(process->primary_thread_id);
    }
    return osStatus;
}

void PmCreateProcess(
        _In_ Request_t* request,
        _In_ void*      cancellationToken)
{
    PeExecutable_t* image;
    Process_t*      process;
    UUId_t          handle;
    OsStatus_t      osStatus;
    ENTRY("PmCreateProcess(path=%s, args=%s)",
          request->parameters.spawn.path,
          request->parameters.spawn.args);

    osStatus = LoadProcessImage(request, &image);
    if (osStatus != OsSuccess) {
        sys_process_spawn_response(request->message, osStatus, UUID_INVALID);
        goto cleanup;
    }

    osStatus = handle_create(&handle);
    if (osStatus != OsSuccess) {
        ERROR("CreateProcess failed to allocate a system handle for process");
        PeUnloadImage(image);
        sys_process_spawn_response(request->message, osStatus, UUID_INVALID);
        goto cleanup;
    }

    osStatus = PmProcessNew(request, handle, image, &process);
    if (osStatus != OsSuccess) {
        PeUnloadImage(image);
        sys_process_spawn_response(request->message, osStatus, UUID_INVALID);
        goto cleanup;
    }

    osStatus = BuildArguments(process, request);
    if (osStatus != OsSuccess) {
        DestroyProcess(process);
        sys_process_spawn_response(request->message, osStatus, UUID_INVALID);
        goto cleanup;
    }

    osStatus = BuildInheritationBlock(process, request);
    if (osStatus != OsSuccess) {
        DestroyProcess(process);
        sys_process_spawn_response(request->message, osStatus, UUID_INVALID);
        goto cleanup;
    }

    // acquire the processes lock before starting it as we need to add the process entry
    // to the hashtable, but we need the thread id before adding it. So to avoid any data races
    // in a multi-core environment, hold the lock untill we've added it.
    usched_mtx_lock(&g_processesLock);
    osStatus = StartProcess(process);
    if (osStatus != OsSuccess) {
        usched_mtx_unlock(&g_processesLock);
        DestroyProcess(process);
        sys_process_spawn_response(request->message, osStatus, UUID_INVALID);
        goto cleanup;
    }

    // Add it to the list of processes
    TRACE("PmCreateProcess registering process %u/%u", handle, process->primary_thread_id);
    hashtable_set(&g_threadmappings, &(struct thread_mapping) {
            .process_id = handle,
            .thread_id  = process->primary_thread_id
    });
    hashtable_set(&g_processes, &(struct process_entry) {
        .process_id = handle,
        .process    = process
    });
    usched_mtx_unlock(&g_processesLock);

    sys_process_spawn_response(request->message, osStatus, process->handle);

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
    Process_t* process            = GetProcessByThread(request->parameters.get_initblock.threadHandle);
    OsStatus_t osStatus           = OsDoesNotExist;
    UUId_t     processHandle      = UUID_INVALID;
    size_t     argumentLength     = 0;
    size_t     inheritationLength = 0;
    int        moduleCount        = PROCESS_MAXMODULES;
    TRACE("PmGetProcessStartupInformation(thread=%u)", request->parameters.get_initblock.threadHandle);

    if (process) {
        struct dma_attachment dmaAttachment;
        osStatus = dma_attach(request->parameters.get_initblock.bufferHandle, &dmaAttachment);
        if (osStatus == OsSuccess) {
            osStatus = dma_attachment_map(&dmaAttachment, DMA_ACCESS_WRITE);
            if (osStatus == OsSuccess) {
                char* buffer = dmaAttachment.buffer;

                processHandle      = process->handle;
                argumentLength     = process->arguments_length;
                inheritationLength = process->inheritation_block_length;

                memcpy(&buffer[0], process->arguments, argumentLength);
                if (inheritationLength) {
                    memcpy(&buffer[argumentLength], process->inheritation_block, inheritationLength);
                }

                osStatus = PeGetModuleEntryPoints(process->image,
                                                  (Handle_t *)&buffer[argumentLength + inheritationLength],
                                                  &moduleCount);
                dma_attachment_unmap(&dmaAttachment);
            }
            dma_detach(&dmaAttachment);
        }
    }

    sys_process_get_startup_information_response(
            request->message,
            osStatus,
            processHandle,
            argumentLength,
            inheritationLength,
            moduleCount * sizeof(Handle_t));
    RequestDestroy(request);
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
            sys_process_join_response(request->message, OsSuccess, entry->exit_code);
            goto cleanup;
        }
        sys_process_join_response(request->message, OsDoesNotExist, 0);
        goto cleanup;
    }

    // ok so the process is still running/terminating
    usched_mtx_lock(&target->lock);
    if (target->state == ProcessState_RUNNING) {
        int status = usched_cnd_wait_timed(&request->signal, &target->lock,
                                           request->parameters.join.timeout);
        if (status) {
            usched_mtx_unlock(&target->lock);
            sys_process_join_response(request->message, OsTimeout, 0);
            goto exit;
        }
    }
    exitCode = target->exit_code;
    usched_mtx_unlock(&target->lock);

    sys_process_join_response(request->message, OsSuccess, exitCode);

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

    process = GetProcessByHandle(request->parameters.terminate.handle);
    if (!process) {
        // what
        sys_process_terminate_response(request->message, OsDoesNotExist);
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

    sys_process_terminate_response(request->message, OsSuccess);
cleanup:
    RequestDestroy(request);
}

void PmKillProcess(
        _In_ Request_t* request,
        _In_ void*      cancellationToken)
{
    Process_t* victim;
    OsStatus_t osStatus;
    TRACE("PmKillProcess(process=%u)", request->parameters.kill.victim_handle);

    // ok how to handle this, optimally we should check that the process
    // has license to kill before executing the signal
    victim = RegisterProcessRequest(request->parameters.kill.victim_handle, request);
    if (!victim) {
        osStatus = OsDoesNotExist;
        goto exit;
    }

    // try to send a kill signal to primary thread
    usched_mtx_lock(&victim->lock);
    if (victim->state == ProcessState_RUNNING) {
        osStatus = Syscall_ThreadSignal(victim->primary_thread_id, SIGKILL);
    }
    else {
        osStatus = OsInProgress;
    }
    usched_mtx_unlock(&victim->lock);
    UnregisterProcessRequest(victim, request);

exit:
    sys_process_kill_response(request->message, osStatus);
    RequestDestroy(request);
}

void PmLoadLibrary(
        _In_ Request_t* request,
        _In_ void*      cancellationToken)
{
    Process_t*      process;
    PeExecutable_t* executable;
    MString_t*      path;
    OsStatus_t      osStatus = OsDoesNotExist;
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
        osStatus = OsSuccess;
        goto exit;
    }

    path     = MStringCreate((void *)request->parameters.load_library.path, StrUTF8);
    osStatus = PeLoadImage(process->handle, process->image, path, &executable);
    if (osStatus == OsSuccess) {
        handle = executable;
        entry  = executable->EntryAddress;
    }
    MStringDestroy(path);

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
    OsStatus_t osStatus = OsDoesNotExist;
    uintptr_t  address  = 0;
    TRACE("PmGetLibraryFunction(process=%u, func=%s)",
          request->parameters.get_function.handle,
          request->parameters.get_function.name);

    process = GetProcessByHandle(request->parameters.stat_handle.handle);
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
            osStatus = OsSuccess;
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
    OsStatus_t osStatus = OsDoesNotExist;
    TRACE("PmUnloadLibrary(process=%u)",
          request->parameters.unload_library.handle);

    if (request->parameters.unload_library.library_handle == HANDLE_GLOBAL) {
        osStatus = OsSuccess;
        goto respond;
    }

    process = GetProcessByHandle(request->parameters.stat_handle.handle);
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

    process = GetProcessByHandle(request->parameters.stat_handle.handle);
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
    Process_t*  process;
    OsStatus_t  status = OsInvalidParameters;
    const char* name = "";

    process = GetProcessByHandle(request->parameters.stat_handle.handle);
    if (process) {
        usched_mtx_lock(&process->lock);
        name = MStringRaw(process->name);
        usched_mtx_unlock(&process->lock);
        status = OsSuccess;
    }

    // respond
    sys_process_get_name_response(request->message, status, name);

    // cleanup
    RequestDestroy(request);
}

void PmGetTickBase(
        _In_ Request_t* request,
        _In_ void*      cancellationToken)
{
    Process_t*      process;
    OsStatus_t      status = OsInvalidParameters;
    LargeUInteger_t tick;

    process = GetProcessByHandle(request->parameters.stat_handle.handle);
    if (process) {
        usched_mtx_lock(&process->lock);
        tick.QuadPart = clock() - process->tick_base;
        usched_mtx_unlock(&process->lock);
        status = OsSuccess;
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
    Process_t*  process;
    OsStatus_t  status = OsInvalidParameters;
    const char* path   = "";

    process = GetProcessByHandle(request->parameters.stat_handle.handle);
    if (process) {
        usched_mtx_lock(&process->lock);
        path = MStringRaw(process->working_directory);
        usched_mtx_unlock(&process->lock);
        status = OsSuccess;
        TRACE("PmGetWorkingDirectory path=%s", path);
    }

    // respond
    sys_process_get_working_directory_response(request->message, status, path);

    // cleanup
    RequestDestroy(request);
}

void PmSetWorkingDirectory(
        _In_ Request_t* request,
        _In_ void*      cancellationToken)
{
    Process_t* process;
    OsStatus_t status = OsInvalidParameters;

    process = GetProcessByHandle(request->parameters.set_cwd.handle);
    if (process) {
        usched_mtx_lock(&process->lock);
        TRACE("PmSetWorkingDirectory path=%s", request->parameters.set_cwd.path);
        MStringDestroy(process->working_directory);
        process->working_directory = MStringCreate((void*)request->parameters.set_cwd.path, StrUTF8);
        usched_mtx_unlock(&process->lock);
        status = OsSuccess;
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
    Process_t * process;
    OsStatus_t  osStatus = OsInvalidParameters;
    const char* path    = "";

    process = GetProcessByHandle(request->parameters.stat_handle.handle);
    if (process) {
        usched_mtx_lock(&process->lock);
        path = MStringRaw(process->assembly_directory);
        usched_mtx_unlock(&process->lock);
        osStatus = OsSuccess;
    }

    // respond
    sys_process_get_assembly_directory_response(request->message, osStatus, path);

    // cleanup
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
