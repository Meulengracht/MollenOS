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

//#define __TRACE

#include <assert.h>
#include <ds/mstring.h>
#include <ds/hashtable.h>
#include <ddk/eventqueue.h>
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

#include "svc_library_protocol_server.h"
#include "svc_process_protocol_server.h"

struct process_history_entry {
    UUId_t process_id;
    int    exit_code;
};

static list_t        g_processes = LIST_INIT;
static list_t        g_joiners   = LIST_INIT;
static EventQueue_t* g_eventQueue = NULL;
static hashtable_t   g_processHistory;

static uint64_t ProcessHistoryHash(const void* element);
static int      ProcessHistoryCmp(const void* element1, const void* element2);

static OsStatus_t
DestroyProcess(
        _In_ Process_t* process)
{
    int References = atomic_fetch_sub(&process->References, 1);
    if (References == 1) {
        UUId_t Handle = (UUId_t) (uintptr_t) process->Header.key;
        list_remove(&g_processes, &process->Header);
        if (process->Name != NULL) {
            MStringDestroy(process->Name);
        }
        if (process->Path != NULL) {
            MStringDestroy(process->Path);
        }
        if (process->WorkingDirectory != NULL) {
            MStringDestroy(process->WorkingDirectory);
        }
        if (process->AssemblyDirectory != NULL) {
            MStringDestroy(process->AssemblyDirectory);
        }
        if (process->Executable != NULL) {
            PeUnloadImage(process->Executable);
        }
        handle_destroy(Handle);
        free(process);
        return OsSuccess;
    }
    return OsError;
}

void
ReleaseProcess(
        _In_ Process_t* process)
{
    if (!process) {
        return;
    }

    if (DestroyProcess(process) != OsSuccess) {
        spinlock_release(&process->SyncObject);
    }
}

Process_t*
AcquireProcess(
        _In_ UUId_t Handle)
{
    Process_t* process = (Process_t*)list_find_value(&g_processes, (void *)(uintptr_t)Handle);
    if (process != NULL) {
        int References;
        while (1) {
            References = atomic_load(&process->References);
            if (References == 0) {
                break;
            }
            if (atomic_compare_exchange_weak(&process->References, &References, References + 1)) {
                break;
            }
        }

        if (References > 0) {
            spinlock_acquire(&process->SyncObject);
            if (process->State == PROCESS_RUNNING) {
                return process;
            }
            ReleaseProcess(process);
        }
    }
    return NULL;
}

static void
HandleJoinProcess(
        _In_ void* context)
{
    ProcessJoiner_t* waitContext = (ProcessJoiner_t*)context;
    OsStatus_t       status      = OsTimeout;
    int              exitCode    = 0;

    // Notify application about this
    if (waitContext->Process->State == PROCESS_TERMINATING) {
        status   = OsSuccess;
        exitCode = waitContext->Process->ExitCode;
    }

    svc_process_join_response(&waitContext->DeferredResponse.recv_message, status, exitCode);
    DestroyProcess(waitContext->Process);
    free(waitContext);
}

static OsStatus_t
TestFilePath(
        _In_
        MString_t *Path)
{
    OsFileDescriptor_t FileStats;
    if (GetFileInformationFromPath((const char *) MStringRaw(Path), &FileStats) != FsOk) {
        return OsError;
    }
    return OsSuccess;
}

