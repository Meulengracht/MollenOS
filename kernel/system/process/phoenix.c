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
 */
#define __MODULE "PCIF"
#define __TRACE

/* Includes 
 * - System */
#include <system/utils.h>
#include <process/phoenix.h>
#include <process/process.h>
#include <process/server.h>
#include <garbagecollector.h>
#include <scheduler.h>
#include <threading.h>
#include <debug.h>
#include <heap.h>

/* Includes
 * C-Library */
#include <os/file.h>
#include <ds/collection.h>
#include <ds/mstring.h>
#include <stddef.h>
#include <string.h>

/* Prototypes 
 * They are defined later down this file */
int
PhoenixEventHandler(
    _In_Opt_ void *UserData,
    _In_ MCoreEvent_t *Event);
OsStatus_t
PhoenixReapAsh(
    _In_Opt_ void *UserData);
OsStatus_t
PhoenixFileHandler(
    _In_Opt_ void *UserData);

/* Globals 
 * State-keeping and data-storage */
static MCoreEventHandler_t *EventHandler    = NULL;
static Collection_t *Processes              = NULL;
static CriticalSection_t ProcessLock;
static UUId_t *AliasMap                     = NULL;
static UUId_t GcHandlerId                   = 0;
static UUId_t GcFileHandleId                = 0;
static UUId_t ProcessIdGenerator            = 0;
CriticalSection_t LoaderLock;

/* PhoenixInitialize
 * Initialize the Phoenix environment and 
 * start the event-handler loop, it handles all requests 
 * and nothing happens if it isn't started */
void
PhoenixInitialize(void)
{
	// Variables
	int i;

	// Debug
	TRACE("Initializing environment and event handler");

	// Initialize Globals
	ProcessIdGenerator = 1;
	Processes = CollectionCreate(KeyInteger);
    GcHandlerId = GcRegister(PhoenixReapAsh);
    GcFileHandleId = GcRegister(PhoenixFileHandler);
    CriticalSectionConstruct(&ProcessLock, CRITICALSECTION_PLAIN);
    CriticalSectionConstruct(&LoaderLock, CRITICALSECTION_REENTRANCY);

	// Initialize the global alias map
	AliasMap = (UUId_t*)kmalloc(sizeof(UUId_t) * PHOENIX_MAX_ASHES);
	for (i = 0; i < PHOENIX_MAX_ASHES; i++) {
		AliasMap[i] = UUID_INVALID;
	}

	// Create event handler
	EventHandler = EventInit("phoenix", PhoenixEventHandler, NULL);
}

/* PhoenixCreateRequest
 * Creates and queues up a new request for the process-manager. */
void
PhoenixCreateRequest(
    _In_ MCorePhoenixRequest_t *Request) {
	EventCreate(EventHandler, &Request->Base);
}

/* PhoenixWaitRequest
 * Wait for a request to finish. A timeout can be specified. */
void
PhoenixWaitRequest(
    _In_ MCorePhoenixRequest_t *Request,
    _In_ size_t Timeout) {
	EventWait(&Request->Base, Timeout);
}

/* PhoenixRegisterAlias
 * Allows a server to register an alias for its id
 * which means that id (must be above SERVER_ALIAS_BASE)
 * will always refer the calling process */
OsStatus_t
PhoenixRegisterAlias(
	_In_ MCoreAsh_t *Ash, 
	_In_ UUId_t Alias)
{
	// Sanitize both the server and alias 
	if (Ash == NULL
		|| (Alias < PHOENIX_ALIAS_BASE)
		|| AliasMap[Alias - PHOENIX_ALIAS_BASE] != UUID_INVALID) {
		LogFatal("PHNX", "Failed to register alias 0x%x for ash %u (0x%x - %u)",
			Alias, (Ash == NULL ? UUID_INVALID : Ash->Id),
			AliasMap[Alias - PHOENIX_ALIAS_BASE], Alias - PHOENIX_ALIAS_BASE);
		return OsError;
	}

	// Register
	AliasMap[Alias - PHOENIX_ALIAS_BASE] = Ash->Id;
	return OsSuccess;
}

/* PhoenixUpdateAlias
 * Checks if the given process-id has an registered alias.
 * If it has, the given process-id will be overwritten. */
