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
#include <ddk/barrier.h>
#include <ddk/handle.h>
#include <ddk/utils.h>
#include <internal/_syscalls.h> // for Syscall_ThreadCreate
#include <internal/_io.h>
#include <os/threads.h>
#include <os/usched/cond.h>
#include <pe.h>
#include <process.h>
#include <stdlib.h>
#include <string.h>

#include <sys_library_service_server.h>
#include <sys_process_service_server.h>

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
    OSHandleDestroy(process->handle);
    free(process);
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
    usched_mtx_init(&g_processesLock, USCHED_MUTEX_PLAIN);
    hashtable_construct(&g_processHistory, HASHTABLE_MINIMUM_CAPACITY,
                        sizeof(struct process_history_entry), processhistory_hash,
                        processhistory_cmp);
    usched_mtx_init(&g_processHistoryLock, USCHED_MUTEX_PLAIN);
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
        return OS_EOOM;
    }

    // handle arguments, we need to prepend the full path of the executable
    pathLength       = strlen(processPath);
    argumentsPointer = malloc(pathLength + 1 + argumentsLength);
    if (!argumentsPointer) {
        free(processPath);
        return OS_EOOM;
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
    return OS_EOK;
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
            return OS_EOOM;
        }

        process->inheritation_block_length = inheritationBlockLength;
        memcpy(process->inheritation_block, block, inheritationBlockLength);
    }
    return OS_EOK;
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
        return OS_EOOM;
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
        return OS_EOOM;
    }
    memset(process, 0, sizeof(Process_t));

    process->handle = handle;
    process->primary_thread_id = UUID_INVALID;
    process->tick_base = clock();
    process->state = ProcessState_RUNNING;
    process->image = image;
    process->references = 1;
    memcpy(&process->config, config, sizeof(ProcessConfiguration_t));
    usched_mtx_init(&process->mutex, USCHED_MUTEX_PLAIN);
    usched_cnd_init(&process->condition);

    process->path = mstr_clone(process->image->FullPath);
    if (!process->path) {
        free(process);
        return OS_EOOM;
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
        return OS_EOOM;
    }

    *processOut = process;
    return OS_EOK;
}

static oserr_t
__StartProcess(
        _In_ Process_t* process)
{
    ThreadParameters_t threadParameters;
    oserr_t            oserr;
    TRACE("__StartProcess(process=%ms)", process->name);

    // Initialize threading paramaters for the new thread
    ThreadParametersInitialize(&threadParameters);
    threadParameters.MemorySpaceHandle = (uuid_t)(uintptr_t)process->image->MemorySpace;
    threadParameters.Name              = mstr_u8(process->name);
    if (threadParameters.Name == NULL) {
        return OS_EOOM;
    }

    oserr = Syscall_ThreadCreate(
            process->image->EntryAddress,
            NULL, // Argument
            &threadParameters,
            &process->primary_thread_id);
    if (oserr == OS_EOK) {
        oserr = Syscall_ThreadDetach(process->primary_thread_id);
    }
    free((void*)threadParameters.Name);
    return oserr;
}

