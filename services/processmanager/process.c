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
#include <ddk/eventqueue.h>
#include <ddk/handle.h>
#include <ddk/utils.h>
#include <internal/_syscalls.h> // for Syscall_ThreadCreate
#include <internal/_io.h>
#include <internal/_ipc.h>
#include "../../librt/libds/pe/pe.h"
#include <os/mollenos.h>
#include <os/dmabuf.h>
#include <os/context.h>
#include "process.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>

#include "svc_library_protocol_server.h"
#include "svc_process_protocol_server.h"

static list_t        Processes  = LIST_INIT;
static list_t        Joiners    = LIST_INIT;
static EventQueue_t* EventQueue = NULL;

static OsStatus_t
DestroyProcess(
    _In_ Process_t* Process)
{
    int References = atomic_fetch_sub(&Process->References, 1);
    if (References == 1) {
        UUId_t Handle = (UUId_t)(uintptr_t)Process->Header.key;
        list_remove(&Processes, &Process->Header);
        if (Process->Name != NULL) {
            MStringDestroy(Process->Name);
        }
        if (Process->Path != NULL) {
            MStringDestroy(Process->Path);
        }
        if (Process->WorkingDirectory != NULL) {
            MStringDestroy(Process->WorkingDirectory);
        }
        if (Process->AssemblyDirectory != NULL) {
            MStringDestroy(Process->AssemblyDirectory);
        }
        if (Process->Executable != NULL) {
            PeUnloadImage(Process->Executable);
        }
        handle_destroy(Handle);
        free(Process);
        return OsSuccess;
    }
    return OsError;
}

void
ReleaseProcess(
    _In_ Process_t* Process)
{
    if (DestroyProcess(Process) != OsSuccess) {
        spinlock_release(&Process->SyncObject);
    } 
}

Process_t*
AcquireProcess(
    _In_ UUId_t Handle)
{
    Process_t* Process = (Process_t*)list_find_value(&Processes, (void*)(uintptr_t)Handle);
    if (Process != NULL) {
        int References;
        while (1) {
            References = atomic_load(&Process->References);
            if (References == 0) {
                break;
            }
            if (atomic_compare_exchange_weak(&Process->References, &References, References + 1)) {
                break;
            }
        }

        if (References > 0) {
            spinlock_acquire(&Process->SyncObject);
            if (Process->State == PROCESS_RUNNING) {
                return Process;
            }
            ReleaseProcess(Process);
        }
    }
    return NULL;
}

static void
HandleJoinProcess(
    _In_ void* Context)
{
    ProcessJoiner_t* join   = (ProcessJoiner_t*)Context;
    OsStatus_t       status = OsTimeout;
    int              exitCode = 0;

    // Notify application about this
    if (join->Process->State == PROCESS_TERMINATING) {
        status   = OsSuccess;
        exitCode = join->Process->ExitCode;
    }
    
    svc_process_join_response(&join->DeferredResponse.recv_message, status, exitCode);
    DestroyProcess(join->Process);
    free(join);
}

static OsStatus_t
TestFilePath(
    _In_ MString_t* Path)
{
    OsFileDescriptor_t FileStats;
    if (GetFileInformationFromPath((const char*)MStringRaw(Path), &FileStats) != FsOk) {
        return OsError;
    }
    return OsSuccess;
}

static OsStatus_t
GuessBasePath(
    _In_  UUId_t      ProcessId,
    _In_  MString_t*  Path,
    _Out_ MString_t** FullPathOut)
{
    // Check the working directory, if it fails iterate the environment defaults
    Process_t* Process = AcquireProcess(ProcessId);
    MString_t* Result;
    int        IsApp;
    int        IsDll;

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
    _In_  UUId_t      ProcessId,
    _In_  MString_t*  Path,
    _Out_ MString_t** FullPathOut)
{
    OsStatus_t Status          = OsSuccess;
    MString_t* TemporaryResult = Path;
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
            char* CanonicalizedPath = (char*)malloc(_MAXPATH);
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
    FILE*  file;
    long   fileSize;
    void*  fileBuffer;
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
    _In_ MString_t* FullPath,
    _In_ void*      Buffer)
{
    // So right now we will simply free the buffer, 
    // but when we implement caching we will check if it should stay cached
    _CRT_UNUSED(FullPath);
    free(Buffer);
}

