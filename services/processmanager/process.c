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

#include <internal/_syscalls.h> // for Syscall_ThreadCreate
#include "../../librt/libds/pe/pe.h"
#include <os/eventqueue.h>
#include "process.h"
#include <ds/mstring.h>
#include <ddk/buffer.h>
#include <ddk/utils.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

static Collection_t  Processes          = COLLECTION_INIT(KeyId);
static Collection_t  Joiners            = COLLECTION_INIT(KeyId);
static UUId_t        ProcessIdGenerator = 1;
static EventQueue_t* EventQueue         = NULL;

static void
DestroyProcess(
    _In_ Process_t* Process)
{
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
}

static void
HandleJoinProcess(
    _In_ void* Context)
{
    ProcessJoiner_t*     Join    = (ProcessJoiner_t*)Context;
    JoinProcessPackage_t Package = { .Status = OsTimeout };

    // Notify application about this
    if (Join->Process->State == ProcessTerminating) {
        Package.Status   = OsSuccess;
        Package.ExitCode = Join->Process->ExitCode;
    }
    RPCRespond(&Join->Address, (const void*)&Package, sizeof(JoinProcessPackage_t));

    // Release our reference
    DestroyProcess(Join->Process);

    // Cleanup our resources
    CollectionRemoveByNode(&Joiners, &Join->Header);
    free(Join);
}

OsStatus_t
InitializeProcessManager(void)
{
    CreateEventQueue(&EventQueue);
}

OsStatus_t
CreateProcess(
    _In_  const char*                  Path,
    _In_  ProcessStartupInformation_t* Parameters,
    _In_  const char*                  Arguments,
    _In_  size_t                       ArgumentsLength,
    _In_  void*                        InheritationBlock,
    _In_  size_t                       InheritationBlockLength,
    _Out_ UUId_t*                      Handle)
{
    uint8_t*   FileBuffer = NULL;
    size_t     FileBufferLength;
    Process_t* Process;
    MString_t* PathAsMString;
    int        Index;
    OsStatus_t Status;

    assert(Path != NULL);
    assert(Handle != NULL);

    Process = (Process_t*)malloc(sizeof(Process_t));
    assert(Process != NULL);
    memset(Process, 0, sizeof(Process_t));

    // Create the neccessary strings
    PathAsMString = MStringCreate((void*)Path, StrUTF8);
    Status        = LoadFile(PathAsMString, &Process->Path, (void**)&FileBuffer, &FileBufferLength);
    if (Status != OsSuccess) {
        ERROR(" > failed to resolve process path");
        goto CleanupAndExit;
    }

    // Verify image as PE compliant
    Status = PeValidateImageBuffer(FileBuffer, FileBufferLength);
    if (Status != OsSuccess) {
        ERROR(" > invalid pe image");
        goto CleanupAndExit;
    }

    // Split path, even if a / is not found
    // it won't fail, since -1 + 1 = 0, so we just copy the entire string
    Index                      = MStringFindReverse(Process->Path, '/', 0);
    Process->Name              = MStringSubString(Process->Path, Index + 1, -1);
    Process->WorkingDirectory  = MStringSubString(Process->Path, 0, Index);
    Process->AssemblyDirectory = MStringSubString(Process->Path, 0, Index);

    // Load the executable
    Status = PeLoadImage(NULL, Process->Name, Process->Path, FileBuffer, FileBufferLength, &Process->Executable);
    if (Status != OsSuccess) {
        ERROR(" > failed to load executable");
        goto CleanupAndExit;
    }

    // Store copies of startup information
    memcpy(&Process->StartupInformation, Parameters, sizeof(ProcessStartupInformation_t));
    if (Arguments != NULL && ArgumentsLength != 0) {
        Process->Arguments = malloc(ArgumentsLength);
        Process->ArgumentsLength = ArgumentsLength;
        memcpy((void*)Process->Arguments, (void*)Arguments, ArgumentsLength);
    }
    if (InheritationBlock != NULL && InheritationBlockLength != 0) {
        Process->InheritationBlock = malloc(InheritationBlockLength);
        Process->InheritationBlockLength = InheritationBlockLength;
        memcpy(Process->InheritationBlock, InheritationBlock, InheritationBlockLength);
    }

    SpinlockReset(&Process->SyncObject);
    Process->State               = ProcessRunning;
    Process->References          = ATOMIC_VAR_INIT(1);
    Process->Header.Key.Value.Id = ProcessIdGenerator++;
    Process->StartedAt           = clock();
    Process->PrimaryThreadId     = Syscall_ThreadCreate(Process->Executable->EntryAddress, 0, 0, Process->Executable->MemorySpace);
    Status                       = Syscall_ThreadDetach(Process->PrimaryThreadId);
    CollectionAppend(&Processes, &Process->Header);
    free(FileBuffer);
    *Handle = Process->Header.Key.Value.Id;
    return Status;

CleanupAndExit:
    if (FileBuffer != NULL) {
        free(FileBuffer);
    }
    DestroyProcess(Process);
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
    
    Join->Header.Key.Value.Id = Process->Header.Key.Value.Id;
    memcpy(&Join->Address, Address, sizeof(MRemoteCallAddress_t));
    Join->Process = Process;

    SpinlockAcquire(&Process->SyncObject);
    if (Process->State == ProcessRunning) {
        atomic_fetch_and_add(&Process->References, 1);
        Join->EventHandle = QueueDelayedEvent(EventQueue, HandleJoinProcess, Join, Timeout);
        CollectionAppend(&Joiners, &Join->Header);
    }
    else {
        HandleJoinProcess((void*)Join);
    }
    SpinlockRelease(&Process->SyncObject);
    return OsSuccess;
}

OsStatus_t
KillProcess(
    _In_ Process_t* Killer,
    _In_ Process_t* Target)
{
    return OsError;
}

OsStatus_t
TerminateProcess(
    _In_ Process_t* Process,
    _In_ int        ExitCode)
{
    return OsError;
}

OsStatus_t
LoadProcessLibrary(
    _In_  Process_t*  Process,
    _In_  const char* Path,
    _Out_ Handle_t*   HandleOut)
{
    return OsError;
}

uintptr_t
ResolveProcessLibraryFunction(
    _In_ Process_t*  Process,
    _In_ Handle_t    Handle,
    _In_ const char* Function)
{
    return PeResolveFunction((PeExecutable_t*)Handle, Function);
}

OsStatus_t
UnloadProcessLibrary(
    _In_ Process_t* Process,
    _In_ Handle_t   Handle)
{
    return PeUnloadLibrary(Process->Executable, (PeExecutable_t*)Handle);
}

OsStatus_t
GetProcessLibraryHandles(
    _In_  Process_t* Process,
    _Out_ Handle_t   LibraryList[PROCESS_MAXMODULES])
{
    return PeGetModuleHandles(Process->Executable, LibraryList);
}

OsStatus_t
GetProcessLibraryEntryPoints(
    _In_  Process_t* Process,
    _Out_ Handle_t   LibraryList[PROCESS_MAXMODULES])
{
    return PeGetModuleEntryPoints(Process->Executable, LibraryList);
}

Process_t*
GetProcess(
    _In_ UUId_t Handle)
{
    DataKey_t Key = { .Value.Id = Handle };
    return (Process_t*)CollectionGetNodeByKey(&Processes, Key, 0);
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