oserr_t
PmCreateProcess(
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
    ENTRY("PmCreateProcess(path=%s, args=%s)", path, args);

    osStatus = __LoadProcessImage(path, &image);
    if (osStatus != OS_EOK) {
        goto exit;
    }

    osStatus = OSHandleCreate(&handle);
    if (osStatus != OS_EOK) {
        ERROR("PmCreateProcess failed to allocate a system handle for process");
        PeUnloadImage(image);
        goto exit;
    }

    osStatus = __ProcessNew(processConfiguration, handle, image, &process);
    if (osStatus != OS_EOK) {
        PeUnloadImage(image);
        goto exit;
    }

    osStatus = __BuildArguments(process, args);
    if (osStatus != OS_EOK) {
        DestroyProcess(process);
        goto exit;
    }

    osStatus = __BuildInheritationBlock(process, inherit);
    if (osStatus != OS_EOK) {
        DestroyProcess(process);
        goto exit;
    }

    // acquire the processes lock before starting it as we need to add the process entry
    // to the hashtable, but we need the thread id before adding it. So to avoid any data races
    // in a multicore environment, hold the lock untill we've added it.
    usched_mtx_lock(&g_processesLock);
    osStatus = __StartProcess(process);
    if (osStatus != OS_EOK) {
        usched_mtx_unlock(&g_processesLock);
        DestroyProcess(process);
        goto exit;
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

    *handleOut = handle;
exit:
    EXIT("PmCreateProcess");
    return osStatus;
}

oserr_t
PmGetProcessStartupInformation(
        _In_ uuid_t   threadHandle,
        _In_ uuid_t   bufferHandle,
        _In_ size_t   bufferOffset,
        _Out_ uuid_t* processHandleOut)
{
    Process_t* process;
    oserr_t    oserr       = OS_ENOENT;
    int        moduleCount = PROCESS_MAXMODULES;
    TRACE("PmGetProcessStartupInformation(thread=%u)", threadHandle);

    process = GetProcessByThread(threadHandle);
    if (process == NULL) {
        goto exit;
    }
    *processHandleOut = process->handle;

    DMAAttachment_t dmaAttachment;
    oserr = DmaAttach(bufferHandle, &dmaAttachment);
    if (oserr != OS_EOK) {
        ERROR("PmGetProcessStartupInformation failed to attach to user buffer");
        goto exit;
    }

    oserr = DmaAttachmentMap(&dmaAttachment, DMA_ACCESS_WRITE);
    if (oserr != OS_EOK) {
        goto detach;
    }

    ProcessStartupInformation_t* startupInfo = dmaAttachment.buffer;
    char*                        buffer      = (char*)dmaAttachment.buffer + sizeof(ProcessStartupInformation_t);
    memcpy(&buffer[0], process->arguments, process->arguments_length);
    if (process->inheritation_block_length) {
        memcpy(
                &buffer[process->arguments_length],
                process->inheritation_block,
                process->inheritation_block_length
        );
    }

    oserr = PeGetModuleEntryPoints(
            process->image,
            (Handle_t *)&buffer[process->arguments_length + process->inheritation_block_length],
            &moduleCount);

    // fill in the startup header as we now have all info
    startupInfo->ArgumentsLength = process->arguments_length;
    startupInfo->InheritationLength = process->inheritation_block_length;
    startupInfo->LibraryEntriesLength = moduleCount * sizeof(Handle_t);
    startupInfo->EnvironmentBlockLength = 0;

    // unmap and cleanup
    DmaAttachmentUnmap(&dmaAttachment);

detach:
    DmaDetach(&dmaAttachment);

exit:
    return oserr;
}

static void
__get_timestamp_from_now(unsigned int ms, struct timespec* ts)
{
    timespec_get(ts, TIME_UTC);
    ts->tv_nsec += (NSEC_PER_MSEC * ms);
    while (ts->tv_nsec >= NSEC_PER_SEC) {
        ts->tv_sec++;
        ts->tv_nsec -= NSEC_PER_SEC;
    }
}

oserr_t
PmJoinProcess(
        _In_  uuid_t       handle,
        _In_  unsigned int timeout,
        _Out_ int*         exitCodeOut)
{
    Process_t* target;
    oserr_t    oserr = OS_EOK;

    TRACE("PmJoinProcess(process=%u, timeout=%u)", handle, timeout);

    target = PmGetProcessByHandle(handle);
    if (!target) {
        struct process_history_entry* entry;

        // first check process history before responding negative
        usched_mtx_lock(&g_processHistoryLock);
        entry = hashtable_get(&g_processHistory, &(struct process_history_entry) {
                .process_id = handle
        });
        if (entry) {
            *exitCodeOut = entry->exit_code;
            usched_mtx_unlock(&g_processHistoryLock);
            return OS_EOK;
        }
        usched_mtx_unlock(&g_processHistoryLock);
        return OS_ENOENT;
    }

    // ok so the process is still running/terminating
    usched_mtx_lock(&target->mutex);
    if (target->state == ProcessState_RUNNING) {
        struct timespec ts;
        int             status;

        target->references++;
        __get_timestamp_from_now(timeout, &ts);
        status = usched_cnd_timedwait(&target->condition, &target->mutex, &ts);
        if (status) {
            usched_mtx_unlock(&target->mutex);
            oserr = OS_ETIMEOUT;
            goto exit;
        }

        // store exit code and handle cleanup
        *exitCodeOut = target->exit_code;
        target->references--;
        if (target->references == 0) {
            DestroyProcess(target);
            goto exit;
        }
    } else {
        *exitCodeOut = target->exit_code;
    }
    usched_mtx_unlock(&target->mutex);

exit:
    return oserr;
}

oserr_t
PmTerminateProcess(
        _In_ uuid_t processHandle,
        _In_ int    exitCode)
{
    struct process_entry* entry;
    Process_t*            process;

    TRACE("PmTerminateProcess process %u exitted with code: %i",
          processHandle, exitCode);

    // Add a new entry in our history archive before removing to avoid
    // any data races when other requests look for this.
    usched_mtx_lock(&g_processHistoryLock);
    hashtable_set(&g_processHistory, &(struct process_history_entry) {
            .process_id = processHandle,
            .exit_code = exitCode
    });
    usched_mtx_unlock(&g_processHistoryLock);

    // Do manual lookup here as we might be altering the list. We do not want some race
    // where someone gets a hold of the process pointer while we are in the process of
    // removing it.
    usched_mtx_lock(&g_processesLock);
    entry = hashtable_remove(
            &g_processes,
            &(struct process_entry) {
                .process_id = processHandle
            }
    );
    if (entry == NULL) {
        usched_mtx_unlock(&g_processesLock);
        return OS_ENOENT;
    }

    // get a pointer to the actual process before we release the lock
    process = entry->process;
    usched_mtx_unlock(&g_processesLock);

    // At this point it's possible for other requests to hold a pointer to
    // the process (waiters), which means we must wait for all these requests to
    // finish.
    usched_mtx_lock(&process->mutex);

    // update state of process first
    process->state     = ProcessState_TERMINATING;
    process->exit_code = exitCode;

    // handle all waiters (if any)
    if (process->references > 1) {
        process->references--;
        // Wake them, and let them handle cleanup. Let the last waiter
        // cleanup the actual process. Maybe we should spawn a cleanup task or
        // something, but not now.
        usched_cnd_notify_all(&process->condition);
        usched_mtx_unlock(&process->mutex);
    } else {
        DestroyProcess(process);
    }
    return OS_EOK;
}

oserr_t
PmSignalProcess(
        _In_ uuid_t processHandle,
        _In_ uuid_t victimHandle,
        _In_ int    signal)
{
    Process_t* victim;
    oserr_t    oserr;
    TRACE("PmSignalProcess(process=%u, signal=%i)", victimHandle, signal);

    // ok how to handle this, optimally we should check that the process
    // has license to kill before executing the signal
    victim = PmGetProcessByHandle(victimHandle);
    if (!victim) {
        oserr = OS_ENOENT;
        goto exit;
    }

    // try to send a kill signal to primary thread
    usched_mtx_lock(&victim->mutex);
    if (victim->state == ProcessState_RUNNING) {
        oserr = Syscall_ThreadSignal(victim->primary_thread_id, signal);
    } else {
        oserr = OS_EINPROGRESS;
    }
    usched_mtx_unlock(&victim->mutex);

exit:
    return oserr;
}

oserr_t
PmLoadLibrary(
        _In_  uuid_t      processHandle,
        _In_  const char* cpath,
        _Out_ Handle_t*   handleOut,
        _Out_ uintptr_t*  entryPointOut)
{
    Process_t*      process;
    PeExecutable_t* executable;
    mstring_t*      path;
    oserr_t         oserr  = OS_ENOENT;

    ENTRY("PmLoadLibrary(%u, %s)", processHandle, (cpath == NULL) ? "Global" : cpath);

    process = PmGetProcessByHandle(processHandle);
    if (!process) {
        goto exit;
    }

    if (!strlen(cpath)) {
        *handleOut     = HANDLE_GLOBAL;
        *entryPointOut = process->image->EntryAddress;
        oserr = OS_EOK;
        goto exit;
    }

    path  = mstr_new_u8((void *)cpath);
    oserr = PeLoadImage(process->handle, process->image, path, &executable);
    if (oserr == OS_EOK) {
        *handleOut    = executable;
        *entryPointOut = executable->EntryAddress;
    }
    mstr_delete(path);

exit:
    EXIT("LoadProcessLibrary");
    return oserr;
}

oserr_t
PmGetLibraryFunction(
        _In_  uuid_t      processHandle,
        _In_  Handle_t    libraryHandle,
        _In_  const char* name,
        _Out_ uintptr_t*  functionAddressOut)
{
    Process_t*      process;
    PeExecutable_t* executable;
    oserr_t         oserr   = OS_ENOENT;
    TRACE("PmGetLibraryFunction(process=%u, func=%s)", processHandle, name);

    process = PmGetProcessByHandle(processHandle);
    if (process == NULL) {
        return OS_ENOENT;
    }

    usched_mtx_lock(&process->mutex);
    if (libraryHandle != HANDLE_GLOBAL) {
        executable = (PeExecutable_t*)libraryHandle;
    } else {
        executable = process->image;
    }

    *functionAddressOut = PeResolveFunction(executable, name);
    if (*functionAddressOut != 0) {
        oserr = OS_EOK;
    }
    usched_mtx_unlock(&process->mutex);
    return oserr;
}

oserr_t
PmUnloadLibrary(
        _In_ uuid_t   processHandle,
        _In_ Handle_t libraryHandle)
{
    Process_t* process;
    oserr_t    oserr;
    TRACE("PmUnloadLibrary(process=%u)", processHandle);

    if (libraryHandle == HANDLE_GLOBAL) {
        oserr = OS_EOK;
        goto exit;
    }

    process = PmGetProcessByHandle(processHandle);
    if (process == NULL) {
        oserr = OS_ENOENT;
        goto exit;
    }

    usched_mtx_lock(&process->mutex);
    oserr = PeUnloadLibrary(process->image, (PeExecutable_t *)libraryHandle);
    usched_mtx_unlock(&process->mutex);

exit:
    return oserr;
}

oserr_t
PmGetModules(
        _In_ uuid_t    processHandle,
        _In_ Handle_t* modules,
        _In_ int*      modulesCount)
{
    Process_t* process;

    process = PmGetProcessByHandle(processHandle);
    if (process == NULL) {
        return OS_ENOENT;
    }

    usched_mtx_lock(&process->mutex);
    PeGetModuleHandles(process->image, modules, modulesCount);
    usched_mtx_unlock(&process->mutex);
    return OS_EOK;
}

oserr_t
PmGetName(
        _In_  uuid_t      processHandle,
        _Out_ mstring_t** nameOut)
{
    Process_t* process;

    process = PmGetProcessByHandle(processHandle);
    if (process == NULL) {
        return OS_ENOENT;
    }

    *nameOut = process->name;
    return OS_EOK;
}

oserr_t
PmGetTickBase(
        _In_ uuid_t        processHandle,
        _In_ UInteger64_t* tick)
{
    Process_t* process;

    process = PmGetProcessByHandle(processHandle);
    if (process == NULL) {
        return OS_ENOENT;
    }
    tick->QuadPart = clock() - process->tick_base;
    return OS_EOK;
}

oserr_t
PmGetWorkingDirectory(
        _In_  uuid_t      processHandle,
        _Out_ mstring_t** pathOut)
{
    Process_t* process;

    process = PmGetProcessByHandle(processHandle);
    if (process == NULL) {
        return OS_ENOENT;
    }

    usched_mtx_lock(&process->mutex);
    *pathOut = process->working_directory;
    usched_mtx_unlock(&process->mutex);
    return OS_EOK;
}

oserr_t
PmSetWorkingDirectory(
        _In_ uuid_t      processHandle,
        _In_ const char* path)
{
    Process_t* process;
    mstring_t* newCwd;

    process = PmGetProcessByHandle(processHandle);
    if (process == NULL) {
        return OS_ENOENT;
    }

    newCwd = mstr_new_u8(path);
    if (newCwd == NULL) {
        return OS_EOOM;
    }

    usched_mtx_lock(&process->mutex);
    TRACE("PmSetWorkingDirectory path=%s", path);
    mstr_delete(process->working_directory);
    process->working_directory = newCwd;
    usched_mtx_unlock(&process->mutex);
    return OS_EOK;
}

oserr_t
PmGetAssemblyDirectory(
        _In_  uuid_t      processHandle,
        _Out_ mstring_t** pathOut)
{
    Process_t* process;

    process = PmGetProcessByHandle(processHandle);
    if (process == NULL) {
        return OS_ENOENT;
    }
    *pathOut = process->assembly_directory;
    return OS_EOK;
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