OsStatus_t
InitializeProcessManager(void)
{
    CreateEventQueue(&EventQueue);
    return OsSuccess;
}

OsStatus_t
CreateProcess(
    _In_  const char*             Path,
    _In_  const char*             Arguments,
    _In_  void*                   InheritationBlock,
    _In_  ProcessConfiguration_t* Configuration,
    _Out_ UUId_t*                 HandleOut)
{
    ThreadParameters_t Paramaters;
    Process_t*         Process;
    MString_t*         PathAsMString;
    size_t             PathLength;
    size_t             ArgumentsLength;
    size_t             InheritationBlockLength;
    char*              ArgumentsPointer;
    int                Index;
    UUId_t             Handle;
    OsStatus_t         Status;

    assert(Path != NULL);
    assert(HandleOut != NULL);
    TRACE("[process] [spawn] path %s, args %s", Path, Arguments);
    
    ArgumentsLength = Arguments ? strlen(Arguments) : 0;

    Process = (Process_t*)malloc(sizeof(Process_t));
    if (!Process) {
        return OsOutOfMemory;
    }
    memset(Process, 0, sizeof(Process_t));

    Status = handle_create(&Handle);
    if (Status != OsSuccess) {
        free(Process);
        return Status;
    }
    
    ELEMENT_INIT(&Process->Header, (uintptr_t)Handle, Process);
    Process->State      = ATOMIC_VAR_INIT(PROCESS_RUNNING);
    Process->References = ATOMIC_VAR_INIT(1);
    Process->StartedAt  = clock();
    spinlock_init(&Process->SyncObject, spinlock_recursive);

    // Load the executable
    PathAsMString = MStringCreate((void*)Path, StrUTF8);
    Status        = PeLoadImage(UUID_INVALID, NULL, PathAsMString, &Process->Executable);
    MStringDestroy(PathAsMString);
    if (Status != OsSuccess) {
        ERROR(" > failed to load executable");
        free(Process);
        return Status;
    }

    // it won't fail, since -1 + 1 = 0, so we just copy the entire string
    TRACE("[process] [spawn] full path %s", MStringRaw(Process->Executable->FullPath));
    Process->Path              = MStringCreate((void*)MStringRaw(Process->Executable->FullPath), StrUTF8);
    Index                      = MStringFindReverse(Process->Path, '/', 0);
    Process->Name              = MStringSubString(Process->Path, Index + 1, -1);
    Process->WorkingDirectory  = MStringSubString(Process->Path, 0, Index);
    Process->AssemblyDirectory = MStringSubString(Process->Path, 0, Index);

    // Store copies of startup information
    memcpy(&Process->Configuration, Configuration, sizeof(ProcessConfiguration_t));

    // Handle arguments, we need to prepend the full path of the executable
    PathLength       = strlen(MStringRaw(Process->Path));
    ArgumentsPointer = malloc(PathLength + 1 + ArgumentsLength);
    if (!ArgumentsPointer) {
        MStringDestroy(Process->Path);
        MStringDestroy(Process->Name);
        MStringDestroy(Process->WorkingDirectory);
        MStringDestroy(Process->AssemblyDirectory);
        free(Process);
        return OsOutOfMemory;
    }
    
    memcpy(&ArgumentsPointer[0], (const void*)MStringRaw(Process->Path), PathLength);
    ArgumentsPointer[PathLength] = ' ';
    if (Arguments != NULL && ArgumentsLength != 0) {
        memcpy(&ArgumentsPointer[PathLength + 1], (void*)Arguments, ArgumentsLength);
    }
    Process->Arguments       = (const char*)ArgumentsPointer;
    Process->ArgumentsLength = PathLength + 1 + ArgumentsLength;

    if (InheritationBlock != NULL) {
        stdio_inheritation_block_t* block = InheritationBlock;
        
        InheritationBlockLength          = sizeof(stdio_inheritation_block_t) + (block->handle_count * sizeof(struct stdio_handle));
        Process->InheritationBlock       = malloc(InheritationBlockLength);
        Process->InheritationBlockLength = InheritationBlockLength;
        memcpy(Process->InheritationBlock, InheritationBlock, InheritationBlockLength);
    }

    // Initialize threading paramaters for the new thread
    InitializeThreadParameters(&Paramaters);
    Paramaters.Name              = MStringRaw(Process->Name);
    Paramaters.MemorySpaceHandle = (UUId_t)Process->Executable->MemorySpace;
    
    Status = Syscall_ThreadCreate(Process->Executable->EntryAddress, NULL, &Paramaters, &Process->PrimaryThreadId);
    if (Status == OsSuccess) {
        Status = Syscall_ThreadDetach(Process->PrimaryThreadId);
    }
    list_append(&Processes, &Process->Header);
    *HandleOut = Handle;
    return Status;
}

