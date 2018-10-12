/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS MCore - Server & Process Management
 * - The process/server manager is known as Phoenix
 * - In this file we implement Ash functions
 */
#define __MODULE "ASH1"
//#define __TRACE

#include <system/thread.h>
#include <system/utils.h>
#include <process/phoenix.h>
#include <modules/modules.h>
#include <scheduler.h>
#include <threading.h>
#include <machine.h>
#include <timers.h>
#include <debug.h>
#include <heap.h>

// Prototypes
OsStatus_t LoadFile(const char* Path, char** FullPath, void** Data, size_t* Length);

/* This is the finalizor function for starting
 * up a new base Ash, it finishes setting up the environment
 * and memory mappings, must be called on it's own thread */
void
PhoenixFinishAsh(
    _In_ MCoreAsh_t *Ash)
{
    // Variables
    UUId_t CurrentCpu       = CpuGetCurrentId();
    MCoreThread_t *Thread   = ThreadingGetCurrentThread(CurrentCpu);
    uintptr_t BaseAddress   = 0;
    int LoadedFromInitRD    = 0;

    // Debug
    TRACE("PhoenixFinishAsh(%s)", MStringRaw(Ash->Path));

    // Sanitize the loaded path, if we were
    // using the initrd set flags accordingly
    if (MStringFindCString(Ash->Path, "rd:/") != MSTRING_NOT_FOUND) {
        LoadedFromInitRD = 1;
    }

    // Update currently running thread
    Ash->MainThread         = Thread->Id;
    Thread->AshId           = Ash->Id;
    Thread->ParentId        = UUID_INVALID;
    TimersGetSystemTick(&Thread->StartedAt);

    // Store current address space
    Ash->MemorySpace       = GetCurrentSystemMemorySpace();

    // Setup base address for code data
    BaseAddress             = GetMachine()->MemoryMap.UserCode.Start;

    // Load Executable
    TRACE("Loading PE-image into memory (buffer 0x%x, size %u)", 
        Ash->FileBuffer, Ash->FileBufferLength);
    Ash->Executable         = PeLoadImage(NULL, Ash->Name, Ash->FileBuffer, 
        Ash->FileBufferLength, &BaseAddress, LoadedFromInitRD);
    Ash->NextLoadingAddress = BaseAddress;

    // Cleanup file buffer
    if (!LoadedFromInitRD) {
        kfree(Ash->FileBuffer);
    }
    Ash->FileBuffer = NULL;

    // Initialize the memory bitmaps
    CreateBlockmap(0, GetMachine()->MemoryMap.UserHeap.Start, 
        GetMachine()->MemoryMap.UserHeap.Start + GetMachine()->MemoryMap.UserHeap.Length, 
        GetMachine()->MemoryGranularity, &Ash->Heap);
}

/* PhoenixStartupEntry
 * This is the standard ash-boot function
 * which simply sets up the ash and jumps to userland */
void
PhoenixStartupEntry(
    _In_ void *BasePointer) {
    // Variables
    MCoreAsh_t *Ash = (MCoreAsh_t*)BasePointer;

    // Debug
    TRACE("PhoenixStartupEntry(%s)", MStringRaw(Ash->Path));

    PhoenixFinishAsh(Ash);
    ThreadingSwitchLevel(Ash);
}

/* PhoenixInitializeAsh
 * This function loads the executable and
 * prepares the ash-environment, at this point
 * it won't be completely running yet, it needs
 * its own thread for that. Returns 0 on success */
