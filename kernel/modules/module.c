/* MollenOS
 *
 * Copyright 2018, Philip Meulengracht
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
 * Alias & Process Management
 * - The implementation of phoenix is responsible for managing alias's, handle
 *   file events and creating/destroying processes.
 */
#define __MODULE "PROC"
//#define __TRACE

#include <modules/manager.h>
#include <modules/module.h>
#include <system/utils.h>
#include <threading.h>
#include <machine.h>
#include <handle.h>
#include <timers.h>
#include <debug.h>
#include <heap.h>

typedef struct _SystemProcessPackage {
    UUId_t      ProcessHandle;
    uint8_t*    FileBuffer;
    size_t      FileBufferLength;
} SystemProcessPackage_t;

/* ProcessThreadEntry
 * This is the standard ash-boot function
 * which simply sets up the ash and jumps to userland */
void
ProcessThreadEntry(
    _In_ void*                          Context)
{
    SystemProcessPackage_t* Package     = (SystemProcessPackage_t*)Context;
    SystemProcess_t* Process            = (SystemProcess_t*)LookupHandle(Package->ProcessHandle);
    UUId_t          CurrentCpu          = CpuGetCurrentId();
    MCoreThread_t*  Thread              = ThreadingGetCurrentThread(CurrentCpu);
    uintptr_t       BaseAddress;
    
    assert(Package != NULL);
    assert(Process != NULL);
    assert(Thread != NULL);

    // Argument when calling a new process is just NULL
    Thread->ParentThreadId  = UUID_INVALID;
    Thread->ProcessHandle   = Package->ProcessHandle;

    // Update currently running thread, by nulling parent we mark
    // it as a standalone thread, which make sure it's not a part of a killable chain
    Process->MainThreadId   = Thread->Id;
    Process->MemorySpace    = GetCurrentSystemMemorySpace();
    TimersGetSystemTick(&Process->StartedAt);

    // Setup base address for code data
    TRACE("Loading PE-image into memory (buffer 0x%x, size %u)", 
        Package->FileBuffer, Package->FileBufferLength);
    BaseAddress                 = GetMachine()->MemoryMap.UserCode.Start;
    Process->Executable         = PeLoadImage(NULL, Process->Name, Package->FileBuffer, 
        Package->FileBufferLength, &BaseAddress, Package->LoadedFromInitRD);
    Process->NextLoadingAddress = BaseAddress;

    // Update entry functions
    assert(Process->Executable != NULL);
    Thread->Function        = (ThreadEntry_t)Process->Executable->EntryAddress;
    Thread->Arguments       = NULL;

    if (!Package->LoadedFromInitRD) {
        kfree(Package->FileBuffer);
    }
    kfree(Package);

    // Initialize the memory bitmaps
    CreateBlockmap(0, GetMachine()->MemoryMap.UserHeap.Start, 
        GetMachine()->MemoryMap.UserHeap.Start + GetMachine()->MemoryMap.UserHeap.Length, 
        GetMachine()->MemoryGranularity, &Process->Heap);
    ThreadingSwitchLevel();
}

/* SpawnModule 
 * Loads the module given into memory, creates a new bootstrap thread and executes the module. */
OsStatus_t
SpawnModule(
    _In_  SystemModule_t* Module,
    _In_  const void*     Data,
    _In_  size_t          Length)
{
    SystemProcessPackage_t* Package;
    int                     Index;
    UUId_t                  ThreadId;

    assert(Module != NULL);
    assert(Module->Executable == NULL);

    // If no data is passed the data stored initially in module structure
    // must be present
    if (Data == NULL || Length == 0) {
        assert(Module->Data != NULL && Module->Length != 0);
    }

    // Split path, even if a / is not found
    // it won't fail, since -1 + 1 = 0, so we just copy the entire string
    Index                       = MStringFindReverse(Process->Path, '/', 0);
    Process->WorkingDirectory   = MStringSubString(Process->Path, 0, Index);
    Process->BaseDirectory      = MStringSubString(Process->Path, 0, Index);
    Process->Name               = MStringSubString(Process->Path, Index + 1, -1);
    Process->Pipes              = CollectionCreate(KeyInteger);
    Process->FileMappings       = CollectionCreate(KeyInteger);
    Process->Type               = Type;
    CreateThread(MStringRaw(Process->Name), ProcessThreadEntry, Package, THREADING_USERMODE, UUID_INVALID, &ThreadId);
    ThreadingDetachThread(ThreadId);
    return OsSuccess;
}