void svc_process_spawn_callback(struct gracht_recv_message* message, struct svc_process_spawn_args* args)
{
    UUId_t     handle;
    OsStatus_t status = CreateProcess(args->path, args->arguments,
        args->inheritation_block, args->configuration, &handle);
    svc_process_spawn_response(message, status, handle);
}

void svc_process_get_startup_information_callback(struct gracht_recv_message* message,
    struct svc_process_get_startup_information_args* args)
{
    Process_t* process            = GetProcessByPrimaryThread(args->handle);
    OsStatus_t status             = OsDoesNotExist;
    char*      buffer             = NULL;
    size_t     bufferLength       = 0;
    UUId_t     handle             = UUID_INVALID;
    size_t     argumentLength     = 0;
    size_t     inheritationLength = 0;
    int        moduleCount        = PROCESS_MAXMODULES;
    
    if (process) {
        buffer = malloc(process->ArgumentsLength + process->InheritationBlockLength + (PROCESS_MAXMODULES * sizeof(Handle_t)));
        if (!buffer) {
            status = OsOutOfMemory;
        }
        else {
            handle             = (UUId_t)(uintptr_t)process->Header.key;
            argumentLength     = process->ArgumentsLength;
            inheritationLength = process->InheritationBlockLength;
            
            memcpy(&buffer[0], process->Arguments, process->ArgumentsLength);
            memcpy(&buffer[process->ArgumentsLength], process->Arguments, process->InheritationBlockLength);
            status = PeGetModuleEntryPoints(process->Executable,
                (Handle_t*)&buffer[process->ArgumentsLength + process->InheritationBlockLength],
                &moduleCount);
            bufferLength = process->ArgumentsLength + process->InheritationBlockLength + (moduleCount * sizeof(Handle_t));
        }
    }
    
    svc_process_get_startup_information_response(message, status, handle, argumentLength, inheritationLength,
        moduleCount * sizeof(Handle_t), buffer, bufferLength);
    if (buffer) {
        free(buffer);
    }
}