OsStatus_t
PhoenixInitializeAsh(
    _InOut_ MCoreAsh_t* Ash, 
    _In_    MString_t*  Path)
{ 
    // Variables
    OsStatus_t Status;
    uint8_t *fBuffer;
    size_t fSize;
    char *fPath;
    int Index = 0, ShouldFree = 0;

    // Sanitize inputs
    if (Path == NULL) {
        ERROR("Invalid parameters for PhoenixInitializeAsh");
        return OsError;
    }

    // Debug
    TRACE("PhoenixInitializeAsh(%s)", MStringRaw(Path));

    // Zero out structure
    memset(Ash, 0, sizeof(MCoreAsh_t));

    // Open File 
    // We have a special case here in case we are loading from RD
    if (MStringFindCString(Path, "rd:/") != -1) {
        TRACE("Loading from ramdisk (%s)", MStringRaw(Path));
        Status      = ModulesQueryPath(Path, (void**)&fBuffer, &fSize);
        fPath       = (char*)MStringRaw(Path);
    }
    else {
        TRACE("Loading from filesystem (%s)", MStringRaw(LibraryName));
        Status      = LoadFile(MStringRaw(Path), &fPath, (void**)&fBuffer, &fSize);
        ShouldFree  = 1;
    }
    
    // Verify the status of the loading, and the validty of the file
    if (Status != OsSuccess && fBuffer != NULL && fSize != 0) {
        ERROR("Failed to load file %s", MStringRaw(Path));
        return OsError;
    }
    
    Ash->Path = MStringCreate(fPath, StrUTF8);
    if (ShouldFree == 1) {
        kfree((void*)fPath);
    }

    // Validate the pe-file buffer
    if (!PeValidate(fBuffer, fSize)) {
        ERROR("Failed to validate the file as a PE-file.");
        if (ShouldFree == 1) {
            kfree(fBuffer);
        }
        return OsError;
    }

    // Update initial members
    Ash->Id                 = PhoenixGetNextId();
    Ash->Parent             = ThreadingGetCurrentThread(CpuGetCurrentId())->AshId;
    Ash->Type               = AshBase;
    atomic_store_explicit(&Ash->References, 1, memory_order_relaxed);

    // Split path, even if a / is not found
    // it won't fail, since -1 + 1 = 0, so we just copy the entire string
    Index                   = MStringFindReverse(Ash->Path, '/', 0);
    Ash->Name               = MStringSubString(Ash->Path, Index + 1, -1);

    // Store members and initialize members
    Ash->SignalHandler      = 0;
    Ash->FileBuffer         = fBuffer;
    Ash->FileBufferLength   = fSize;
    Ash->Pipes              = CollectionCreate(KeyInteger);
    Ash->FileMappings       = CollectionCreate(KeyInteger);
    return OsSuccess;
}

/* PhoenixStartupAsh
 * This is a wrapper for starting up a base Ash
 * and uses <PhoenixInitializeAsh> to setup the env
 * and do validation before starting */
UUId_t
PhoenixStartupAsh(
    _In_ MString_t *Path)
{
    // Variables
    MCoreAsh_t *Ash = NULL;

    // Allocate and initialize instance
    Ash = (MCoreAsh_t*)kmalloc(sizeof(MCoreAsh_t));
    if (PhoenixInitializeAsh(Ash, Path) != OsSuccess) {
        kfree(Ash);
        return UUID_INVALID;
    }

    // Register ash
    PhoenixRegisterAsh(Ash);
    ThreadingCreateThread(MStringRaw(Ash->Name), PhoenixStartupEntry, Ash, THREADING_USERMODE);
    return Ash->Id;
}

/* PhoenixOpenAshPipe
 * Creates a new communication pipe available for use. */
OsStatus_t
PhoenixOpenAshPipe(
    _In_ MCoreAsh_t*    Ash, 
    _In_ int            Port, 
    _In_ int            Type)
{
    // Variables
    SystemPipe_t *Pipe = NULL;
    DataKey_t Key;

    // Debug
    TRACE("PhoenixOpenAshPipe(Port %i)", Port);

    // Sanitize
    if (Ash == NULL || Port < 0) {
        ERROR("Invalid ash-instance or port id");
        return OsError;
    }

    // Make sure that a pipe on the given Port 
    // doesn't already exist!
    Key.Value = Port;
    if (CollectionGetDataByKey(Ash->Pipes, Key, 0) != NULL) {
        WARNING("The requested pipe already exists");
        return OsSuccess;
    }

    // Create a new pipe and add it to list 
    if (Type == PIPE_RAW) {
        Pipe = CreateSystemPipe(0, PIPE_DEFAULT_ENTRYCOUNT);
    }
    else {
        Pipe = CreateSystemPipe(PIPE_MPMC | PIPE_STRUCTURED_BUFFER, PIPE_DEFAULT_ENTRYCOUNT);
    }
    CollectionAppend(Ash->Pipes, CollectionCreateNode(Key, Pipe));

    // Wake sleepers waiting for pipe creations
    SchedulerHandleSignalAll((uintptr_t*)Ash->Pipes);
    return OsSuccess;
}

