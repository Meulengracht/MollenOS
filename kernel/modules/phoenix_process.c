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

#include <modules/modules.h>
#include <process/phoenix.h>
#include <process/process.h>
#include <process/pe.h>
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
    int         LoadedFromInitRD;
} SystemProcessPackage_t;

// Prototypes
OsStatus_t LoadFile(const char* Path, char** FullPath, void** Data, size_t* Length);

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

/* HandleProcessStartupInformation
 * Creates neccessary kernel copies of the process startup information, as well as
 * validating the structure. */
void
HandleProcessStartupInformation(
    _In_ SystemProcess_t*               Process,
    _In_ ProcessStartupInformation_t*   StartupInformation)
{
    char* ArgumentBuffer;

    // Handle startup information
    if (StartupInformation->ArgumentPointer != NULL && StartupInformation->ArgumentLength != 0) {
        ArgumentBuffer = (char*)kmalloc(MStringSize(Process->Path) + 1 + StartupInformation->ArgumentLength + 1);

        memcpy(ArgumentBuffer, MStringRaw(Process->Path), MStringSize(Process->Path));
        ArgumentBuffer[MStringSize(Process->Path)] = ' ';
        
        memcpy(ArgumentBuffer + MStringSize(Process->Path) + 1,
            StartupInformation->ArgumentPointer, StartupInformation->ArgumentLength);
        ArgumentBuffer[MStringSize(Process->Path) + 1 + StartupInformation->ArgumentLength] = '\0';
        
        Process->StartupInformation.ArgumentPointer = ArgumentBuffer;
        Process->StartupInformation.ArgumentLength  = MStringSize(Process->Path) + 1 + StartupInformation->ArgumentLength + 1;
    }
    else {
        ArgumentBuffer = (char*)kmalloc(MStringSize(Process->Path) + 1);
        memcpy(ArgumentBuffer, MStringRaw(Process->Path), MStringSize(Process->Path) + 1);

        Process->StartupInformation.ArgumentPointer = ArgumentBuffer;
        Process->StartupInformation.ArgumentLength  = MStringSize(Process->Path) + 1;
    }

    // Debug
    TRACE("Arguments: %s", ArgumentBuffer);

    // Handle the inheritance block
    if (StartupInformation->InheritanceBlockPointer != NULL && StartupInformation->InheritanceBlockLength != 0) {
        void* InheritanceBlock = kmalloc(StartupInformation->InheritanceBlockLength);
        memcpy(InheritanceBlock, StartupInformation->InheritanceBlockPointer, StartupInformation->InheritanceBlockLength);
        Process->StartupInformation.InheritanceBlockPointer = InheritanceBlock;
        Process->StartupInformation.InheritanceBlockLength  = StartupInformation->InheritanceBlockLength;
    }
}

/* LoadProcess
 * Loads the process-file and performs basic executable validation on the data. */
OsStatus_t
LoadProcess(
    _In_ SystemProcess_t*               Process,
    _In_ SystemProcessPackage_t*        Package,
    _In_ MString_t*                     Path)
{
    OsStatus_t  Status;
    uint8_t*    Buffer;
    size_t      Size;
    char*       RawPathString;
    int         ShouldFree = 0;
    
    // Open File
    // We have a special case here in case we are loading from RD
    if (MStringFindCString(Path, "rd:/") != MSTRING_NOT_FOUND) {
        TRACE("Loading from ramdisk (%s)", MStringRaw(Path));
        Status                      = ModulesQueryPath(Path, (void**)&Buffer, &Size);
        RawPathString               = (char*)MStringRaw(Path);
        Package->LoadedFromInitRD   = 1;
    }
    else {
        TRACE("Loading from filesystem (%s)", MStringRaw(Path));
        Status                      = LoadFile(MStringRaw(Path), &RawPathString, (void**)&Buffer, &Size);
        ShouldFree                  = 1;
        Package->LoadedFromInitRD   = 0;
    }

    if (Status == OsSuccess) {
        if (PeValidate(Buffer, Size) == PE_VALID) {
            Process->Path               = MStringCreate(RawPathString, StrUTF8);
            Package->FileBuffer         = Buffer;
            Package->FileBufferLength   = Size;
        }
        else {
            ERROR(" > invalid executable file %s", MStringRaw(Path));
            Status = OsError;
        }
    }
    else {
        ERROR(" > failed to load file %s", MStringRaw(Path));
    }
    
    if (ShouldFree == 1) {
        kfree((void*)RawPathString);
        kfree(Buffer);
    }
    return Status;
}

