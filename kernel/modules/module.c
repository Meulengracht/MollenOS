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

#include "../../librt/libds/pe/pe.h"
#include <modules/manager.h>
#include <modules/module.h>
#include <system/utils.h>
#include <threading.h>
#include <machine.h>
#include <handle.h>
#include <timers.h>
#include <debug.h>
#include <heap.h>

typedef struct _SystemModulePackage {
    SystemModule_t* Module;
    MString_t*      ModuleName;
    const void*     FileBuffer;
    size_t          FileBufferLength;
    int             FileBufferDynamic;
} SystemModulePackage_t;

/* ModuleThreadEntry
 * Bootstraps the module, by relocating the image correctly into the module's address
 * space and handles operatings that must be done on the same thread. */
void
ModuleThreadEntry(
    _In_ void* Context)
{
    SystemModulePackage_t* Package    = (SystemModulePackage_t*)Context;
    UUId_t                 CurrentCpu = ArchGetProcessorCoreId();
    MCoreThread_t*         Thread     = GetCurrentThreadForCore(CurrentCpu);
    OsStatus_t             Status;
    
    assert(Package != NULL);
    TimersGetSystemTick(&Package->Module->StartedAt);

    // Setup base address for code data
    TRACE("Loading PE-image into memory (buffer 0x%x, size %u)", 
        Package->FileBuffer, Package->FileBufferLength);
    Status = PeLoadImage(NULL, Package->ModuleName, Package->Module->Path, (uint8_t*)Package->FileBuffer,
        Package->FileBufferLength, &Package->Module->Executable);
    if (Status == OsSuccess) {
        Thread->Function  = (ThreadEntry_t)Package->Module->Executable->EntryAddress;
        Thread->Arguments = NULL;
    }
    else {
        ERROR("Failed to bootstrap pe image: %u", Status);
        Package->Module->PrimaryThreadId = UUID_INVALID;
    }
    
    if (Package->FileBufferDynamic) {
        kfree((void*)Package->FileBuffer);
    }
    MStringDestroy(Package->ModuleName);
    kfree(Package);
    
    if (Status == OsSuccess) {
        EnterProtectedThreadLevel();
    }
}

/* SpawnModule 
 * Loads the module given into memory, creates a new bootstrap thread and executes the module. */
OsStatus_t
SpawnModule(
    _In_  SystemModule_t* Module,
    _In_  const void*     Data,
    _In_  size_t          Length)
{
    SystemModulePackage_t* Package;
    int                    Index;
    OsStatus_t             Status;

    assert(Module != NULL);
    assert(Module->Executable == NULL);

    Package = (SystemModulePackage_t*)kmalloc(sizeof(SystemModulePackage_t));
    Package->FileBuffer        = Data;
    Package->FileBufferLength  = Length;
    Package->Module            = Module;
    Package->FileBufferDynamic = 1;

    // If no data is passed the data stored initially in module structure
    // must be present
    if (Data == NULL || Length == 0) {
        assert(Module->Data != NULL && Module->Length != 0);
        Package->FileBuffer        = Module->Data;
        Package->FileBufferLength  = Module->Length;
        Package->FileBufferDynamic = 0;
    }

    Status = PeValidateImageBuffer((uint8_t*)Package->FileBuffer, Package->FileBufferLength);
    if (Status != OsSuccess) {
        kfree(Package);
        return Status;
    }

    // Create initial resources
    Module->Rpc = CreateSystemPipe(PIPE_MPMC | PIPE_STRUCTURED_BUFFER, PIPE_DEFAULT_ENTRYCOUNT);

    // Split path, even if a / is not found
    // it won't fail, since -1 + 1 = 0, so we just copy the entire string
    Index                    = MStringFindReverse(Module->Path, '/', 0);
    Module->WorkingDirectory = MStringSubString(Module->Path, 0, Index);
    Module->BaseDirectory    = MStringSubString(Module->Path, 0, Index);
    Package->ModuleName      = MStringSubString(Module->Path, Index + 1, -1);
    Status                   = CreateThread(MStringRaw(Package->ModuleName), ModuleThreadEntry, Package, 
        THREADING_USERMODE, UUID_INVALID, &Module->PrimaryThreadId);
    if (Status != OsSuccess) {
        // @todo cleanup everything?
        MStringDestroy(Package->ModuleName);
        kfree(Package);
        return Status;
    }
    ThreadingDetachThread(Module->PrimaryThreadId);
    return OsSuccess;
}