static OsStatus_t
GuessBasePath(
        _In_  UUId_t ProcessId,
        _In_  MString_t *Path,
        _Out_ MString_t **FullPathOut)
{
    // Check the working directory, if it fails iterate the environment defaults
    Process_t *Process = AcquireProcess(ProcessId);
    MString_t *Result;
    int       IsApp;
    int       IsDll;

    // Start by testing against the loaders current working directory,
    // however this won't work for the base process
    if (Process != NULL) {
        Result = MStringClone(Process->WorkingDirectory);
        ReleaseProcess(Process);
        MStringAppendCharacter(Result, '/');
        MStringAppend(Result, Path);
        if (TestFilePath(Result) == OsSuccess) {
            *FullPathOut = Result;
            return OsSuccess;
        }
    }
    else {
        Result = MStringCreate(NULL, StrUTF8);
    }

    // At this point we have to run through all PATH values
    // Look at the type of file we are trying to load. .app? .dll? 
    // for other types its most likely resource load
    IsApp = MStringFindCString(Path, ".app");
    IsDll = MStringFindCString(Path, ".dll");
    if (IsApp != MSTRING_NOT_FOUND || IsDll != MSTRING_NOT_FOUND) {
        MStringReset(Result, "$bin/", StrUTF8);
    }
    else {
        MStringReset(Result, "$sys/", StrUTF8);
    }
    MStringAppend(Result, Path);
    if (TestFilePath(Result) == OsSuccess) {
        *FullPathOut = Result;
        return OsSuccess;
    }
    else {
        MStringDestroy(Result);
        return OsError;
    }
}

OsStatus_t
ResolveFilePath(
        _In_  UUId_t ProcessId,
        _In_  MString_t *Path,
        _Out_ MString_t **FullPathOut)
{
    OsStatus_t Status           = OsSuccess;
    MString_t  *TemporaryResult = Path;
    TRACE("[resolve] resolve %s", MStringRaw(Path));

    if (MStringFind(Path, ':', 0) == MSTRING_NOT_FOUND) {
        // If we don't even have an environmental identifier present, we
        // have to get creative and guess away
        if (MStringFind(Path, '$', 0) == MSTRING_NOT_FOUND) {
            Status = GuessBasePath(ProcessId, Path, &TemporaryResult);

            TRACE("[resolve] base path %s", MStringRaw(TemporaryResult));

            // If we already deduced an absolute path skip the canonicalizing moment
            if (Status == OsSuccess && MStringFind(TemporaryResult, ':', 0) != MSTRING_NOT_FOUND) {
                *FullPathOut = TemporaryResult;
                return Status;
            }
        }

        // Take into account we might have failed to guess base path
        if (Status == OsSuccess) {
            char *CanonicalizedPath = (char *) malloc(_MAXPATH);
            if (!CanonicalizedPath) {
                return OsOutOfMemory;
            }
            memset(CanonicalizedPath, 0, _MAXPATH);

            Status = PathCanonicalize(MStringRaw(TemporaryResult), CanonicalizedPath, _MAXPATH);
            TRACE("[resolve] canon %s", CanonicalizedPath);
            if (Status == OsSuccess) {
                *FullPathOut = MStringCreate(CanonicalizedPath, StrUTF8);
            }
            free(CanonicalizedPath);
        }
    }
    else {
        // Assume absolute path
        *FullPathOut = MStringClone(TemporaryResult);
    }
    return Status;
}

OsStatus_t
LoadFile(
        _In_  MString_t* FullPath,
        _Out_ void**     BufferOut,
        _Out_ size_t*    LengthOut)
{
    FILE   *file;
    long   fileSize;
    void   *fileBuffer;
    size_t bytesRead;

    TRACE("[load_file] %s", MStringRaw(FullPath));

    file = fopen(MStringRaw(FullPath), "rb");
    if (!file) {
        ERROR("[load_file] [open_file] failed: %i", errno);
        return OsError;
    }

    fseek(file, 0, SEEK_END);
    fileSize = ftell(file);
    rewind(file);

    TRACE("[load_file] size %" PRIuIN, fileSize);
    fileBuffer = malloc(fileSize);
    if (!fileBuffer) {
        ERROR("[load_file] [malloc] null");
        fclose(file);
        return OsOutOfMemory;
    }

    bytesRead = fread(fileBuffer, 1, fileSize, file);
    fclose(file);

    TRACE("[load_file] [transfer_file] read %" PRIuIN " bytes from file", bytesRead);

    *BufferOut = fileBuffer;
    *LengthOut = fileSize;
    return OsSuccess;
}