OsStatus_t
JoinProcess(
    _In_  Process_t*                  process,
    _In_  struct gracht_recv_message* message,
    _In_  size_t                      timeout)
{
    ProcessJoiner_t* Join = (ProcessJoiner_t*)malloc(sizeof(ProcessJoiner_t) + VALI_MSG_DEFER_SIZE(message));
    if (!Join) {
        return OsOutOfMemory;
    }
    
    memset(Join, 0, sizeof(ProcessJoiner_t));
    TRACE("JoinProcess(%u, %u)", (UUId_t)(uintptr_t)process->Header.key, timeout);
    
    ELEMENT_INIT(&Join->Header, process->Header.key, Join);
    gracht_vali_message_defer_response(&Join->DeferredResponse, message);
    Join->Process = process;
    if (timeout != 0) {
        Join->EventHandle = QueueDelayedEvent(EventQueue, HandleJoinProcess, Join, timeout);
    }
    else {
        Join->EventHandle = UUID_INVALID;
    }
    
    list_append(&Joiners, &Join->Header);
    return OsSuccess;
}

void svc_process_join_callback(struct gracht_recv_message* message, struct svc_process_join_args* args)
{
    Process_t* process = AcquireProcess(args->handle);
    OsStatus_t status  = JoinProcess(process, message, args->timeout);
    
    // Only respond directly if the join didn't attach
    if (status != OsSuccess) {
        if (process) {
            ReleaseProcess(process);
        }
        svc_process_join_response(message, status, 0);
    }
}

static int
WakeupAllWaiters(
    _In_ int        Index,
    _In_ element_t* Element,
    _In_ void*      Context)
{
    Process_t*       Process = Context;
    ProcessJoiner_t* Join    = Element->value;
    if (Process->Header.key != Element->key) {
        return LIST_ENUMERATE_CONTINUE;
    }
    
    if (Join->EventHandle != UUID_INVALID) {
        if (CancelEvent(EventQueue, Join->EventHandle) == OsSuccess) {
            HandleJoinProcess((void*)Join);
        }
    }
    else {
        HandleJoinProcess((void*)Join);
    }
    return LIST_ENUMERATE_REMOVE;
}

OsStatus_t
TerminateProcess(
    _In_ Process_t* Process,
    _In_ int        ExitCode)
{
    WARNING("[terminate_process] process %u exitted with code: %i", Process->Header.key, ExitCode);
    
    Process->State    = PROCESS_TERMINATING;
    Process->ExitCode = ExitCode;
    list_enumerate(&Joiners, WakeupAllWaiters, Process);
    return OsSuccess;
}

void svc_process_terminate_callback(struct gracht_recv_message* message, struct svc_process_terminate_args* args)
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
    _In_ Process_t* Killer,
    _In_ Process_t* Target)
{
    // Verify permissions
    TRACE("KillProcess(%u, %u)", (UUId_t)(uintptr_t)Killer->Header.key, 
        (UUId_t)(uintptr_t)Target->Header.key);
        
    if (!Killer) {
        return OsInvalidParameters;
    }
    
    if (!Target) {
        return OsDoesNotExist;
    }

    // Send a kill signal on the primary thread, if it fails, then
    // the thread has probably already shutdown, but the process instance is
    // lingering around.
    if (Syscall_ThreadSignal(Target->PrimaryThreadId, SIGKILL) != OsSuccess) {
        return TerminateProcess(Target, -1);
    }
    return OsSuccess;
}

void svc_process_kill_callback(struct gracht_recv_message* message, struct svc_process_kill_args* args)
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
    _In_  Process_t*  Process,
    _In_  const char* Path,
    _Out_ Handle_t*   HandleOut)
{
    MString_t* PathAsMString;
    OsStatus_t Status;

    TRACE("LoadProcessLibrary(%u, %s)", (UUId_t)(uintptr_t)Process->Header.key, 
        (Path == NULL) ? "Global" : Path);
    if (Path == NULL) {
        *HandleOut = HANDLE_GLOBAL;
        return OsSuccess;
    }

    // Create the neccessary strings
    PathAsMString = MStringCreate((void*)Path, StrUTF8);
    Status        = PeLoadImage((UUId_t)(uintptr_t)Process->Header.key, 
        Process->Executable, PathAsMString, (PeExecutable_t**)HandleOut);
    MStringDestroy(PathAsMString);
    return Status;
}