OsStatus_t
PhoenixUpdateAlias(
    _InOut_ UUId_t *AshId)
{
    if (*AshId >= PHOENIX_ALIAS_BASE
		&& *AshId < (PHOENIX_ALIAS_BASE + PHOENIX_MAX_ASHES)) {
        *AshId = AliasMap[*AshId - PHOENIX_ALIAS_BASE];
        return OsSuccess;
    }
    return OsError;
}

/* PhoenixGetNextId 
 * Retrieves the next process-id. */
UUId_t
PhoenixGetNextId(void)
{
    return ProcessIdGenerator++;
}

/* PhoenixGetAsh (@interrupt_context)
 * This function looks up a ash structure by the given id */
MCoreAsh_t*
PhoenixGetAsh(
    _In_ UUId_t AshId)
{
    // Variables
    CollectionItem_t *Node  = NULL;
    MCoreAsh_t *Result      = NULL;
    UUId_t CurrentCpu       = UUID_INVALID;

    // If we pass invalid get the current
    if (AshId == UUID_INVALID) {
        CurrentCpu = CpuGetCurrentId();
        if (ThreadingGetCurrentThread(CurrentCpu) != NULL) {
            AshId = ThreadingGetCurrentThread(CurrentCpu)->AshId;
        }
        else {
            return NULL;
        }
    }

    // Still none?
    if (AshId == UUID_INVALID) {
        return NULL;
    }

    // Now we can sanitize the extra stuff, like alias
    PhoenixUpdateAlias(&AshId);

    // Iterate the list for ash-id
    _foreach(Node, Processes) {
        MCoreAsh_t *Ash = (MCoreAsh_t*)Node->Data;
        if (Ash->Id == AshId) {
            Result = Ash;
            break;
        }
    }

    // We didn't find it
    return Result;
}

/* GetServerByDriver
 * Retrieves a running server by driver-information
 * to avoid spawning multiple servers */
MCoreServer_t*
PhoenixGetServerByDriver(
    _In_ DevInfo_t VendorId,
    _In_ DevInfo_t DeviceId,
    _In_ DevInfo_t DeviceClass,
    _In_ DevInfo_t DeviceSubClass)
{
	foreach(pNode, Processes) {
		MCoreAsh_t *Ash = (MCoreAsh_t*)pNode->Data;
		if (Ash->Type == AshServer) {
			MCoreServer_t *Server = (MCoreServer_t*)Ash;

            // Should we check vendor-id && device-id?
            if (VendorId != 0 && DeviceId != 0) {
                if (Server->VendorId == VendorId
                    && Server->DeviceId == DeviceId) {
                    return Server;
                }
            }

            // Skip all fixed-vendor ids
            if (Server->VendorId != 0xFFEF) {
                if (Server->DeviceClass == DeviceClass
                    && Server->DeviceSubClass == DeviceSubClass) {
                    return Server;
                }
            }
		}
	}
	return NULL;
}

/* PhoenixRegisterAsh
 * Registers a new ash by adding it to the process-list */
OsStatus_t
PhoenixRegisterAsh(
    _In_ MCoreAsh_t *Ash)
{
    // Variables
    DataKey_t Key;
    
	// Modifications to process-list are locked
	Key.Value = (int)Ash->Id;
    CriticalSectionEnter(&ProcessLock);
	CollectionAppend(Processes, CollectionCreateNode(Key, Ash));
    CriticalSectionLeave(&ProcessLock);
    return OsSuccess;
}

/* PhoenixTerminateAsh
 * This marks an ash for termination by taking it out
 * of rotation and adding it to the cleanup list */
void
PhoenixTerminateAsh(
    _In_ MCoreAsh_t*    Ash,
    _In_ int            ExitCode,
    _In_ int            TerminateDetachedThreads,
    _In_ int            TerminateInstantly)
{
    // Variables
    int LeftoverThreads = 0;
    DataKey_t Key;

    // Update it's return code
    Ash->Code = ExitCode;

    // Kill it's threads
    LeftoverThreads = ThreadingTerminateAshThreads(Ash->Id, 
        TerminateDetachedThreads, TerminateInstantly);
    if (LeftoverThreads != 0) {
        return;
    }

    // To modify list is locked operation
    Key.Value = (int)Ash->Id;
    CriticalSectionEnter(&ProcessLock);
	CollectionRemoveByKey(Processes, Key);
    CriticalSectionLeave(&ProcessLock);

	// Alert GC
	SchedulerHandleSignalAll((uintptr_t*)Ash);
	GcSignal(GcHandlerId, Ash);
}