void
UnloadFile(
        _In_
        MString_t *FullPath,
        _In_
        void *Buffer)
{
    // So right now we will simply free the buffer, 
    // but when we implement caching we will check if it should stay cached
    _CRT_UNUSED(FullPath);
    free(Buffer);
}

OsStatus_t
InitializeProcessManager(void)
{
    hashtable_construct(&g_processHistory, HASHTABLE_MINIMUM_CAPACITY,
                        sizeof(struct process_history_entry), ProcessHistoryHash,
                        ProcessHistoryCmp);
    CreateEventQueue(&g_eventQueue);
    DebuggerInitialize();
    return OsSuccess;
}

OsStatus_t
CreateProcess(
        _In_  const char*             path,
        _In_  const char*             arguments,
        _In_  void*                   inheritationBlock,
        _In_  ProcessConfiguration_t* configuration,
        _Out_ UUId_t*                 handleOut)
{
    ThreadParameters_t threadParameters;
    Process_t*         process;
    MString_t*         pathAsMString;
    size_t             pathLength;
    size_t             argumentsLength = 0;
    size_t             inheritationBlockLength;
    char*              argumentsPointer;
    int                index;
    UUId_t             handle;
    OsStatus_t         osStatus;

    assert(path != NULL);
    assert(handleOut != NULL);
    TRACE("[process] [spawn] path %s, args %s", path, arguments);

    // check for null or empty
    if (arguments && strlen(arguments) > 0) {
        // include zero terminator
        argumentsLength = strlen(arguments) + 1;
    }

    process = (Process_t *)malloc(sizeof(Process_t));
    if (!process) {
        return OsOutOfMemory;
    }
    memset(process, 0, sizeof(Process_t));

    osStatus = handle_create(&handle);
    if (osStatus != OsSuccess) {
        free(process);
        return osStatus;
    }

    ELEMENT_INIT(&process->Header, (uintptr_t) handle, process);
    process->State      = ATOMIC_VAR_INIT(PROCESS_RUNNING);
    process->References = ATOMIC_VAR_INIT(1);
    process->StartedAt  = clock();
    spinlock_init(&process->SyncObject, spinlock_recursive);

    // Load the executable
    pathAsMString = MStringCreate((void *) path, StrUTF8);
    osStatus      = PeLoadImage(UUID_INVALID, NULL, pathAsMString, &process->Executable);
    MStringDestroy(pathAsMString);
    if (osStatus != OsSuccess) {
        ERROR(" > failed to load executable");
        free(process);
        return osStatus;
    }

    // it won't fail, since -1 + 1 = 0, so we just copy the entire string
    TRACE("[process] [spawn] full path %s", MStringRaw(process->Executable->FullPath));
    process->Path = MStringCreate((void *) MStringRaw(process->Executable->FullPath), StrUTF8);
    index = MStringFindReverse(process->Path, '/', 0);
    process->Name              = MStringSubString(process->Path, index + 1, -1);
    process->WorkingDirectory  = MStringSubString(process->Path, 0, index);
    process->AssemblyDirectory = MStringSubString(process->Path, 0, index);

    // Store copies of startup information
    memcpy(&process->Configuration, configuration, sizeof(ProcessConfiguration_t));

    // handle arguments, we need to prepend the full path of the executable
    pathLength       = strlen(MStringRaw(process->Path));
    argumentsPointer = malloc(pathLength + 1 + argumentsLength);
    if (!argumentsPointer) {
        MStringDestroy(process->Path);
        MStringDestroy(process->Name);
        MStringDestroy(process->WorkingDirectory);
        MStringDestroy(process->AssemblyDirectory);
        free(process);
        return OsOutOfMemory;
    }

    // Build the argument string, remember to null terminate.
    memcpy(&argumentsPointer[0], (const void *) MStringRaw(process->Path), pathLength);
    if (argumentsLength != 0) {
        argumentsPointer[pathLength] = ' ';

        // argumentsLength includes zero termination, so no need to set explict
        memcpy(&argumentsPointer[pathLength + 1], (void *) arguments, argumentsLength);
    }
    else {
        argumentsPointer[pathLength] = '\0';
    }

    process->Arguments       = (const char *) argumentsPointer;
    process->ArgumentsLength = pathLength + 1 + argumentsLength;

    if (inheritationBlock != NULL) {
        stdio_inheritation_block_t *block = inheritationBlock;

        inheritationBlockLength = sizeof(stdio_inheritation_block_t) +
                                  (block->handle_count * sizeof(struct stdio_handle));
        process->InheritationBlock = malloc(inheritationBlockLength);
        if (!process->InheritationBlock) {
            // todo
        }

        process->InheritationBlockLength = inheritationBlockLength;
        memcpy(process->InheritationBlock, inheritationBlock, inheritationBlockLength);
    }

    // Initialize threading paramaters for the new thread
    InitializeThreadParameters(&threadParameters);
    threadParameters.Name              = MStringRaw(process->Name);
    threadParameters.MemorySpaceHandle = (UUId_t)(uintptr_t)process->Executable->MemorySpace;

    osStatus = Syscall_ThreadCreate(
            process->Executable->EntryAddress,
            NULL, // Argument
            &threadParameters,
            &process->PrimaryThreadId);
    if (osStatus == OsSuccess) {
        osStatus = Syscall_ThreadDetach(process->PrimaryThreadId);
    }
    list_append(&g_processes, &process->Header);
    *handleOut = handle;
    return osStatus;
}

