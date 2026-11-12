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

#define __TRACE

#include <assert.h>
#include <sys_process_service.h>
#include <limits.h>
#include <ds/mstring.h>
#include <ds/hashtable.h>
#include <ddk/barrier.h>
#include <os/handle.h>
#include <ddk/capabilities.h>
#include <ddk/utils.h>
#include <internal/_syscalls.h> // for Syscall_ThreadCreate
#include <internal/_process.h>
#include <ddk/convert/inheritance.h>
#include <os/threads.h>
#include <os/usched/cond.h>
#include <os/shm.h>
#include <pe.h>
#include <process.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

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

    // The final owner calls cleanup without holding the process mutex.
    usched_mtx_lock(&g_processesLock);
    hashtable_remove(&g_threadmappings, &(struct thread_mapping){ .thread_id = process->primary_thread_id });
    hashtable_remove(&g_processes, &(struct process_entry){ .process_id = process->handle.ID });
    usched_mtx_unlock(&g_processesLock);

    mstr_delete(process->name);
    mstr_delete(process->working_directory);
    mstr_delete(process->assembly_directory);
    PEImageLoadContextDelete(process->load_context);
    OSHandleDestroy(&process->handle);
    free(process->startup_data.Buffer);
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

static oserr_t
__ReferenceProcess(
    _In_  struct process_entry* entry,
    _Out_ Process_t**           processOut)
{
    Process_t* process;
    oserr_t    oserr = OS_EOK;

    // The caller holds the registry lock until the reference has been added.
    if (entry == NULL) {
        return OS_ENOENT;
    }

    process = entry->process;
    usched_mtx_lock(&process->mutex);
    if (process->references == INT_MAX) {
        oserr = OS_EOVERFLOW;
    } else {
        process->references++;
        *processOut = process;
    }
    usched_mtx_unlock(&process->mutex);
    return oserr;
}

oserr_t
PmReferenceProcessByThread(
    _In_  uuid_t      threadHandle,
    _Out_ Process_t** processOut)
{
    struct thread_mapping* mapping;
    struct process_entry*  entry;
    oserr_t                oserr;

    usched_mtx_lock(&g_processesLock);
    mapping = hashtable_get(&g_threadmappings, &(struct thread_mapping) { .thread_id = threadHandle });
    entry = mapping == NULL ? NULL : hashtable_get(
        &g_processes, &(struct process_entry) { .process_id = mapping->process_id }
    );
    oserr = __ReferenceProcess(entry, processOut);
    usched_mtx_unlock(&g_processesLock);
    return oserr;
}