/* PhoenixReapAsh
 * This function cleans up processes and
 * ashes and servers that might be queued up for
 * destruction, they can't handle all their cleanup themselves */
OsStatus_t
PhoenixReapAsh(
    _In_Opt_ void *UserData)
{
	// Instantiate the base-pointer
	MCoreAsh_t *Ash = (MCoreAsh_t*)UserData;

	// Clean up
	if (Ash->Type == AshBase) {
		PhoenixCleanupAsh(Ash);
	}
	else if (Ash->Type == AshProcess) {
		PhoenixCleanupProcess((MCoreProcess_t*)Ash);
	}
	else {
		//??
		return OsError;
	}
	return OsSuccess;
}

/* PhoenixFileHandler
 * Handles new file-mapping events that occur through unmapped page events. */
OsStatus_t
PhoenixFileHandler(
    _In_Opt_ void *UserData)
{
    // Variables
	MCoreAshFileMappingEvent_t *Event   = (MCoreAshFileMappingEvent_t*)UserData;
    MCoreAshFileMapping_t *Mapping      = NULL;

    // Set default response
    Event->Result = OsError;

    // Iterate file-mappings
    foreach(Node, Event->Ash->FileMappings) {
        Mapping = (MCoreAshFileMapping_t*)Node->Data;
        if (ISINRANGE(Event->Address, Mapping->VirtualBase, (Mapping->VirtualBase + Mapping->Length) - 1)) {
            uintptr_t Block = 0;
            size_t BytesIndex = 0;
            size_t BytesRead = 0;
            FATAL(FATAL_SCOPE_KERNEL, "Finish implementation of file mappings. They are obviously used now");
            AddressSpaceMap(Event->Ash->AddressSpace, &Block, 
                (VirtualAddress_t*)(Event->Address & (AddressSpaceGetPageSize() - 1)), 
                AddressSpaceGetPageSize(), ASPACE_FLAG_APPLICATION | ASPACE_FLAG_SUPPLIEDVIRTUAL, __MASK);
            if (SeekFile(Mapping->FileHandle, 0, 0) == FsOk && 
                ReadFile(Mapping->FileHandle, Mapping->TransferObject, &BytesIndex, &BytesRead) == FsOk) {
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

/* PhoenixEventHandler
 * This routine is invoked every-time there is any
 * request pending for us */
int
PhoenixEventHandler(
    _In_Opt_ void *UserData,
    _In_ MCoreEvent_t *Event)
{
	// Variables
	MCorePhoenixRequest_t *Request = NULL;

	// Unused
	_CRT_UNUSED(UserData);

	// Instantiate pointers
	Request = (MCorePhoenixRequest_t*)Event;
	switch (Request->Base.Type) {
		case AshSpawnProcess:
		case AshSpawnServer: {
			TRACE("Spawning %s", MStringRaw(Request->Path));

			if (Request->Base.Type == AshSpawnServer) {
				Request->AshId = PhoenixCreateServer(Request->Path);
			}
			else {
				Request->AshId = PhoenixCreateProcess(
                    Request->Path, &Request->StartupInformation);
			}

			// Sanitize result
			if (Request->AshId != UUID_INVALID) {
                Request->Base.State = EventOk;      
            }
			else {
				Request->Base.State = EventFailed;
            }
        } break;
        
		case AshKill: {
			MCoreAsh_t *Ash = PhoenixGetAsh(Request->AshId);
			if (Ash != NULL) {
				PhoenixTerminateAsh(Ash, 0, 1, 1);
			}
			else {
				Request->Base.State = EventFailed;
			}
		} break;

		default: {
			ERROR("Unhandled Event %u", (size_t)Request->Base.Type);
		} break;
	}

	// Handle cleanup
	if (Request->Base.Cleanup != 0) {
		if (Request->Path != NULL) {
			MStringDestroy(Request->Path);
        }
	}
	return 0;
}