void svc_process_spawn_callback(struct gracht_recv_message *message,
                                struct svc_process_spawn_args *args)
{
    UUId_t     handle;
    OsStatus_t status = CreateProcess(args->path, args->arguments,
                                      args->inheritation_block, args->configuration, &handle);
    svc_process_spawn_response(message, status, handle);
}

void svc_process_get_startup_information_callback(struct gracht_recv_message *message,
                                                  struct svc_process_get_startup_information_args *args)
{
    Process_t* process            = GetProcessByPrimaryThread(args->process_handle);
    OsStatus_t status             = OsDoesNotExist;
    UUId_t     handle             = UUID_INVALID;
    size_t     argumentLength     = 0;
    size_t     inheritationLength = 0;
    int        moduleCount        = PROCESS_MAXMODULES;

    if (process) {
        struct dma_attachment dmaAttachment;
        status = dma_attach(args->dmabuf_handle, &dmaAttachment);
        if (status == OsSuccess) {
            status = dma_attachment_map(&dmaAttachment, DMA_ACCESS_WRITE);
            if (status == OsSuccess) {
                char* buffer = dmaAttachment.buffer;

                handle             = (UUId_t) (uintptr_t) process->Header.key;
                argumentLength     = process->ArgumentsLength;
                inheritationLength = process->InheritationBlockLength;

                memcpy(&buffer[0], process->Arguments, argumentLength);
                if (inheritationLength) {
                    memcpy(&buffer[argumentLength], process->InheritationBlock, inheritationLength);
                }

                status = PeGetModuleEntryPoints(
                        process->Executable,
                        (Handle_t *)
                                &buffer[argumentLength + inheritationLength],
                        &moduleCount);
                dma_attachment_unmap(&dmaAttachment);
            }
            dma_detach(&dmaAttachment);
        }
    }

    svc_process_get_startup_information_response(
            message,
            status,
            handle,
            argumentLength,
            inheritationLength,
            moduleCount * sizeof(Handle_t));
}

