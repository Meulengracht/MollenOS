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
#include <os/types/process.h>
#include <ds/list.h>
#include <time.h>

typedef struct PeExecutable PeExecutable_t;
typedef struct MString MString_t;

typedef enum SystemModuleType {
    FileResource = 0,
    ModuleResource,
    ServiceResource
} SystemModuleType_t;

typedef struct SystemModule {
    element_t   ListHeader;
    UUId_t      Handle;
    MString_t*  Path;
    const void* Data;
    size_t      Length;

    // Used by Module/Service type
    void*           InheritanceBlock;
    size_t          InheritanceBlockLength;
    void*           ArgumentBlock;
    size_t          ArgumentBlockLength;
    clock_t         StartedAt;
    MString_t*      WorkingDirectory;
    MString_t*      BaseDirectory;
    UUId_t          PrimaryThreadId;
    UUId_t          Alias;
    unsigned int       VendorId;
    unsigned int       DeviceId;
    unsigned int       DeviceClass;
    unsigned int       DeviceSubclass;
    PeExecutable_t* Executable;
} SystemModule_t;

/* SpawnModule 
 * Loads the module given into memory, creates a new bootstrap thread and executes the module. */
KERNELAPI OsStatus_t KERNELABI
SpawnModule(
    _In_  SystemModule_t* Module);

#endif //!__MODULE_INTERFACE__
