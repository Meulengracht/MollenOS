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
#include <arch/utils.h>
#include <threading.h>
#include <machine.h>
#include <handle.h>
#include <timers.h>
#include <debug.h>

void
ModuleThreadEntry(
    _In_ void* Context)
{
    SystemModule_t* Module    = (SystemModule_t*)Context;
    UUId_t          CurrentCpu = ArchGetProcessorCoreId();
    MCoreThread_t*  Thread     = GetCurrentThreadForCore(CurrentCpu);
    OsStatus_t      Status;
    
    assert(Module != NULL);
    TimersGetSystemTick(&Module->StartedAt);

    // Setup base address for code data
    TRACE("Loading PE-image into memory (path %s)", MStringRaw(Module->Path));
    Status = PeLoadImage(UUID_INVALID, NULL, Module->Path, &Module->Executable);
    if (Status == OsSuccess) {
        Thread->Function  = (ThreadEntry_t)Module->Executable->EntryAddress;
        Thread->Arguments = NULL;
    }
    else {
        ERROR("Failed to bootstrap pe image: %" PRIuIN "", Status);
        Module->PrimaryThreadId = UUID_INVALID;
    }
    
    if (Status == OsSuccess) {
        EnterProtectedThreadLevel();
    }
}

OsStatus_t
SpawnModule(
    _In_ SystemModule_t* Module)
{
    int                    Index;
    OsStatus_t             Status;
    MString_t*             ModuleName;
    TRACE("SpawnModule(%s)", MStringRaw(Module->Path));

    assert(Module != NULL);
    assert(Module->Executable == NULL);
    assert(Module->Data != NULL && Module->Length != 0);

    // Split path, even if a / is not found
    // it won't fail, since -1 + 1 = 0, so we just copy the entire string
    Index                    = MStringFindReverse(Module->Path, '/', 0);
    Module->WorkingDirectory = MStringSubString(Module->Path, 0, Index);
    Module->BaseDirectory    = MStringSubString(Module->Path, 0, Index);
    ModuleName               = MStringSubString(Module->Path, Index + 1, -1);
    Status                   = CreateThread(MStringRaw(ModuleName), ModuleThreadEntry, Module, 
        THREADING_KERNELENTRY | THREADING_USERMODE, UUID_INVALID, &Module->PrimaryThreadId);
    MStringDestroy(ModuleName);
    if (Status != OsSuccess) {
        // @todo cleanup everything?
        return Status;
    }
    ThreadingDetachThread(Module->PrimaryThreadId);
    return OsSuccess;
}