OsStatus_t
JoinProcess(
        _In_ Process_t*                  process,
        _In_ struct gracht_recv_message* message,
        _In_ size_t                      timeout)
{
    size_t           waitContextSize = sizeof(ProcessJoiner_t) + VALI_MSG_DEFER_SIZE(message);
    ProcessJoiner_t* waitContext;

    if (!process) {
        return OsInvalidParameters;
    }

    waitContext = (ProcessJoiner_t*)malloc(waitContextSize);
    if (!waitContext) {
        return OsOutOfMemory;
    }

    memset(waitContext, 0, waitContextSize);
    ELEMENT_INIT(&waitContext->Header, process->Header.key, waitContext);
    gracht_vali_message_defer_response(&waitContext->DeferredResponse, message);
    waitContext->Process = process;
    if (timeout != 0) {
        waitContext->EventHandle = QueueDelayedEvent(g_eventQueue, HandleJoinProcess, waitContext, timeout);
    }
    else {
        waitContext->EventHandle = UUID_INVALID;
    }

    list_append(&g_joiners, &waitContext->Header);
    return OsSuccess;
}

void svc_process_join_callback(struct gracht_recv_message*   message,
                               struct svc_process_join_args* args)
{
    Process_t* process = AcquireProcess(args->handle);
    OsStatus_t status;

    // If the process is no longer alive, then we look in our archive for a history lesson
    // on that process
    if (!process) {
        struct process_history_entry* entry;

        entry = hashtable_get(&g_processHistory, &(struct process_history_entry) {
            .process_id = args->handle
        });
        if (entry) {
            svc_process_join_response(message, OsSuccess, entry->exit_code);
        }

        // otherwise fall through and let normal error handling code take-over
    }

    // Only respond directly if the join didn't attach
    status = JoinProcess(process, message, args->timeout);
    if (status != OsSuccess) {
        ReleaseProcess(process);
        svc_process_join_response(message, status, 0);
    }
}

static int
WakeupAllWaiters(
        _In_ int        index,
        _In_ element_t* element,
        _In_ void*      context)
{
    Process_t*       process     = context;
    ProcessJoiner_t* waitContext = element->value;
    if (process->Header.key != element->key) {
        return LIST_ENUMERATE_CONTINUE;
    }

    if (waitContext->EventHandle != UUID_INVALID) {
        if (CancelEvent(g_eventQueue, waitContext->EventHandle) == OsSuccess) {
            HandleJoinProcess((void*)waitContext);
        }
    }
    else {
        HandleJoinProcess((void*)waitContext);
    }
    return LIST_ENUMERATE_REMOVE;
}

OsStatus_t
TerminateProcess(
        _In_ Process_t* process,
        _In_ int        exitCode)
{
    UUId_t processId = (UUId_t)(uintptr_t)process->Header.key;
    WARNING("[terminate_process] process %u exitted with code: %i", processId, exitCode);

    process->State    = PROCESS_TERMINATING;
    process->ExitCode = exitCode;

    // Add a new entry in our history archive and then wake up all waiters
    hashtable_set(&g_processHistory, &(struct process_history_entry) {
        .process_id = processId,
        .exit_code = exitCode
    });
    list_enumerate(&g_joiners, WakeupAllWaiters, process);
    return OsSuccess;
}

void svc_process_terminate_callback(struct gracht_recv_message*        message,
                                    struct svc_process_terminate_args* args)
{
    Process_t* process = AcquireProcess(args->handle);
    OsStatus_t status  = TerminateProcess(process, args->exit_code);
    if (process) {
        ReleaseProcess(process);
    }
    svc_process_terminate_response(message, status);
}

OsStatus_t
KillProcess(
        _In_ Process_t* killer,
        _In_ Process_t* victim)
{
    // Verify permissions
    TRACE("KillProcess(%u, %u)", (UUId_t) (uintptr_t) killer->Header.key,
          (UUId_t) (uintptr_t) victim->Header.key);

    if (!killer) {
        return OsInvalidParameters;
    }

    if (!victim) {
        return OsDoesNotExist;
    }

    // Send a kill signal on the primary thread, if it fails, then
    // the thread has probably already shutdown, but the process instance is
    // lingering around.
    if (Syscall_ThreadSignal(victim->PrimaryThreadId, SIGKILL) != OsSuccess) {
        return TerminateProcess(victim, -1);
    }
    return OsSuccess;
}

