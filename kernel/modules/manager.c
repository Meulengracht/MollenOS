/* MollenOS
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

#include <system/interrupts.h>
#include <system/utils.h>
#include <garbagecollector.h>
#include <modules/manager.h>
#include <modules/module.h>
#include <ds/collection.h>
#include <interrupts.h>
#include <threading.h>
#include <debug.h>
#include <heap.h>

//OsStatus_t PhoenixFileHandler(void *UserData);

static Collection_t Modules           = COLLECTION_INIT(KeyInteger);
static UUId_t       GcFileHandleId    = 0;
static UUId_t       ModuleIdGenerator = 1;

/* InitializeModuleManager
 * Initializes the static storage needed for the module manager, and registers a garbage collector. */
void
InitializeModuleManager(void)
{
    //GcFileHandleId = GcRegister(PhoenixFileHandler);
}

/* RegisterModule
 * Registers a new system module resource that is then available for the operating system
 * to use. The resource can be can be either an driver, service or a generic file. */
OsStatus_t
RegisterModule(
    _In_ const char*        Path,
    _In_ const void*        Data,
    _In_ size_t             Length,
    _In_ SystemModuleType_t Type,
    _In_ DevInfo_t          VendorId,
    _In_ DevInfo_t          DeviceId,
    _In_ DevInfo_t          DeviceClass,
    _In_ DevInfo_t          DeviceSubclass)
{
    SystemModule_t* Module;

    // Allocate a new module header and copy some values 
    Module       = (SystemModule_t*)kmalloc(sizeof(SystemModule_t));
    memset(Module, 0, sizeof(SystemModule_t));
    Module->ListHeader.Key.Value.Integer = (int)Type;

    Module->Handle = ModuleIdGenerator++;
    Module->Data   = Data;
    Module->Path   = MStringCreate("rd:/", StrUTF8);
    MStringAppendCharacters(Module->Path, Path, StrUTF8);

    Module->VendorId        = VendorId;
    Module->DeviceId        = DeviceId;
    Module->DeviceClass     = DeviceClass;
    Module->DeviceSubclass  = DeviceSubclass;
    Module->PrimaryThreadId = UUID_INVALID;
    return CollectionAppend(&Modules, &Module->ListHeader);
}

/* SpawnServices
 * Loads all system services present in the initial ramdisk. */
void
SpawnServices(void)
{
    IntStatus_t IrqState;

    // Disable interrupts while doing this as
    // we are still the idle thread -> as soon as a new
    // work is spawned we hardly ever return to this
    IrqState = InterruptDisable();

    // Iterate module list and spawn all servers
    // then they will "run" the system for us
    foreach(Node, &Modules) {
        if (Node->Key.Value.Integer == (int)ServiceResource) {
            SystemModule_t* Module = (SystemModule_t*)Node;
            OsStatus_t      Status = SpawnModule((SystemModule_t*)Node, NULL, 0);
            if (Status != OsSuccess) {
                FATAL(FATAL_SCOPE_KERNEL, "Failed to spawn module %s: %u", MStringRaw(Module->Path), Status);
            }
        }
    }
    InterruptRestoreState(IrqState);
}

/* GetModuleDataByPath
 * Retrieve a pointer to the file-buffer and its length based on 
 * the given <rd:/> path */
OsStatus_t
GetModuleDataByPath(
    _In_  MString_t* Path, 
    _Out_ void**     Buffer, 
    _Out_ size_t*    Length)
{
    MString_t* Token  = Path;
    OsStatus_t Result = OsError;
    int        Index;
    TRACE("GetModuleDataByPath(%s)", MStringRaw(Path));

    // Build the token we are looking for
    Index = MStringFindReverse(Path, '/', 0);
    if (Index != MSTRING_NOT_FOUND) {
        Token = MStringSubString(Path, Index + 1, -1);
    }
    TRACE("TokenToSearchFor(%s)", MStringRaw(Token));

    // Locate the module
    foreach(Node, &Modules) {
        SystemModule_t* Module = (SystemModule_t*)Node;
        TRACE("Comparing(%s)To(%s)", MStringRaw(Token), MStringRaw(Module->Path));
        if (MStringCompare(Token, Module->Path, 1) != MSTRING_NO_MATCH) {
            *Buffer = (void*)Module->Data;
            *Length = Module->Length;
            Result  = OsSuccess;
            break;
        }
    }
    if (Index != MSTRING_NOT_FOUND) {
        MStringDestroy(Token);
    }
    return Result;
}

/* GetGenericDeviceModule
 * Resolves a device module by it's generic class and subclass instead of a device specific
 * module that is resolved by vendor id and product id. */
SystemModule_t*
GetGenericDeviceModule(
    _In_ DevInfo_t DeviceClass, 
    _In_ DevInfo_t DeviceSubclass)
{
    foreach(Node, &Modules) {
        SystemModule_t* Module = (SystemModule_t*)Node;
        if (Module->DeviceClass       == DeviceClass
            && Module->DeviceSubclass == DeviceSubclass) {
            return Module;
        }
    }
    return NULL;
}

