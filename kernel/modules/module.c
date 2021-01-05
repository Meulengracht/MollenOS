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

#include <assert.h>
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
    SystemModule_t* module = (SystemModule_t*)Context;
    OsStatus_t status;
    
    assert(module != NULL);
    TimersGetSystemTick(&module->StartedAt);

    // Setup base address for code data
    TRACE("Loading PE-image into memory (path %s)", MStringRaw(module->Path));
    status = PeLoadImage(UUID_INVALID, NULL, module->Path, &module->Executable);
    if (status != OsSuccess) {
        ERROR("Failed to bootstrap pe image: %" PRIuIN "", status);
        module->PrimaryThreadId = UUID_INVALID;
    }
    
    if (status == OsSuccess) {
        ThreadingEnterUsermode((ThreadEntry_t)module->Executable->EntryAddress, NULL);
    }
}

OsStatus_t
SpawnModule(
    _In_ SystemModule_t* Module)
{
    int        index;
    OsStatus_t status;
    MString_t* moduleName;
    TRACE("SpawnModule(%s)", MStringRaw(Module->Path));

    assert(Module != NULL);
    assert(Module->Executable == NULL);
    assert(Module->Data != NULL && Module->Length != 0);

    // Split path, even if a / is not found
    // it won't fail, since -1 + 1 = 0, so we just copy the entire string
    index                    = MStringFindReverse(Module->Path, '/', 0);
    Module->WorkingDirectory = MStringSubString(Module->Path, 0, index);
    Module->BaseDirectory    = MStringSubString(Module->Path, 0, index);
    moduleName               = MStringSubString(Module->Path, index + 1, -1);
    status                   = ThreadCreate(MStringRaw(moduleName), ModuleThreadEntry, Module,
                                THREADING_KERNELENTRY | THREADING_USERMODE, UUID_INVALID,
                                &Module->PrimaryThreadId);
    MStringDestroy(moduleName);
    if (status != OsSuccess) {
        // @todo cleanup everything?
        return status;
    }
    ThreadDetach(Module->PrimaryThreadId);
    return OsSuccess;
}