void svc_process_kill_callback(struct gracht_recv_message*   message,
                               struct svc_process_kill_args* args)
{
    Process_t* process = AcquireProcess(args->handle);
    Process_t* target  = AcquireProcess(args->target_handle);
    OsStatus_t status  = KillProcess(process, target);

    if (process) {
        ReleaseProcess(process);
    }

    if (target) {
        ReleaseProcess(target);
    }

    svc_process_kill_response(message, status);
}

OsStatus_t
LoadProcessLibrary(
        _In_  Process_t*  process,
        _In_  const char* path,
        _Out_ Handle_t*   handleOut)
{
    MString_t* pathAsMString;
    OsStatus_t osStatus;

    TRACE("LoadProcessLibrary(%u, %s)", (UUId_t) (uintptr_t) process->Header.key,
          (path == NULL) ? "Global" : path);
    if (path == NULL) {
        *handleOut = HANDLE_GLOBAL;
        return OsSuccess;
    }

    // Create the neccessary strings
    pathAsMString = MStringCreate((void *) path, StrUTF8);
    osStatus = PeLoadImage((UUId_t) (uintptr_t) process->Header.key,
                           process->Executable, pathAsMString, (PeExecutable_t **) handleOut);
    MStringDestroy(pathAsMString);
    return osStatus;
}

void svc_library_load_callback(struct gracht_recv_message*   message,
                               struct svc_library_load_args* args)
{
    Process_t* process = AcquireProcess(args->process_handle);
    OsStatus_t status   = OsInvalidParameters;
    Handle_t   handle   = HANDLE_INVALID;

    if (process) {
        status = LoadProcessLibrary(process, args->path, &handle);
        ReleaseProcess(process);
    }
    svc_library_load_response(message, status, handle);
}

uintptr_t
ResolveProcessLibraryFunction(
        _In_
        Process_t *Process,
        _In_
        Handle_t Handle,
        _In_
        const char *Function)
{
    PeExecutable_t *Image = Process->Executable;
    TRACE("ResolveProcessLibraryFunction(%u, %s)",
          (UUId_t) (uintptr_t) Process->Header.key, Function);
    if (Handle != HANDLE_GLOBAL) {
        Image = (PeExecutable_t *) Handle;
    }
    return PeResolveFunction(Image, Function);
}

void svc_library_get_function_callback(struct gracht_recv_message *message,
                                       struct svc_library_get_function_args *args)
{
    Process_t  *process = AcquireProcess(args->process_handle);
    OsStatus_t status   = OsInvalidParameters;
    uintptr_t  address  = 0;

    if (process) {
        address = ResolveProcessLibraryFunction(process, args->handle, args->function);
        status  = OsSuccess;
        ReleaseProcess(process);
    }
    svc_library_get_function_response(message, status, address);
}

OsStatus_t
UnloadProcessLibrary(
        _In_ Process_t* process,
        _In_ Handle_t   handle)
{
    TRACE("UnloadProcessLibrary(%u)", (UUId_t) (uintptr_t) process->Header.key);
    if (handle == HANDLE_GLOBAL) {
        return OsSuccess;
    }
    return PeUnloadLibrary(process->Executable, (PeExecutable_t *) handle);
}

void svc_library_unload_callback(struct gracht_recv_message *message,
                                 struct svc_library_unload_args *args)
{
    Process_t* process = AcquireProcess(args->process_handle);
    OsStatus_t status   = OsInvalidParameters;

    if (process) {
        status = UnloadProcessLibrary(process, args->handle);
        ReleaseProcess(process);
    }
    svc_library_unload_response(message, status);
}

