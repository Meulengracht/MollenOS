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

#ifndef __MODULE_INTERFACE__
#define __MODULE_INTERFACE__

#include <os/osdefs.h>
#include <ds/mstring.h>

#define MODULE_FILESYSTEM       0x01010101
#define MODULE_BUS              0x02020202

typedef enum _SystemModuleType {
    FileResource = 0,
    ModuleResource,
    ServiceResource
} SystemModuleType_t;

typedef struct _SystemModule {
    DevInfo_t          VendorId;
    DevInfo_t          DeviceId;
    DevInfo_t          DeviceClass;
    DevInfo_t          DeviceSubclass;
    MString_t*         Path;
    const void*        Data;
} SystemModule_t;

/* RegisterModule
 * Registers a new system module resource that is then available for the operating system
 * to use. The resource can be can be either an driver, service or a generic file. */
KERNELAPI OsStatus_t KERNELABI
RegisterModule(
    _In_ const char*        Path,
    _In_ const void*        Data,
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

/* ModulesFindGeneric
 * Resolve a 'generic' driver by its device-type and/or
 * its device sub-type, this is generally used if there is no
 * vendor specific driver available for the device. Returns NULL if none is available */
__EXTERN MCoreModule_t*
ModulesFindGeneric(
    _In_ DevInfo_t DeviceType, 
    _In_ DevInfo_t DeviceSubType);

/* ModulesFindSpecific
 * Resolve a specific driver by its vendorid and deviceid 
 * this is to ensure optimal module load. Returns NULL if none is available */
__EXTERN MCoreModule_t*
ModulesFindSpecific(
    _In_ DevInfo_t VendorId, 
    _In_ DevInfo_t DeviceId);

/* ModulesFindString
 * Resolve a module by its name. Returns NULL if none is available */
__EXTERN MCoreModule_t*
ModulesFindString(
    _In_ MString_t* Module);

#endif //!__MODULE_INTERFACE__