void svc_library_load_callback(struct gracht_recv_message* message, struct svc_library_load_args* args)
{
    Process_t* process = AcquireProcess(args->process_handle);
    OsStatus_t status  = OsInvalidParameters;
    Handle_t   handle  = HANDLE_INVALID;
    
    if (process) {
        status = LoadProcessLibrary(process, args->path, &handle);
        ReleaseProcess(process);
    }
    svc_library_load_response(message, status, handle);
}

uintptr_t
ResolveProcessLibraryFunction(
    _In_ Process_t*  Process,
    _In_ Handle_t    Handle,
    _In_ const char* Function)
{
    PeExecutable_t* Image = Process->Executable;
    TRACE("ResolveProcessLibraryFunction(%u, %s)", 
        (UUId_t)(uintptr_t)Process->Header.key, Function);
    if (Handle != HANDLE_GLOBAL) {
        Image = (PeExecutable_t*)Handle;
    }
    return PeResolveFunction(Image, Function);
}

void svc_library_get_function_callback(struct gracht_recv_message* message, struct svc_library_get_function_args* args)
{
    Process_t* process = AcquireProcess(args->process_handle);
    OsStatus_t status  = OsInvalidParameters;
    uintptr_t  address = 0;
    
    if (process) {
        address = ResolveProcessLibraryFunction(process, args->handle, args->function);
        status  = OsSuccess;
        ReleaseProcess(process);
    }
    svc_library_get_function_response(message, status, address);
}

OsStatus_t
UnloadProcessLibrary(
    _In_ Process_t* Process,
    _In_ Handle_t   Handle)
{
    TRACE("UnloadProcessLibrary(%u)", (UUId_t)(uintptr_t)Process->Header.key);
    if (Handle == HANDLE_GLOBAL) {
        return OsSuccess;
    }
    return PeUnloadLibrary(Process->Executable, (PeExecutable_t*)Handle);
}

void svc_library_unload_callback(struct gracht_recv_message* message, struct svc_library_unload_args* args)
{
    Process_t* process = AcquireProcess(args->process_handle);
    OsStatus_t status  = OsInvalidParameters;
    
    if (process) {
        status = UnloadProcessLibrary(process, args->handle);
        ReleaseProcess(process);
    }
    svc_library_unload_response(message, status);
}

void svc_process_get_modules_callback(struct gracht_recv_message* message, struct svc_process_get_modules_args* args)
{
    Process_t* process     = AcquireProcess(args->handle);
    OsStatus_t status      = OsInvalidParameters;
    int        moduleCount = PROCESS_MAXMODULES;
    Handle_t   buffer[PROCESS_MAXMODULES];
    
    if (process) {
        status = PeGetModuleHandles(process->Executable, &buffer[0], &moduleCount);
        ReleaseProcess(process);
    }
    svc_process_get_modules_response(message, &buffer[0], moduleCount * sizeof(Handle_t), moduleCount);
}

OsStatus_t
HandleProcessCrashReport(
    _In_ Process_t* Process,
    _In_ Context_t* CrashContext,
    _In_ int        CrashReason)
{
    uintptr_t  ImageBase;
    MString_t* ImageName;
    uintptr_t  CrashAddress;
    
    if (!Process || !CrashContext) {
        return OsInvalidParameters;
    }
    
    ImageBase    = Process->Executable->VirtualAddress;
    ImageName    = Process->Executable->Name;
    CrashAddress = CONTEXT_IP(CrashContext);
    
    TRACE("HandleProcessCrashReport(%i)", CrashReason);
    // Was it not main executable?
    if (CrashAddress > (Process->Executable->CodeBase + Process->Executable->CodeSize)) {
        // Iterate libraries to find the sinner
        if (Process->Executable->Libraries != NULL) {
            foreach(i, Process->Executable->Libraries) {
                PeExecutable_t* Library = (PeExecutable_t*)i->value;
                if (CrashAddress >= Library->CodeBase && 
                    CrashAddress < (Library->CodeBase + Library->CodeSize)) {
                    ImageName = Library->Name;
                    ImageBase = Library->VirtualAddress;
                    break;
                }
            }
        }
    }

    // Debug
    ERROR("%s: Crashed in module %s, at offset 0x%" PRIxIN " (0x%" PRIxIN ") with reason %i",
        MStringRaw(Process->Executable->Name), MStringRaw(ImageName),
        CrashAddress - ImageBase, CrashAddress, CrashReason);
    return OsSuccess;
}