/* GetSpecificDeviceModule
 * Resolves a specific device module that is specified by both vendor id and product id. */
SystemModule_t*
GetSpecificDeviceModule(
    _In_ DevInfo_t VendorId,
    _In_ DevInfo_t DeviceId)
{
    if (VendorId == 0) {
        return NULL;
    }
    foreach(Node, &Modules) {
        SystemModule_t* Module = (SystemModule_t*)Node;
        if (Module->VendorId == VendorId && Module->DeviceId == DeviceId) {
            return Module;
        }
    }
    return NULL;
}

/* GetModule
 * Retrieves an existing module instance based on the identification markers. */
SystemModule_t*
GetModule(
    _In_  DevInfo_t VendorId,
    _In_  DevInfo_t DeviceId,
    _In_  DevInfo_t DeviceClass,
    _In_  DevInfo_t DeviceSubclass)
{
    foreach(Node, &Modules) {
        SystemModule_t* Module = (SystemModule_t*)Node;
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

/* GetCurrentModule
 * Retrieves the module that belongs to the calling thread. */
SystemModule_t*
GetCurrentModule(void)
{
    MCoreThread_t* Thread = ThreadingGetCurrentThread(CpuGetCurrentId());
    foreach(Node, &Modules) {
        SystemModule_t* Module = (SystemModule_t*)Node;
        if (Module->PrimaryThreadId == Thread->Id /* || IsThreadChildOf(Module->PrimaryThreadId)*/) {
            return Module;
        }
    }
    return NULL;
}

/* SetModuleAlias
 * Sets the alias for the currently running module. Only the primary thread is allowed to perform
 * this call. */
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

/* GetModuleByAlias
 * Retrieves a running service/module by it's registered alias. This is usually done
 * by system services to be contactable by applications. */
SystemModule_t*
GetModuleByAlias(
    _In_ UUId_t Alias)
{
    foreach(Node, &Modules) {
        SystemModule_t* Module = (SystemModule_t*)Node;
        if (Module->Alias == Alias) {
            return Module;
        }
    }
    return NULL;
}

/* RegisterFileMappingEvent
 * Signals a new file-mapping access event to the system. */
void
RegisterFileMappingEvent(
    _In_ SystemFileMappingEvent_t* Event)
{
    //GcSignal(GcFileHandleId, Event);
}

#if 0
/* PhoenixFileHandler
 * Handles new file-mapping events that occur through unmapped page events. */
OsStatus_t
PhoenixFileHandler(
    _In_Opt_ void*  Context)
{
    SystemFileMappingEvent_t* Event   = (SystemFileMappingEvent_t*)Context;
    SystemFileMapping_t*      Mapping = NULL;
    LargeInteger_t            Value;

    Event->Result = OsError;
    foreach(Node, Event->MemorySpace->FileMappings) {
        Mapping = (SystemFileMapping_t*)Node->Data;
        if (ISINRANGE(Event->Address, Mapping->BufferObject.Address, (Mapping->BufferObject.Address + Mapping->Length) - 1)) {
            Flags_t MappingFlags    = MAPPING_USERSPACE | MAPPING_FIXED | MAPPING_PROVIDED;
            size_t BytesIndex       = 0;
            size_t BytesRead        = 0;
            size_t Offset;
            if (!(Mapping->Flags & FILE_MAPPING_WRITE)) {
                MappingFlags |= MAPPING_READONLY;
            }
            if (Mapping->Flags & FILE_MAPPING_EXECUTE) {
                MappingFlags |= MAPPING_EXECUTABLE;
            }

            // Allocate a page for this transfer
            Mapping->BufferObject.Dma = AllocateSystemMemory(GetSystemMemoryPageSize(), __MASK, MEMORY_DOMAIN);
            if (Mapping->BufferObject.Dma == 0) {
                return OsSuccess;
            }

            // Calculate the file offset, but it has to be page-aligned
            Offset          = (Event->Address - Mapping->BufferObject.Address);
            Offset         -= Offset % GetSystemMemoryPageSize();

            // Create the mapping
            Value.QuadPart  = Mapping->FileBlock + Offset; // File offset in page-aligned blocks
            Event->Result = CreateSystemMemorySpaceMapping(Event->MemorySpace, 
                &Mapping->BufferObject.Dma, &Event->Address, GetSystemMemoryPageSize(), MappingFlags, __MASK);

            // Seek to the file offset, then perform the read of one-page size
            if (SeekFile(Mapping->FileHandle, Value.u.LowPart, Value.u.HighPart) == FsOk && 
                ReadFile(Mapping->FileHandle, Mapping->BufferObject.Handle, GetSystemMemoryPageSize(), &BytesIndex, &BytesRead) == FsOk) {
                Event->Result = OsSuccess;
            }
        }
    }
    SchedulerHandleSignal((uintptr_t*)Event);
    return OsSuccess;
}
#endif
