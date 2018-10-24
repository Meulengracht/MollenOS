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
#define __TRACE

#include <process/phoenix.h>
#include <process/process.h>
#include <criticalsection.h>
#include <os/mollenos.h>
#include <threading.h>
#include <machine.h>
#include <handle.h>
#include <assert.h>

OsStatus_t PhoenixFileHandler(void *UserData);

static Collection_t Services                        = COLLECTION_INIT(KeyInteger);
static UUId_t       AliasMap[PHOENIX_MAX_ALIASES]   = { 0 };
static UUId_t       GcFileHandleId                  = 0;

// Used with the DISABLE_SIMOULTANIOUS loading setting
CriticalSection_t LoaderLock;

/* InitializePhoenix
 * Initializes the process and server manager. Keeps track of registered
 * alias's and handles file mapped events. */
void
InitializePhoenix(void)
{
    int i;
    GcFileHandleId = GcRegister(PhoenixFileHandler);
    CriticalSectionConstruct(&LoaderLock, CRITICALSECTION_REENTRANCY);
    for (i = 0; i < PHOENIX_MAX_ALIASES; i++) {
        AliasMap[i] = UUID_INVALID;
    }
}

/* SetProcessAlias
 * Allows a server to register an alias for its id which means that id (must be above SERVER_ALIAS_BASE)
 * will always refer the calling process */
OsStatus_t
SetProcessAlias(
    _In_ UUId_t         Handle,
    _In_ UUId_t         Alias)
{
    // Sanitize both the server and alias 
    if (Alias < PHOENIX_ALIAS_BASE || AliasMap[Alias - PHOENIX_ALIAS_BASE] != UUID_INVALID) {
        ERROR("Failed to register alias 0x%x for ash %u (0x%x - %u)",
            Alias, Handle, AliasMap[Alias - PHOENIX_ALIAS_BASE], 
            Alias - PHOENIX_ALIAS_BASE);
        return OsError;
    }
    AliasMap[Alias - PHOENIX_ALIAS_BASE] = Handle;
    return OsSuccess;
}

/* GetProcessHandleByAlias
 * Checks if the given process-id has an registered alias.
 * If it has, the given process-id will be overwritten. */
OsStatus_t
GetProcessHandleByAlias(
    _InOut_ UUId_t*     Alias)
{
    if (*Alias >= PHOENIX_ALIAS_BASE && 
        *Alias < (PHOENIX_ALIAS_BASE + PHOENIX_MAX_ALIASES)) {
        *Alias = AliasMap[*Alias - PHOENIX_ALIAS_BASE];
        return OsSuccess;
    }
    return OsError;
}

/* CreateService
 * Creates a new service by the service identification, this in turns call CreateProcess. */
OsStatus_t
CreateService(
    _In_  MString_t*    Path,
    _In_  DevInfo_t     VendorId,
    _In_  DevInfo_t     DeviceId,
    _In_  DevInfo_t     DeviceClass,
    _In_  DevInfo_t     DeviceSubClass,
    _Out_ UUId_t*       Handle)
{
    DevInfo_t ServiceInfo[4] = { 
        VendorId, DeviceId, DeviceClass, DeviceSubClass
    };
    ProcessStartupInformation_t Info = {
        (const char*)&ServiceInfo[0], sizeof(ServiceInfo), 0
    };
    OsStatus_t Status = CreateProcess(Path, &Info, Handle);
    if (Status == OsSuccess) {
        DataKey_t Value;
        Value.Value = *Handle;
        CollectionAppend(&Services, CollectionCreateNode(Value, LookupHandle(*Handle)));
    }
    return Status;
}

/* GetServiceByIdentification
 * Retrieves a running service by driver-information to avoid spawning multiple services */
SystemProcess_t*
GetServiceByIdentification(
    _In_ DevInfo_t VendorId,
    _In_ DevInfo_t DeviceId,
    _In_ DevInfo_t DeviceClass,
    _In_ DevInfo_t DeviceSubClass)
{
    foreach(Node, &Services) {
        SystemProcess_t* Service    = (SystemProcess_t*)Node->Data;
        DevInfo_t* ServiceInfo      = (DevInfo_t*)Service->StartupInformation.ArgumentPointer;
        
        // Should we check vendor-id && device-id?
        if (VendorId != 0 && DeviceId != 0) {
            if (ServiceInfo[0] == VendorId && ServiceInfo[1] == DeviceId) {
                return Service;
            }
        }

        // Skip all fixed-vendor ids
        if (ServiceInfo[0] != 0xFFEF) {
            if (ServiceInfo[2] == DeviceClass && 
                ServiceInfo[3] == DeviceSubClass) {
                return Service;
            }
        }
    }
    return NULL;
}

/* PhoenixFileHandler
 * Handles new file-mapping events that occur through unmapped page events. */
OsStatus_t
PhoenixFileHandler(
    _In_Opt_ void*  Context)
{
    MCoreAshFileMappingEvent_t *Event   = (MCoreAshFileMappingEvent_t*)Context;
    MCoreAshFileMapping_t *Mapping      = NULL;
    LargeInteger_t Value;

    Event->Result = OsError;
    foreach(Node, Event->Process->FileMappings) {
        Mapping = (MCoreAshFileMapping_t*)Node->Data;
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
            Event->Result = CreateSystemMemorySpaceMapping(Event->Process->MemorySpace, 
                &Mapping->BufferObject.Dma, &Event->Address, GetSystemMemoryPageSize(), MappingFlags, __MASK);

            // Seek to the file offset, then perform the read of one-page size
            if (SeekFile(Mapping->FileHandle, Value.u.LowPart, Value.u.HighPart) == FsOk && 
                ReadFile(Mapping->FileHandle, Mapping->BufferObject.Handle, GetSystemMemoryPageSize(), &BytesIndex, &BytesRead) == FsOk) {
                Event->Result = OsSuccess;
            }
        }
    }

    // Ignore invalid requests
    SchedulerHandleSignal((uintptr_t*)Event);
    return OsSuccess;
}

/* PhoenixFileMappingEvent
 * Signals a new file-mapping access event to the phoenix process system. */
void
PhoenixFileMappingEvent(
    _In_ MCoreAshFileMappingEvent_t* Event) {
    GcSignal(GcFileHandleId, Event);
}
