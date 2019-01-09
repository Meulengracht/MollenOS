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
#include "process.h"
#include <ds/mstring.h>
#include <os/buffer.h>
#include <os/utils.h>
#include <stdlib.h>

static Collection_t Processes          = COLLECTION_INIT(KeyId);
static UUId_t       ProcessIdGenerator = 1;

void
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
        memcpy(Process->Arguments, Arguments, ArgumentsLength);
    }
    if (InheritationBlock != NULL && InheritationBlockLength != 0) {
        Process->InheritationBlock = malloc(InheritationBlockLength);
        Process->InheritationBlockLength = InheritationBlockLength;
        memcpy(Process->InheritationBlock, InheritationBlock, InheritationBlockLength);
    }

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