/* PhoenixWaitAshPipe
 * Waits for a pipe to be opened on the given
 * ash instance. */
OsStatus_t
PhoenixWaitAshPipe(
    _In_ MCoreAsh_t *Ash, 
    _In_ int         Port)
{
    // Variables
    DataKey_t Key;
    int Run = 1;

    // Sanitize input
    if (Ash == NULL) {
        return OsError;
    }

    // Wait for wake-event on pipe
    Key.Value = Port;
    while (Run) {
        if (CollectionGetDataByKey(Ash->Pipes, Key, 0) != NULL) {
            break;
        }
        if (SchedulerThreadSleep((uintptr_t*)Ash->Pipes, 5000) == SCHEDULER_SLEEP_TIMEOUT) {
            ERROR("Failed to wait for open pipe, timeout after 5 seconds.");
            return OsError;
        }
     }
    return OsSuccess;
}

/* PhoenixCloseAshPipe
 * Closes the pipe for the given Ash, and cleansup
 * resources allocated by the pipe. This shutsdown
 * any communication on the port */
OsStatus_t
PhoenixCloseAshPipe(
    _In_ MCoreAsh_t *Ash, 
    _In_ int         Port)
{
    // Variables
    SystemPipe_t *Pipe = NULL;
    DataKey_t Key;

    // Sanitize input
    if (Ash == NULL || Port < 0) {
        return OsSuccess;
    }

    // Lookup pipe
    Key.Value = Port;
    Pipe = (SystemPipe_t*)CollectionGetDataByKey(Ash->Pipes, Key, 0);
    if (Pipe == NULL) {
        return OsError;
    }

    // Cleanup pipe and remove node
    DestroySystemPipe(Pipe);
    return CollectionRemoveByKey(Ash->Pipes, Key);
}

/* PhoenixGetAshPipe
 * Retrieves an existing pipe instance for the given ash
 * and port-id. If it doesn't exist, returns NULL. */
SystemPipe_t*
PhoenixGetAshPipe(
    _In_ MCoreAsh_t     *Ash, 
    _In_ int             Port)
{
    // Variables
    DataKey_t Key;

    // Sanitize input
    if (Ash == NULL || Port < 0) {
        return NULL;
    }

    // Perform the lookup
    Key.Value = Port;
    return (SystemPipe_t*)CollectionGetDataByKey(Ash->Pipes, Key, 0);
}

/* PhoenixGetCurrentAsh
 * Retrives the current ash for the running thread */
MCoreAsh_t*
PhoenixGetCurrentAsh(void)
{
    // Variables
    UUId_t CurrentCpu = UUID_INVALID;

    // Get the ID
    CurrentCpu = CpuGetCurrentId();
    if (ThreadingGetCurrentThread(CurrentCpu) != NULL) {
        if (ThreadingGetCurrentThread(CurrentCpu)->AshId != UUID_INVALID) {
            return PhoenixGetAsh(ThreadingGetCurrentThread(CurrentCpu)->AshId);
        }
        else {
            return NULL;
        }
    }
    else {
        return NULL;
    }
}

/* PhoenixCleanupAsh
 * Cleans up a given Ash, freeing all it's allocated resources
 * and unloads it's executables, memory space is not cleaned up
 * must be done by external thread */
void
PhoenixCleanupAsh(
    _In_ MCoreAsh_t *Ash)
{
    // Variables
    CollectionItem_t *Node = NULL;

    // Strings first
    MStringDestroy(Ash->Name);
    MStringDestroy(Ash->Path);

    // Cleanup pipes
    _foreach(Node, Ash->Pipes) {
        DestroySystemPipe((SystemPipe_t*)Node->Data);
    }
    CollectionDestroy(Ash->Pipes);

    // Cleanup mappings
    _foreach(Node, Ash->FileMappings) {
        kfree(Node->Data);
    }
    CollectionDestroy(Ash->FileMappings);

    // Cleanup memory
    DestroyBlockmap(Ash->Heap);
    PeUnloadImage(Ash->Executable);
    kfree(Ash);
}

