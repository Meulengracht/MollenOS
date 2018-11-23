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
#ifndef __VALI_PHOENIX_H__
#define __VALI_PHOENIX_H__

#include <os/osdefs.h>
#include <ds/collection.h>
#include <memorybuffer.h>

typedef struct _SystemProcess SystemProcess_t;
typedef struct _MString MString_t;

#define PHOENIX_MAX_ALIASES     32
#define PHOENIX_ALIAS_BASE		0x8000

// File Mapping Support
// Provides file-mapping support for processes.
typedef struct _MCoreAshFileMapping {
    CollectionItem_t    Header;
    DmaBuffer_t         BufferObject;
    UUId_t              FileHandle;
    uint64_t            FileBlock;
    uint64_t            BlockOffset;
    size_t              Length;
    Flags_t             Flags;
} MCoreAshFileMapping_t;

/* MCoreAshFileMappingEvent
 * Descripes a file mapping access event. */
typedef struct _MCoreAshFileMappingEvent {
    SystemProcess_t*    Process;
    uintptr_t           Address;
    OsStatus_t          Result;
} MCoreAshFileMappingEvent_t;

/* InitializePhoenix
 * Initializes the process and server manager. Keeps track of registered
 * alias's and handles file mapped events. */
KERNELAPI void KERNELABI
InitializePhoenix(void);

/* CreateService
 * Creates a new service by the service identification, this in turns call CreateProcess. */
KERNELAPI OsStatus_t KERNELABI
CreateService(
    _In_ MString_t* Path,
    _In_ DevInfo_t  VendorId,
    _In_ DevInfo_t  DeviceId,
    _In_ DevInfo_t  DeviceClass,
    _In_ DevInfo_t  DeviceSubClass);

/* SetProcessAlias
 * Allows a server to register an alias for its id
 * which means that id (must be above SERVER_ALIAS_BASE)
 * will always refer the calling process */
KERNELAPI OsStatus_t KERNELABI
SetProcessAlias(
    _In_ UUId_t Handle,
    _In_ UUId_t Alias);

/* IsProcessAlias
 * Checks the process handle owns the given alias. If it does not, it returns
 * OsError, otherwise OsSuccess. */
KERNELAPI OsStatus_t KERNELABI
IsProcessAlias(
    _In_ UUId_t Handle,
    _In_ UUId_t Alias);

/* GetProcessHandleByAlias
 * Checks if the given process-id has an registered alias.
 * If it has, the given process-id will be overwritten. */
KERNELAPI OsStatus_t KERNELABI
GetProcessHandleByAlias(
    _InOut_ UUId_t*     Alias);

/* GetServiceByIdentification
 * Retrieves a running service by driver-information to avoid spawning multiple services */
KERNELAPI SystemProcess_t* KERNELABI
GetServiceByIdentification(
    _In_  DevInfo_t VendorId,
    _In_  DevInfo_t DeviceId,
    _In_  DevInfo_t DeviceClass,
    _In_  DevInfo_t DeviceSubClass,
    _Out_ UUId_t*   ServiceHandle);

/* PhoenixFileMappingEvent
 * Signals a new file-mapping access event to the phoenix process system. */
KERNELAPI void KERNELABI
PhoenixFileMappingEvent(
    _In_ MCoreAshFileMappingEvent_t* Event);

#endif //!__VALI_PHOENIX_H__
