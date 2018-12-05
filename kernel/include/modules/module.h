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
#include <ds/collection.h>

typedef struct _PeExecutable PeExecutable_t;
typedef struct _SystemPipe SystemPipe_t;
typedef struct _MString MString_t;


#define MODULE_FILESYSTEM       0x01010101
#define MODULE_BUS              0x02020202
#define MODULE_INITIAL_STACK    0x1000
#define MODULE_MAX_STACK        (4 << 20)

typedef enum _SystemModuleType {
    FileResource = 0,
    ModuleResource,
    ServiceResource
} SystemModuleType_t;

typedef struct _SystemModule {
    CollectionItem_t ListHeader;
    MString_t*       Path;
    const void*      Data;
    size_t           Length;

    // Used by Module/Service type
    clock_t          StartedAt;
    MString_t*       WorkingDirectory;
    MString_t*       BaseDirectory;
    UUId_t           PrimaryThreadId;
    UUId_t           Alias;
    DevInfo_t        VendorId;
    DevInfo_t        DeviceId;
    DevInfo_t        DeviceClass;
    DevInfo_t        DeviceSubclass;
    PeExecutable_t*  Executable;
    SystemPipe_t*    Rpc;
} SystemModule_t;

/* SpawnModule 
 * Loads the module given into memory, creates a new bootstrap thread and executes the module. */
KERNELAPI OsStatus_t KERNELABI
SpawnModule(
    _In_  SystemModule_t* Module,
    _In_  const void*     Data,
    _In_  size_t          Length);

#endif //!__MODULE_INTERFACE__
