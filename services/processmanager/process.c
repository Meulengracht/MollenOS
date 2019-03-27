/* MollenOS
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

#include <internal/_syscalls.h> // for Syscall_ThreadCreate
#include "../../librt/libds/pe/pe.h"
#include <os/services/file.h>
#include <os/eventqueue.h>
#include <os/mollenos.h>
#include <os/context.h>
#include "process.h"
#include <ds/mstring.h>
#include <ddk/buffer.h>
#include <ddk/utils.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <signal.h>

static Collection_t  Processes          = COLLECTION_INIT(KeyId);
static Collection_t  Joiners            = COLLECTION_INIT(KeyId);
static UUId_t        ProcessIdGenerator = 1;
static EventQueue_t* EventQueue         = NULL;

static OsStatus_t
DestroyProcess(
    _In_ Process_t* Process)
{
    int References = atomic_fetch_sub(&Process->References, 1);
    if (References == 1) {
        CollectionRemoveByNode(&Processes, &Process->Header);
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
        SpinlockRelease(&Process->SyncObject);
    } 
}

Process_t*
AcquireProcess(
    _In_ UUId_t Handle)
{
    DataKey_t  Key     = { .Value.Id = Handle };
    Process_t* Process = (Process_t*)CollectionGetNodeByKey(&Processes, Key, 0);
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
            SpinlockAcquire(&Process->SyncObject);
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
    ProcessJoiner_t*     Join    = (ProcessJoiner_t*)Context;
    JoinProcessPackage_t Package = { .Status = OsTimeout };
    CollectionRemoveByNode(&Joiners, &Join->Header);

    // Notify application about this
    if (Join->Process->State == PROCESS_TERMINATING) {
        Package.Status   = OsSuccess;
        Package.ExitCode = Join->Process->ExitCode;
    }
    RPCRespond(&Join->Address, (const void*)&Package, sizeof(JoinProcessPackage_t));
    DestroyProcess(Join->Process);
    free(Join);
}

static OsStatus_t
TestFilePath(
    _In_ MString_t* Path)
{
    OsFileDescriptor_t FileStats;
    if (GetFileStatsByPath((const char*)MStringRaw(Path), &FileStats) != FsOk) {
        return OsError;
    }
    return OsSuccess;
}

OsStatus_t
ResolveFilePath(
    _In_  UUId_t      ProcessId,
    _In_  MString_t*  Path,
    _Out_ MString_t** FullPathOut)
{
    if (MStringFind(Path, ':', 0) == MSTRING_NOT_FOUND && 
        MStringFind(Path, '$', 0) == MSTRING_NOT_FOUND) {
        // Check the working directory, if it fails iterate the environment defaults
        Process_t* Process = AcquireProcess(ProcessId);
        MString_t* Result;
        int        IsApp;
        int        IsDll;

        // Services do not have working directories (well they do, but that is $sys or $bin)
        // But make sure Result is initialzied to a string
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
    else {
        *FullPathOut = MStringClone(Path);
    }
    return OsSuccess;
}

OsStatus_t
LoadFile(
    _In_  MString_t* FullPath,
    _Out_ void**     BufferOut,
    _Out_ size_t*    LengthOut)
{
    OsStatus_t       Status;
    LargeInteger_t   QueriedSize = { { 0 } };
    void*            Buffer      = NULL;
    FileSystemCode_t FsCode;
    UUId_t           Handle;
    size_t           Size;

    // We have to make sure here that the path is fully resolved before loading. If not
    // then the filemanager will try to use our working directory and that is wrong

    // Open the file as read-only
    FsCode = OpenFile(MStringRaw(FullPath), 0, __FILE_READ_ACCESS, &Handle);
    if (FsCode != FsOk) {
        ERROR("Invalid path given: %s", MStringRaw(FullPath));
        return OsError;
    }

    Status = GetFileSize(Handle, &QueriedSize.u.LowPart, NULL);
    if (Status != OsSuccess) {
        ERROR("Failed to retrieve the file size");
        CloseFile(Handle);
        return Status;
    }

    Size = (size_t)QueriedSize.QuadPart;
    if (Size != 0) {
        DmaBuffer_t* TransferBuffer = CreateBuffer(UUID_INVALID, Size);
        if (TransferBuffer != NULL) {
            Buffer = dsalloc(Size);
            if (Buffer != NULL) {
                size_t Index, Read = 0;
                FsCode = ReadFile(Handle, GetBufferHandle(TransferBuffer), Size, &Index, &Read);
                TRACE("Read %" PRIuIN " bytes from file %s", Read, MStringRaw(FullPath));
                if (FsCode == FsOk && Read != 0) {
                    memcpy(Buffer, (const void*)GetBufferDataPointer(TransferBuffer), Read);
                }
                else {
                    dsfree(Buffer);
                    Status = OsError;
                    Buffer = NULL;
                }
            }
            DestroyBuffer(TransferBuffer);
        }
    }
    CloseFile(Handle);
    *BufferOut = Buffer;
    *LengthOut = Size;
    return Status;
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
    _In_  UUId_t                       Owner,
    _In_  const char*                  Path,
    _In_  ProcessStartupInformation_t* Parameters,
    _In_  const char*                  Arguments,
    _In_  size_t                       ArgumentsLength,
    _In_  void*                        InheritationBlock,
    _In_  size_t                       InheritationBlockLength,
    _Out_ UUId_t*                      Handle)
{
    ThreadParameters_t Paramaters;
    Process_t*         Process;
    MString_t*         PathAsMString;
    size_t             PathLength;
    char*              ArgumentsPointer;
    int                Index;
    OsStatus_t         Status;

    assert(Path != NULL);
    assert(Handle != NULL);
    TRACE("CreateProcess(%s, %u, %u)", Path, ArgumentsLength, InheritationBlockLength);

    Process = (Process_t*)malloc(sizeof(Process_t));
    assert(Process != NULL);
    memset(Process, 0, sizeof(Process_t));

    Process->Header.Key.Value.Id = ProcessIdGenerator++;
    Process->State               = ATOMIC_VAR_INIT(PROCESS_RUNNING);
    Process->References          = ATOMIC_VAR_INIT(1);
    Process->StartedAt           = clock();
    SpinlockReset(&Process->SyncObject);

    // Load the executable
    PathAsMString = MStringCreate((void*)Path, StrUTF8);
    Status        = PeLoadImage(Owner, NULL, PathAsMString, &Process->Executable);
    MStringDestroy(PathAsMString);
    if (Status != OsSuccess) {
        ERROR(" > failed to load executable");
        free(Process);
        return Status;
    }

    // it won't fail, since -1 + 1 = 0, so we just copy the entire string
    Process->Path              = MStringCreate((void*)MStringRaw(Process->Executable->FullPath), StrUTF8);
    Index                      = MStringFindReverse(Process->Path, '/', 0);
    Process->Name              = MStringSubString(Process->Path, Index + 1, -1);
    Process->WorkingDirectory  = MStringSubString(Process->Path, 0, Index);
    Process->AssemblyDirectory = MStringSubString(Process->Path, 0, Index);

    // Store copies of startup information
    memcpy(&Process->StartupInformation, Parameters, sizeof(ProcessStartupInformation_t));

    // Handle arguments, we need to prepend the full path of the executable
    PathLength       = strlen(MStringRaw(Process->Path));
    ArgumentsPointer = malloc(PathLength + 1 + ArgumentsLength);
    
    memcpy(&ArgumentsPointer[0], (const void*)MStringRaw(Process->Path), PathLength);
    ArgumentsPointer[PathLength] = ' ';
    if (Arguments != NULL && ArgumentsLength != 0) {
        memcpy(&ArgumentsPointer[PathLength + 1], (void*)Arguments, ArgumentsLength);
    }
    Process->Arguments       = (const char*)ArgumentsPointer;
    Process->ArgumentsLength = PathLength + 1 + ArgumentsLength;

    if (InheritationBlock != NULL && InheritationBlockLength != 0) {
        Process->InheritationBlock = malloc(InheritationBlockLength);
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
    CollectionAppend(&Processes, &Process->Header);
    *Handle = Process->Header.Key.Value.Id;
    return Status;
}

OsStatus_t
JoinProcess(
    _In_  Process_t*            Process,
    _In_  MRemoteCallAddress_t* Address,
    _In_  size_t                Timeout)
{
    ProcessJoiner_t* Join = (ProcessJoiner_t*)malloc(sizeof(ProcessJoiner_t));
    memset(Join, 0, sizeof(ProcessJoiner_t));
    TRACE("JoinProcess(%u, %u)", Process->Header.Key.Value.Id, Timeout);
    
    Join->Header.Key.Value.Id = Process->Header.Key.Value.Id;
    memcpy(&Join->Address, Address, sizeof(MRemoteCallAddress_t));
    Join->Process = Process;
    if (Timeout != 0) {
        Join->EventHandle = QueueDelayedEvent(EventQueue, HandleJoinProcess, Join, Timeout);
    }
    else {
        Join->EventHandle = UUID_INVALID;
    }

    // Add a reference
    atomic_fetch_add(&Process->References, 1);
    return CollectionAppend(&Joiners, &Join->Header);
}

OsStatus_t
KillProcess(
    _In_ Process_t* Killer,
    _In_ Process_t* Target)
{
    // Verify permissions
    TRACE("KillProcess(%u, %u)", Killer->Header.Key.Value.Id, 
        Target->Header.Key.Value.Id);

    // Send a kill signal on the primary thread, if it fails, then
    // the thread has probably already shutdown, but the process instance is
    // lingering around.
    if (Syscall_ThreadSignal(Target->PrimaryThreadId, SIGKILL) != OsSuccess) {
        return TerminateProcess(Target, -1);
    }
    return OsSuccess;
}

OsStatus_t
TerminateProcess(
    _In_ Process_t* Process,
    _In_ int        ExitCode)
{
    CollectionItem_t* Node;
    TRACE("TerminateProcess(%u, %i)", Process->Header.Key.Value.Id, ExitCode);

    // Mark the process as terminating
    Process->State    = PROCESS_TERMINATING;
    Process->ExitCode = ExitCode;
    
    // Identify all joiners, wake them or cancel their events
    Node = CollectionGetNodeByKey(&Joiners, Process->Header.Key, 0);
    while (Node != NULL) {
        ProcessJoiner_t* Join = (ProcessJoiner_t*)Node;
        if (Join->EventHandle != UUID_INVALID) {
            if (CancelEvent(EventQueue, Join->EventHandle) == OsSuccess) {
                HandleJoinProcess((void*)Join);
            }
        }
        else {
            HandleJoinProcess((void*)Join);
        }

        // Get next node
        Node = CollectionGetNodeByKey(&Joiners, Process->Header.Key, 0);
    }
    return OsSuccess;
}

OsStatus_t
LoadProcessLibrary(
    _In_  Process_t*  Process,
    _In_  const char* Path,
    _Out_ Handle_t*   HandleOut)
{
    MString_t* PathAsMString;
    OsStatus_t Status;

    TRACE("LoadProcessLibrary(%u, %s)", Process->Header.Key.Value.Id, 
        (Path == NULL) ? "Global" : Path);
    if (Path == NULL) {
        *HandleOut = HANDLE_GLOBAL;
        return OsSuccess;
    }

    // Create the neccessary strings
    PathAsMString = MStringCreate((void*)Path, StrUTF8);
    Status        = PeLoadImage(Process->Header.Key.Value.Id, Process->Executable, PathAsMString, (PeExecutable_t**)HandleOut);
    MStringDestroy(PathAsMString);
    return Status;
}

uintptr_t
ResolveProcessLibraryFunction(
    _In_ Process_t*  Process,
    _In_ Handle_t    Handle,
    _In_ const char* Function)
{
    PeExecutable_t* Image = Process->Executable;
    TRACE("ResolveProcessLibraryFunction(%u, %s)", 
        Process->Header.Key.Value.Id, Function);
    if (Handle != HANDLE_GLOBAL) {
        Image = (PeExecutable_t*)Handle;
    }
    return PeResolveFunction(Image, Function);
}

OsStatus_t
UnloadProcessLibrary(
    _In_ Process_t* Process,
    _In_ Handle_t   Handle)
{
    TRACE("UnloadProcessLibrary(%u)", Process->Header.Key.Value.Id);
    if (Handle == HANDLE_GLOBAL) {
        return OsSuccess;
    }
    return PeUnloadLibrary(Process->Executable, (PeExecutable_t*)Handle);
}

OsStatus_t
GetProcessLibraryHandles(
    _In_  Process_t* Process,
    _Out_ Handle_t   LibraryList[PROCESS_MAXMODULES])
{
    TRACE("GetProcessLibraryHandles(%u)", Process->Header.Key.Value.Id);
    return PeGetModuleHandles(Process->Executable, LibraryList);
}

OsStatus_t
GetProcessLibraryEntryPoints(
    _In_  Process_t* Process,
    _Out_ Handle_t   LibraryList[PROCESS_MAXMODULES])
{
    TRACE("GetProcessLibraryEntryPoints(%u)", Process->Header.Key.Value.Id);
    return PeGetModuleEntryPoints(Process->Executable, LibraryList);
}

OsStatus_t
HandleProcessCrashReport(
    _In_ Process_t* Process,
    _In_ Context_t* CrashContext,
    _In_ int        CrashReason)
{
    uintptr_t  ImageBase    = Process->Executable->VirtualAddress;
    MString_t* ImageName    = Process->Executable->Name;
    uintptr_t  CrashAddress = CONTEXT_IP(CrashContext);
    
    TRACE("HandleProcessCrashReport(%i)", CrashReason);
    // Was it not main executable?
    if (CrashAddress > (Process->Executable->CodeBase + Process->Executable->CodeSize)) {
        // Iterate libraries to find the sinner
        if (Process->Executable->Libraries != NULL) {
            foreach(Node, Process->Executable->Libraries) {
                PeExecutable_t* Library = (PeExecutable_t*)Node->Data;
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

Process_t*
GetProcessByPrimaryThread(
    _In_ UUId_t ThreadId)
{
    foreach(Node, &Processes) {
        Process_t* Process = (Process_t*)Node;
        if (Process->PrimaryThreadId == ThreadId) {
            return Process;
        }
    }
    return NULL;
}