void
PmReleaseProcess(
    _In_ Process_t* process)
{
    int destroy;

    usched_mtx_lock(&process->mutex);
    process->references--;
    destroy = process->references == 0;
    usched_mtx_unlock(&process->mutex);
    if (destroy) {
        DestroyProcess(process);
    }
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

static const char*
__ProcOptsEnvironmentBlock(
    _In_ const struct ProcessOptions* options)
{
    if (options->DataBuffer == NULL) {
        return NULL;
    }
    return (const char*)options->DataBuffer + options->InheritationBlockLength;
}

static bool
__FitsStartupPayload(
    _In_ const char* string)
{
    return strnlen(string, PROCESS_STARTUP_MAX_SIZE) < PROCESS_STARTUP_MAX_SIZE;
}

static oserr_t
__ValidateProcessStrings(
    _In_ const char* path,
    _In_ const char* arguments)
{
    if (!__FitsStartupPayload(path)) {
        return OS_EBUFFER;
    }
    if (arguments != NULL && !__FitsStartupPayload(arguments)) {
        return OS_EBUFFER;
    }
    return OS_EOK;
}

static oserr_t
__ValidateProcessBlocks(
    _In_ const struct ProcessOptions* options)
{
    size_t count;

    // Check the lengths before the environment block is located from them.
    if (options->InheritationBlockLength > PROCESS_STARTUP_MAX_SIZE ||
        options->EnvironmentBlockLength > PROCESS_STARTUP_MAX_SIZE - options->InheritationBlockLength) {
        return OS_EBUFFER;
    }
    return __EnvironmentBlockValidate(
        __ProcOptsEnvironmentBlock(options),
        options->EnvironmentBlockLength,
        &count
    );
}

static oserr_t
__ValidateProcessInput(
    _In_ const char*            path,
    _In_ const char*            arguments,
    _In_ struct ProcessOptions* options)
{
    oserr_t oserr;

    oserr = __ValidateProcessStrings(path, arguments);
    if (oserr != OS_EOK) {
        return oserr;
    }
    return __ValidateProcessBlocks(options);
}

static oserr_t
__BuildArguments(
    _In_  Process_t*  process,
    _In_  const char* argsIn,
    _Out_ char**      argumentsOut,
    _Out_ size_t*     lengthOut)
{
    mstring_t* fullPath;
    size_t     pathLength;
    size_t     argumentsLength = 0;
    char*      argumentsPointer;
    char*      processPath;
    oserr_t    oserr;
    TRACE("__BuildArguments()");

    // check for null or empty
    if (argsIn && strlen(argsIn)) {
        // include zero terminator
        argumentsLength = strlen(argsIn) + 1;
    }

    oserr = PEImageLoadContextModulePath(
        process->load_context,
        process->load_context->RootModule,
        &fullPath
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    processPath = mstr_u8(fullPath);
    if (processPath == NULL) {
        return OS_EOOM;
    }

    // handle arguments, we need to prepend the full path of the executable
    pathLength = strlen(processPath);
    if (pathLength >= PROCESS_STARTUP_MAX_SIZE ||
        argumentsLength > PROCESS_STARTUP_MAX_SIZE - pathLength - 1) {
        free(processPath);
        return OS_EBUFFER;
    }

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

    *argumentsOut = argumentsPointer;
    *lengthOut = pathLength + 1 + argumentsLength;
    free(processPath);
    return OS_EOK;
}

static oserr_t
__LoadProcessImage(
    _In_  uuid_t                      scope,
    _In_  const char*                 ldPaths,
    _In_  const char*                 cpath,
    _Out_ struct PEImageLoadContext** loadContextOut)
{
    struct PEImageLoadContext* loadContext;
    mstring_t*                 path;
    oserr_t                    oserr;
    ENTRY("__LoadProcessImage(path=%s)", cpath);

    loadContext = PEImageLoadContextNew(scope, ldPaths);
    if (loadContext == NULL) {
        oserr = OS_EOOM;
        goto exit;
    }

    path = mstr_new_u8(cpath);
    if (path == NULL) {
        oserr = OS_EOOM;
        goto exit;
    }

    oserr = PEImageLoad(loadContext, path, true);
    if (oserr != OS_EOK) {
        goto exit;
    }

    *loadContextOut = loadContext;

exit:
    if (oserr != OS_EOK) {
        PEImageLoadContextDelete(loadContext);
    }
    EXIT("__LoadProcessImage");
    return oserr;
}

static mstring_t*
__SelectWorkingDirectory(
    _In_ struct ProcessOptions* procOpts,
    _In_ mstring_t*             fullPath)
{
    if (procOpts->WorkingDirectory == NULL || strlen(procOpts->WorkingDirectory) == 0) {
        return mstr_path_dirname(fullPath);
    }
    return mstr_new_u8(procOpts->WorkingDirectory);
}

static oserr_t
__ProcessNew(
    _In_  struct ProcessOptions*     procOpts,
    _In_  OSHandle_t*                handle,
    _In_  struct PEImageLoadContext* loadContext,
    _Out_ Process_t**                processOut)
{
    Process_t* process;
    mstring_t* fullPath;
    oserr_t    oserr;
    TRACE("__ProcessNew()");

    process = (Process_t*)malloc(sizeof(Process_t));
    if (!process) {
        return OS_EOOM;
    }
    memset(process, 0, sizeof(Process_t));

    memcpy(&process->handle, handle, sizeof(OSHandle_t));
    process->primary_thread_id = UUID_INVALID;
    process->capabilities = procOpts->Capabilities;
    process->tick_base = clock();
    process->state = ProcessState_RUNNING;
    process->load_context = loadContext;
    process->references = 1;

    usched_mtx_init(&process->mutex, USCHED_MUTEX_PLAIN);
    usched_cnd_init(&process->condition);

    oserr = PEImageLoadContextModulePath(
        loadContext,
        loadContext->RootModule,
        &fullPath
    );
    if (oserr != OS_EOK) {
        free(process);
        return oserr;
    }

    process->name               = mstr_path_basename(fullPath);
    process->working_directory  = __SelectWorkingDirectory(procOpts, fullPath);
    process->assembly_directory = mstr_path_dirname(fullPath);
    if (!process->name || !process->working_directory || !process->assembly_directory) {
        mstr_delete(process->name);
        mstr_delete(process->working_directory);
        mstr_delete(process->assembly_directory);
        free(process);
        return OS_EOOM;
    }

    TRACE("__ProcessNew name=%ms", process->name);
    TRACE("__ProcessNew working_directory=%ms", process->working_directory);
    TRACE("__ProcessNew assembly_directory=%ms", process->assembly_directory);

    *processOut = process;
    return OS_EOK;
}

static oserr_t
__StartProcess(
    _In_ Process_t* process)
{
    OSThreadParameters_t threadParameters;
    uintptr_t            entryPoint;
    oserr_t              oserr;
    TRACE("__StartProcess(process=%ms)", process->name);

    oserr = PEImageLoadContextModuleEntryPoint(
        process->load_context,
        process->load_context->RootModule,
        &entryPoint
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    // Initialize threading paramaters for the new thread
    ThreadParametersInitialize(&threadParameters);
    threadParameters.MemorySpaceHandle = (uuid_t)(uintptr_t)process->load_context->MemorySpace;
    threadParameters.Name              = mstr_u8(process->name);
    if (threadParameters.Name == NULL) {
        return OS_EOOM;
    }

    oserr = Syscall_ThreadCreate(
        entryPoint,
        NULL, // Argument
        &threadParameters,
        &process->primary_thread_id
    );
    if (oserr == OS_EOK) {
        oserr = Syscall_ThreadDetach(process->primary_thread_id);
    }
    free((void*)threadParameters.Name);
    return oserr;
}

// stolen rutine from somewhere xd
static void
__DumpMemory(
    _In_ const void* buffer,
    _In_ size_t      bufferLength)
{
    char *data;
    size_t len;
    FILE *file = open_memstream(&data, &len);

    int i;
    unsigned char buff[17];       // stores the ASCII data
    const unsigned char *pc = buffer;   // cast to make the code cleaner.

    // Process every byte in the data.
    for (i = 0; i < bufferLength; i++) {
        // Multiple of 16 means new line (with line offset).
        if ((i % 16) == 0) {
            // Just don't print ASCII for the zeroth line.
            if (i != 0)
                fprintf(file, "  %s\n", buff);

            // Output the offset.
            fprintf(file, "  %04x ", i);
        }

        // Now the hex code for the specific character.
        fprintf(file, " %02x", pc[i]);

        // And store a printable ASCII character for later.
        if ((pc[i] < 0x20) || (pc[i] > 0x7e))
            buff[i % 16] = '.';
        else
            buff[i % 16] = pc[i];
        buff[(i % 16) + 1] = '\0';
    }

    // Pad out last line if not exactly 16 characters.
    while ((i % 16) != 0) {
        fprintf(file, "   ");
        i++;
    }

    // And print the final ASCII bit.
    fprintf(file, "  %s\n", buff);
    fclose(file);
    TRACE("\n%s", data);
    free(data);
}

static const char*
__GetLoadPaths(
    _In_ struct ProcessOptions* procOpts)
{
    const char* ldpaths = NULL;
    const char* environ;
    TRACE("__GetLoadPaths(length=%u)", procOpts->EnvironmentBlockLength);

    if (procOpts->EnvironmentBlockLength == 0) {
        return NULL;
    }

    // Try to locate LDPATH in a double zero-terminated array
    environ = __ProcOptsEnvironmentBlock(procOpts);
    for (size_t i = 0; environ[i]; i += strlen(&environ[i]) + 1) {
        TRACE("__GetLoadPaths checking %s", &environ[i]);
        if (!strncmp(&environ[i], "LDPATH=", 7)) {
            ldpaths = &environ[i];
            break;
        }
    }
    return ldpaths;
}

static oserr_t
__CreateProcess(
    _In_  const char*            path,
    _In_  const char*            args,
    _In_  struct ProcessOptions* procOpts,
    _Out_ OSProcessIdentity_t*   resultOut)
{
    struct sys_process_inheritance_input input = { 0 };
    OSProcessIdentity_t                  identity = { 0 };
    struct PEImageLoadContext*           loadContext;
    Process_t*                           process;
    OSHandle_t                           handle;
    oserr_t                              oserr;
    const char*                          loadPaths;
    char*                                arguments = NULL;
    size_t                               argumentsLength;
    ENTRY("PmCreateProcess(path=%s, args=%s)", path, args);

    oserr = __ValidateProcessInput(path, args, procOpts);
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = __ReadInheritanceInput(
        procOpts->DataBuffer,
        procOpts->InheritationBlockLength,
        &input
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    loadPaths = __GetLoadPaths(procOpts);
    if (loadPaths == NULL) {
        ERROR("PmCreateProcess no LDPATH were supplied.");
        oserr = OS_EOOM;
        goto exit;
    }

    oserr = __LoadProcessImage(
        procOpts->Scope,
        loadPaths,
        path,
        &loadContext
    );
    if (oserr != OS_EOK) {
        goto exit;
    }

    oserr = OSHandleCreate(NULL, 0, &handle);
    if (oserr != OS_EOK) {
        ERROR("PmCreateProcess failed to allocate a system handle for process");
        PEImageLoadContextDelete(loadContext);
        goto exit;
    }

    oserr = __ProcessNew(
        procOpts,
        &handle,
        loadContext,
        &process
    );
    if (oserr != OS_EOK) {
        OSHandleDestroy(&handle);
        PEImageLoadContextDelete(loadContext);
        goto exit;
    }

    // Set the capabilities for the process based on the process options.
    if (process->capabilities) {
        // The request boundary checked the sender. The kernel independently
        // checks processd's own authority to perform the grant.
        oserr = CapabilityGrant(
            process->capabilities,
            process->load_context->MemorySpace
        );
        if (oserr != OS_EOK) {
            ERROR("PmCreateProcess failed to grant capabilities");
            DestroyProcess(process);
            goto exit;
        }
    }

    oserr = __BuildArguments(process, args, &arguments, &argumentsLength);
    if (oserr != OS_EOK) {
        DestroyProcess(process);
        goto exit;
    }

    oserr = PmPrepareStartupInformation(
        process,
        __ProcOptsEnvironmentBlock(procOpts),
        procOpts->EnvironmentBlockLength,
        arguments,
        argumentsLength,
        &input
    );
    if (oserr != OS_EOK) {
        DestroyProcess(process);
        goto exit;
    }

    // Capture the identity before the child can run. After publication, a child
    // that exits immediately may release its load context before this call returns.
    identity.Process = handle.ID;
    identity.Principal = (uint64_t)loadContext->SecurityGroupID;

    // acquire the processes lock before starting it as we need to add the process entry
    // to the hashtable, but we need the thread id before adding it. So to avoid any data races
    // in a multicore environment, hold the lock untill we've added it.
    usched_mtx_lock(&g_processesLock);
    oserr = __StartProcess(process);
    if (oserr != OS_EOK) {
        usched_mtx_unlock(&g_processesLock);
        DestroyProcess(process);
        goto exit;
    }

    // Add it to the list of processes
    TRACE("PmCreateProcess registering process %u/%u", handle.ID, process->primary_thread_id);
    hashtable_set(&g_threadmappings, &(struct thread_mapping) {
        .process_id = handle.ID,
        .thread_id  = process->primary_thread_id
    });
    hashtable_set(&g_processes, &(struct process_entry) {
        .process_id = handle.ID,
        .process    = process
    });
    usched_mtx_unlock(&g_processesLock);

    *resultOut = identity;
exit:
    free(arguments);
    sys_process_inheritance_input_destroy(&input);
    EXIT("PmCreateProcess");
    return oserr;
}

oserr_t
PmAuthorizeSpawn(
    _In_ const struct gracht_security_context* caller,
    _In_ Capabilities_t                        capabilities)
{
    if ((uint32_t)capabilities & ~(uint32_t)CAPABILITY_ALL) {
        WARNING("PmAuthorizeSpawn rejected unknown child capabilities 0x%x", capabilities);
        return OS_EINVALPARAMS;
    }
    
    if (capabilities == 0) {
        return OS_EOK;
    }
    
    if (!__IsAuthenticatedCaller(caller)) {
        WARNING("PmAuthorizeSpawn rejected privileged spawn without authenticated credentials");
        return OS_EPERMISSIONS;
    }
    
    if (!(caller->capabilities & CAPABILITY_GRANT_CAPABILITY)) {
        WARNING("PmAuthorizeSpawn principal=%llu cannot grant child capabilities 0x%x",
                (unsigned long long)caller->principal, capabilities);
        return OS_EPERMISSIONS;
    }
    
    TRACE("PmAuthorizeSpawn principal=%llu authorized child capabilities 0x%x",
          (unsigned long long)caller->principal, capabilities);
    return OS_EOK;
}

oserr_t
PmCreateProcess(
    _In_  const struct gracht_security_context* caller,
    _In_  const char*                          path,
    _In_  const char*                          args,
    _In_  struct ProcessOptions*               procOpts,
    _Out_ OSProcessIdentity_t*                 resultOut)
{
    oserr_t status;

    status = PmAuthorizeSpawn(caller, procOpts->Capabilities);
    if (status != OS_EOK) {
        return status;
    }
    return __CreateProcess(path, args, procOpts, resultOut);
}

oserr_t
PmCreateServiceProcess(
    _In_  const char*            path,
    _In_  const char*            args,
    _In_  struct ProcessOptions* procOpts,
    _Out_ uuid_t*                handleOut)
{
    OSProcessIdentity_t identity;
    oserr_t             status;

    if ((uint32_t)procOpts->Capabilities & ~(uint32_t)CAPABILITY_ALL) {
        return OS_EINVALPARAMS;
    }
    TRACE("processd: bootstrap service %s capabilities=0x%x", path, procOpts->Capabilities);
    status = __CreateProcess(path, args, procOpts, &identity);
    if (status == OS_EOK) {
        *handleOut = identity.Process;
    }
    return status;
}

static void
__get_timestamp_from_now(
    _In_  unsigned int     ms,
    _Out_ struct timespec* ts)
{
    timespec_get(ts, TIME_UTC);
    ts->tv_sec += ms / 1000;
    ts->tv_nsec += NSEC_PER_MSEC * (ms % 1000);
    if (ts->tv_nsec >= NSEC_PER_SEC) {
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
    struct process_entry*         entry;
    struct process_history_entry* history;
    Process_t*                    target;
    struct timespec               until;
    oserr_t                       oserr = OS_EOK;
    int                           status;
    TRACE("PmJoinProcess(process=%u, timeout=%u)", handle, timeout);

    // Pin the process before dropping the registry lock. Termination removes
    // its entry under the same lock before it can release the registry owner.
    usched_mtx_lock(&g_processesLock);
    entry = hashtable_get(&g_processes, &(struct process_entry) { .process_id = handle });
    oserr = __ReferenceProcess(entry, &target);
    usched_mtx_unlock(&g_processesLock);
    if (oserr == OS_ENOENT) {
        usched_mtx_lock(&g_processHistoryLock);
        history = hashtable_get(&g_processHistory, &(struct process_history_entry) {
            .process_id = handle
        });
        if (history != NULL) {
            *exitCodeOut = history->exit_code;
            oserr = OS_EOK;
        } else {
            oserr = OS_ENOENT;
        }
        usched_mtx_unlock(&g_processHistoryLock);
        return oserr;
    }
    if (oserr != OS_EOK) {
        return oserr;
    }

    usched_mtx_lock(&target->mutex);
    __get_timestamp_from_now(timeout, &until);
    
    while (target->state == ProcessState_RUNNING) {
        status = usched_cnd_timedwait(&target->condition, &target->mutex, &until);
        if (status != 0 && target->state == ProcessState_RUNNING) {
            oserr = OS_ETIMEOUT;
            break;
        }
    }
    if (oserr == OS_EOK) {
        *exitCodeOut = target->exit_code;
    }

    usched_mtx_unlock(&target->mutex);
    PmReleaseProcess(target);
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

    usched_cnd_notify_all(&process->condition);
    usched_mtx_unlock(&process->mutex);
    PmReleaseProcess(process);
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
    void*           imageKey;
    uintptr_t       imageEntryPoint;
    mstring_t*      path;
    oserr_t         oserr  = OS_ENOENT;

    ENTRY("PmLoadLibrary(%u, %s)", processHandle, (cpath == NULL) ? "Global" : cpath);

    process = PmGetProcessByHandle(processHandle);
    if (!process) {
        goto exit;
    }

    if (!strlen(cpath)) {
        oserr = PEImageLoadContextModuleEntryPoint(
                process->load_context,
                process->load_context->RootModule,
                entryPointOut
        );
        goto exit;
    }

    path  = mstr_new_u8((void*)cpath);
    oserr = PEImageLoadLibrary(
            process->load_context,
            path,
            &imageKey,
            &imageEntryPoint
    );
    if (oserr == OS_EOK) {
        *handleOut     = imageKey;
        *entryPointOut = imageEntryPoint;
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
    Process_t* process;
    void*      imageKey;
    oserr_t    oserr   = OS_ENOENT;
    TRACE("PmGetLibraryFunction(process=%u, func=%s)", processHandle, name);

    process = PmGetProcessByHandle(processHandle);
    if (process == NULL) {
        return OS_ENOENT;
    }

    usched_mtx_lock(&process->mutex);
    if (libraryHandle != HANDLE_GLOBAL) {
        imageKey = libraryHandle;
    } else {
        imageKey = process->load_context->RootModule;
    }

    *functionAddressOut = PEImageFindExport(
            process->load_context,
            imageKey,
            name
    );
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
    oserr = PEImageUnload(process->load_context, libraryHandle, false);
    usched_mtx_unlock(&process->mutex);

exit:
    return oserr;
}

oserr_t
PmGetModules(
    _In_  uuid_t    processHandle,
    _Out_ Handle_t* modules,
    _Out_ int*      modulesCount)
{
    Process_t* process;
    oserr_t    oserr;

    process = PmGetProcessByHandle(processHandle);
    if (process == NULL) {
        return OS_ENOENT;
    }

    usched_mtx_lock(&process->mutex);
    oserr = PEModuleKeys(
            process->load_context,
            modules,
            modulesCount
    );
    usched_mtx_unlock(&process->mutex);
    if (oserr != OS_EOK) {
        return oserr;
    }

    // Do not return the module key for the main program
    (*modulesCount)--;
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
