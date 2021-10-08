/**
 * MollenOS
 *
 * Copyright 2011, Philip Meulengracht
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
 * Kernel Module System
 *   - Implements loading and management of modules that exists on the initrd. 
 */

#define __MODULE "MODS"
//#define __TRACE

#include <arch/interrupts.h>
#include <arch/utils.h>
#include <assert.h>
#include <debug.h>
#include <handle.h>
#include <heap.h>
#include "../../librt/libds/pe/pe.h"
#include <memoryspace.h>
#include <ds/mstring.h>
#include <scheduler.h>
#include <threading.h>
#include <string.h>

static list_t g_modules = LIST_INIT;

OsStatus_t
RegisterModule(
    _In_ const char*        Path,
    _In_ const void*        Data,
    _In_ size_t             Length,
    _In_ SystemModuleType_t Type,
    _In_ unsigned int          VendorId,
    _In_ unsigned int          DeviceId,
    _In_ unsigned int          DeviceClass,
    _In_ unsigned int          DeviceSubclass)
{
    SystemModule_t* module;
    TRACE("RegisterModule(path=%s, len=%" PRIuIN ", type=%i)", Path, Length, Type);

    module = (SystemModule_t*)kmalloc(sizeof(SystemModule_t));
    if (!module) {
        return OsOutOfMemory;
    }
    
    memset(module, 0, sizeof(SystemModule_t));
    ELEMENT_INIT(&module->ListHeader, Type, module);

    module->Handle = CreateHandle(HandleTypeGeneric, NULL, module);
    module->Data   = Data;
    module->Length = Length;
    module->Path   = MStringCreate("rd:/", StrUTF8);
    MStringAppendCharacters(module->Path, Path, StrUTF8);

    module->VendorId        = VendorId;
    module->DeviceId        = DeviceId;
    module->DeviceClass     = DeviceClass;
    module->DeviceSubclass  = DeviceSubclass;
    module->PrimaryThreadId = UUID_INVALID;
    list_append(&g_modules, &module->ListHeader);
    return OsSuccess;
}

void
SpawnServices(void)
{
    IntStatus_t IrqState;

    // Disable interrupts while doing this as
    // we are still the idle thread -> as soon as a new
    // work is spawned we hardly ever return to this
    IrqState = InterruptDisable();
    SchedulerDisable();

    // Iterate module list and spawn all servers
    // then they will "run" the system for us
    foreach(i, &g_modules) {
        TRACE("SpawnServices checking key %i", (int)(uintptr_t)i->key);
        if ((int)(uintptr_t)i->key == (int)ServiceResource) {
            SystemModule_t* module   = (SystemModule_t*)i->value;
            OsStatus_t      osStatus = SpawnModule(module);
            if (osStatus != OsSuccess) {
                FATAL(FATAL_SCOPE_KERNEL, "Failed to spawn module %s: %" PRIuIN "", MStringRaw(module->Path), osStatus);
            }
        }
    }

    InterruptRestoreState(IrqState);
    SchedulerEnable();
}

OsStatus_t
GetModuleDataByPath(
    _In_  MString_t* Path, 
    _Out_ void**     Buffer, 
    _Out_ size_t*    Length)
{
    OsStatus_t Result = OsError;
    TRACE("GetModuleDataByPath(%s)", MStringRaw(Path));

    foreach(i, &g_modules) {
        SystemModule_t* Module = (SystemModule_t*)i->value;
        if (Module->Path != NULL) {
            TRACE("Comparing(%s)To(%s)", MStringRaw(Path), MStringRaw(Module->Path));
            if (MStringCompare(Path, Module->Path, 1) != MSTRING_NO_MATCH) {
                assert(Module->Data != NULL && Module->Length != 0);
                *Buffer = (void*)Module->Data;
                *Length = Module->Length;
                Result  = OsSuccess;
                break;
            }
        }
    }
    return Result;
}

SystemModule_t*
GetGenericDeviceModule(
    _In_ unsigned int DeviceClass, 
    _In_ unsigned int DeviceSubclass)
{
    foreach(i, &g_modules) {
        SystemModule_t* Module = (SystemModule_t*)i->value;
        if (Module->DeviceClass       == DeviceClass
            && Module->DeviceSubclass == DeviceSubclass) {
            return Module;
        }
    }
    return NULL;
}

SystemModule_t*
GetSpecificDeviceModule(
    _In_ unsigned int VendorId,
    _In_ unsigned int DeviceId)
{
    if (VendorId == 0) {
        return NULL;
    }
    foreach(i, &g_modules) {
        SystemModule_t* Module = (SystemModule_t*)i->value;
        if (Module->VendorId == VendorId && Module->DeviceId == DeviceId) {
            return Module;
        }
    }
    return NULL;
}

SystemModule_t*
GetModule(
    _In_  unsigned int VendorId,
    _In_  unsigned int DeviceId,
    _In_  unsigned int DeviceClass,
    _In_  unsigned int DeviceSubclass)
{
    foreach(i, &g_modules) {
        SystemModule_t* Module = (SystemModule_t*)i->value;
        if (Module->PrimaryThreadId != UUID_INVALID) {
            // Should we check vendor-id && device-id?
            if (VendorId != 0 && DeviceId != 0) {
                if (Module->VendorId == VendorId && Module->DeviceId == DeviceId) {
                    return Module;
                }
            }

            // Skip all fixed-vendor ids
            if (Module->VendorId != 0xFFEF) {
                if (Module->DeviceClass == DeviceClass && Module->DeviceSubclass == DeviceSubclass) {
                    return Module;
                }
            }
        }
    }   
    return NULL;
}

SystemModule_t*
GetCurrentModule(void)
{
    Thread_t* Thread = ThreadCurrentForCore(ArchGetProcessorCoreId());
    foreach(i, &g_modules) {
        SystemModule_t* Module = (SystemModule_t*)i->value;
        if (Module->Executable != NULL && 
            AreMemorySpacesRelated(Module->Executable->MemorySpace, 
                (MemorySpaceHandle_t)ThreadMemorySpace(Thread)) == OsSuccess) {
            return Module;
        }
    }
    return NULL;
}

OsStatus_t
SetModuleAlias(
    _In_ UUId_t Alias)
{
    SystemModule_t* Module = GetCurrentModule();
    if (Module != NULL) {
        Module->Alias = Alias;
        return OsSuccess;
    }
    return OsInvalidPermissions;
}

SystemModule_t*
GetModuleByHandle(
    _In_ UUId_t Handle)
{
    foreach(i, &g_modules) {
        SystemModule_t* Module = (SystemModule_t*)i->value;
        if (Module->Handle == Handle || Module->Alias == Handle) {
            return Module;
        }
    }
    return NULL;
}