void svc_process_report_crash_callback(struct gracht_recv_message* message, struct svc_process_report_crash_args* args)
{
    Process_t* process = AcquireProcess(args->handle);
    OsStatus_t status  = HandleProcessCrashReport(process, args->crash_context, args->reason);
    if (process) {
        ReleaseProcess(process);
    }
    svc_process_report_crash_response(message, status);
}

void svc_process_get_tick_base_callback(struct gracht_recv_message* message, struct svc_process_get_tick_base_args* args)
{
    Process_t*      process = AcquireProcess(args->handle);
    OsStatus_t      status  = OsInvalidParameters;
    LargeUInteger_t tick;
    if (process) {
        tick.QuadPart = clock() - process->StartedAt;
        status        = OsSuccess;
        ReleaseProcess(process);
    }
    svc_process_get_tick_base_response(message, status, tick.u.LowPart, tick.u.HighPart);
}

void svc_process_get_assembly_directory_callback(struct gracht_recv_message* message,
    struct svc_process_get_assembly_directory_args* args)
{
    Process_t*  process = AcquireProcess(args->handle);
    OsStatus_t  status  = OsInvalidParameters;
    const char* path    = NULL;
    if (process) {
        path   = MStringRaw(process->AssemblyDirectory);
        status = OsSuccess;
        ReleaseProcess(process);
    }
    svc_process_get_assembly_directory_response(message, status, path);
}

void svc_process_get_working_directory_callback(struct gracht_recv_message* message,
    struct svc_process_get_working_directory_args* args)
{
    Process_t*  process = AcquireProcess(args->handle);
    OsStatus_t  status  = OsInvalidParameters;
    const char* path    = NULL;
    if (process) {
        path   = MStringRaw(process->WorkingDirectory);
        status = OsSuccess;
        ReleaseProcess(process);
    }
    svc_process_get_working_directory_response(message, status, path);
}

void svc_process_set_working_directory_callback(struct gracht_recv_message* message,
    struct svc_process_set_working_directory_args* args)
{
    Process_t* process = AcquireProcess(args->handle);
    OsStatus_t status  = OsInvalidParameters;
    if (process) {
        if (args->path != NULL) {
            TRACE("proc_set_cwd(%s)", args->path);
            MStringDestroy(process->WorkingDirectory);
            process->WorkingDirectory = MStringCreate((void*)args->path, StrUTF8);
            status = OsSuccess;
        }
        ReleaseProcess(process);
    }
    svc_process_set_working_directory_response(message, status);
}

void svc_process_get_name_callback(struct gracht_recv_message* message, struct svc_process_get_name_args* args)
{
    Process_t*  process = AcquireProcess(args->handle);
    OsStatus_t  status  = OsInvalidParameters;
    const char* name    = NULL;
    if (process) {
        name   = MStringRaw(process->Name);
        status = OsSuccess;
        ReleaseProcess(process);
    }
    svc_process_get_name_response(message, status, name);
}

Process_t*
GetProcessByPrimaryThread(
    _In_ UUId_t ThreadId)
{
    foreach(Node, &Processes) {
        Process_t* process = (Process_t*)Node;
        if (process->PrimaryThreadId == ThreadId) {
            return process;
        }
    }
    return NULL;
}