void svc_process_get_modules_callback(struct gracht_recv_message *message,
                                      struct svc_process_get_modules_args *args)
{
    Process_t* process    = AcquireProcess(args->handle);
    int        moduleCount = PROCESS_MAXMODULES;
    Handle_t   buffer[PROCESS_MAXMODULES];

    if (process) {
        PeGetModuleHandles(process->Executable, &buffer[0], &moduleCount);
        ReleaseProcess(process);
    }
    svc_process_get_modules_response(message, &buffer[0], moduleCount * sizeof(Handle_t), moduleCount);
}

void
svc_process_get_tick_base_callback(struct gracht_recv_message *message,
                                   struct svc_process_get_tick_base_args *args)
{
    Process_t       *process = AcquireProcess(args->handle);
    OsStatus_t      status   = OsInvalidParameters;
    LargeUInteger_t tick;
    if (process) {
        tick.QuadPart = clock() - process->StartedAt;
        status = OsSuccess;
        ReleaseProcess(process);
    }
    svc_process_get_tick_base_response(message, status, tick.u.LowPart, tick.u.HighPart);
}

void svc_process_get_assembly_directory_callback(struct gracht_recv_message *message,
                                                 struct svc_process_get_assembly_directory_args *args)
{
    Process_t  *process = AcquireProcess(args->handle);
    OsStatus_t status   = OsInvalidParameters;
    const char *path    = NULL;
    if (process) {
        path   = MStringRaw(process->AssemblyDirectory);
        status = OsSuccess;
        ReleaseProcess(process);
    }
    svc_process_get_assembly_directory_response(message, status, path);
}

void svc_process_get_working_directory_callback(struct gracht_recv_message *message,
                                                struct svc_process_get_working_directory_args *args)
{
    Process_t*  process = AcquireProcess(args->handle);
    OsStatus_t  status  = OsInvalidParameters;
    const char* path    = NULL;
    if (process) {
        path   = MStringRaw(process->WorkingDirectory);
        status = OsSuccess;
        ReleaseProcess(process);

        TRACE("[get_cwd] %s", path);
    }
    svc_process_get_working_directory_response(message, status, path);
}

void svc_process_set_working_directory_callback(struct gracht_recv_message *message,
                                                struct svc_process_set_working_directory_args *args)
{
    Process_t* process = AcquireProcess(args->handle);
    OsStatus_t status   = OsInvalidParameters;
    if (process) {
        if (args->path != NULL) {
            TRACE("[set_cwd] %s", args->path);
            MStringDestroy(process->WorkingDirectory);
            process->WorkingDirectory = MStringCreate((void *) args->path, StrUTF8);
            status = OsSuccess;
        }
        ReleaseProcess(process);
    }
    svc_process_set_working_directory_response(message, status);
}

void svc_process_get_name_callback(struct gracht_recv_message *message,
                                   struct svc_process_get_name_args *args)
{
    Process_t  *process = AcquireProcess(args->handle);
    OsStatus_t status   = OsInvalidParameters;
    const char *name    = NULL;
    if (process) {
        name   = MStringRaw(process->Name);
        status = OsSuccess;
        ReleaseProcess(process);
    }
    svc_process_get_name_response(message, status, name);
}

Process_t *
GetProcessByPrimaryThread(
        _In_
        UUId_t ThreadId)
{
    foreach(Node, &g_processes) {
        Process_t *process = (Process_t *) Node;
        if (process->PrimaryThreadId == ThreadId) {
            return process;
        }
    }
    return NULL;
}

static uint64_t ProcessHistoryHash(const void* element)
{
    const struct process_history_entry* entry = element;
    return entry->process_id; // already unique identifier
}

static int ProcessHistoryCmp(const void* element1, const void* element2)
{
    const struct process_history_entry* lh = element1;
    const struct process_history_entry* rh = element2;

    // return 0 on true, 1 on false
    return lh->process_id == rh->process_id ? 0 : 1;
}
