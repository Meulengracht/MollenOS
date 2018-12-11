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

#ifndef __MODULE_MANAGER_INTERFACE__
#define __MODULE_MANAGER_INTERFACE__

#include <os/osdefs.h>
#include <ds/mstring.h>
#include <ds/collection.h>
#include <modules/module.h>
#include <memorybuffer.h>
#include <memoryspace.h>

typedef struct _SystemFileMapping {
    CollectionItem_t    Header;
    DmaBuffer_t         BufferObject;
    UUId_t              FileHandle;
    uint64_t            FileBlock;
    uint64_t            BlockOffset;
    size_t              Length;
    Flags_t             Flags;
} SystemFileMapping_t;

typedef struct _SystemFileMappingEvent {
    SystemMemorySpace_t* MemorySpace;
    uintptr_t            Address;
    OsStatus_t           Result;
} SystemFileMappingEvent_t;

/* InitializeModuleManager
 * Initializes the static storage needed for the module manager, and registers a garbage collector. */
KERNELAPI void KERNELABI
InitializeModuleManager(void);

/* RegisterModule
 * Registers a new system module resource that is then available for the operating system
 * to use. The resource can be can be either an driver, service or a generic file. */
KERNELAPI OsStatus_t KERNELABI
RegisterModule(
    _In_ const char*        Path,
    _In_ const void*        Data,
    _In_ size_t             Length,
    _In_ SystemModuleType_t Type,
    _In_ DevInfo_t          VendorId,
    _In_ DevInfo_t          DeviceId,
    _In_ DevInfo_t          DeviceClass,
    _In_ DevInfo_t          DeviceSubclass);

/* SpawnServices
 * Loads all system services present in the initial ramdisk. */
KERNELAPI void KERNELABI
SpawnServices(void);

/* GetModuleDataByPath
 * Retrieve a pointer to the file-buffer and its length based on 
 * the given <rd:/> path */
KERNELAPI OsStatus_t KERNELABI
GetModuleDataByPath(
    _In_  MString_t* Path, 
    _Out_ void**     Buffer, 
    _Out_ size_t*    Length);

/* GetGenericDeviceModule
 * Resolves a device module by it's generic type and subtype instead of a device specific
 * module that is resolved by vendor id and product id. */
KERNELAPI SystemModule_t* KERNELABI
GetGenericDeviceModule(
    _In_ DevInfo_t DeviceClass, 
    _In_ DevInfo_t DeviceSubclass);

/* GetSpecificDeviceModule
 * Resolves a specific device module that is specified by both vendor id and product id. */
KERNELAPI SystemModule_t* KERNELABI
GetSpecificDeviceModule(
    _In_ DevInfo_t VendorId,
    _In_ DevInfo_t DeviceId);

/* GetModule
 * Retrieves an existing module instance based on the identification markers. */
KERNELAPI SystemModule_t* KERNELABI
GetModule(
    _In_  DevInfo_t VendorId,
    _In_  DevInfo_t DeviceId,
    _In_  DevInfo_t DeviceClass,
    _In_  DevInfo_t DeviceSubclass);

/* GetCurrentModule
 * Retrieves the module that belongs to the calling thread. */
KERNELAPI SystemModule_t* KERNELABI
GetCurrentModule(void);

/* SetModuleAlias
 * Sets the alias for the currently running module. Only the primary thread is allowed to perform
 * this call. */
KERNELAPI OsStatus_t KERNELABI
SetModuleAlias(
    _In_ UUId_t Alias);

/* GetModuleByAlias
 * Retrieves a running service/module by it's registered alias. This is usually done
 * by system services to be contactable by applications. */
KERNELAPI SystemModule_t* KERNELABI
GetModuleByAlias(
    _In_ UUId_t Alias);

/* RegisterFileMappingEvent
 * Signals a new file-mapping access event to the system. */
KERNELAPI void KERNELABI
RegisterFileMappingEvent(
    _In_ SystemFileMappingEvent_t* Event);

#endif //!__MODULE_MANAGER_INTERFACE__