/* CreateProcess 
 * Creates a new handle, allocates a new handle and initializes a thread that performs
 * the rest of setup required. */
OsStatus_t
CreateProcess(
    _In_  MString_t*                    Path,
    _In_  ProcessStartupInformation_t*  StartupInformation,
    _In_  SystemProcessType_t           Type,
    _Out_ UUId_t*                       Handle)
{
    SystemProcessPackage_t* Package;
    SystemProcess_t*        Process;
    int                     Index;
    UUId_t                  ThreadId;

    assert(Path != NULL);
    assert(Handle != NULL);

    Process = (SystemProcess_t*)kmalloc(sizeof(SystemProcess_t));
    Package = (SystemProcessPackage_t*)kmalloc(sizeof(SystemProcessPackage_t));
    assert(Process != NULL);
    assert(Package != NULL);
    memset(Process, 0, sizeof(SystemProcess_t));
    
    // Start out by trying to resolve the process path, otherwise just abort
    if (LoadProcess(Process, Package, Path) != OsSuccess) {
        ERROR(" > failed to resolve process path");
        kfree(Process);
        return OsDoesNotExist;
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
    Package->ProcessHandle      = CreateHandle(HandleTypeProcess, HandleSynchronize, Process);
    HandleProcessStartupInformation(Process, StartupInformation);

    *Handle  = Package->ProcessHandle;
    CreateThread(MStringRaw(Process->Name), ProcessThreadEntry, Package, THREADING_USERMODE, UUID_INVALID, &ThreadId);
    ThreadingDetachThread(ThreadId);
    return OsSuccess;
}

/* DestroyProcess
 * Callback invoked by the handle system when references on a process reaches zero */
OsStatus_t
DestroyProcess(
    _In_ void*                          Resource)
{
    SystemProcess_t*    Process = (SystemProcess_t*)Resource;
    CollectionItem_t*   Node;
    TRACE("DestroyProcess()");

    if (Process->StartupInformation.ArgumentPointer != NULL) {
        kfree((void*)Process->StartupInformation.ArgumentPointer);
    }
    if (Process->StartupInformation.InheritanceBlockPointer != NULL) {
        kfree((void*)Process->StartupInformation.InheritanceBlockPointer);
    }
    MStringDestroy(Process->WorkingDirectory);
    MStringDestroy(Process->BaseDirectory);
    MStringDestroy(Process->Name);
    MStringDestroy(Process->Path);

    // Cleanup pipes
    _foreach(Node, Process->Pipes) {
        DestroySystemPipe((SystemPipe_t*)Node->Data);
    }
    CollectionDestroy(Process->Pipes);

    // Cleanup mappings
    _foreach(Node, Process->FileMappings) {
        kfree(Node->Data);
    }
    CollectionDestroy(Process->FileMappings);

    DestroyBlockmap(Process->Heap);
    PeUnloadImage(Process->Executable);
    kfree(Process);
    return OsSuccess;
}

/* GetProcess
 * This function retrieves the process structure by it's handle. */
SystemProcess_t*
GetProcess(
    _In_ UUId_t Handle)
{
    SystemProcess_t* Process = LookupHandle(Handle);
    if (Process == NULL) {
        if (GetProcessHandleByAlias(&Handle) == OsSuccess) {
            return (SystemProcess_t*)LookupHandle(Handle);
        }
    }
    return Process;
}

/* GetCurrentProcess
 * Retrieves the currently running process, identified by its thread. If none NULL is returned. */
SystemProcess_t*
GetCurrentProcess(void)
{
    return GetProcess(ThreadingGetCurrentThread(CpuGetCurrentId())->ProcessHandle);
}
