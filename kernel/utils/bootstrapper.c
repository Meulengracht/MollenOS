/**
 * Copyright 2021, Philip Meulengracht
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#define __TRACE

#include <debug.h>
#include <handle.h>
#include <machine.h>
#include <memoryspace.h>
#include <threading.h>

void
SpawnBootstrapper(void)
{
    uuid_t     memorySpaceHandle;
    uuid_t     threadHandle;
    oserr_t osStatus;
    vaddr_t    codeAddress;
    TRACE("SpawnBootstrapper(base=0x%llx, entry=0x%llx, len=0x%x)",
          GetMachine()->BootInformation.Phoenix.Base,
          GetMachine()->BootInformation.Phoenix.EntryPoint,
          GetMachine()->BootInformation.Phoenix.Length);

    if (!GetMachine()->BootInformation.Phoenix.Length) {
        TRACE("SpawnBootstrapper no bootstrapper present in boot information");
        return;
    }

    // Create a new memoryspace that is application specific
    osStatus = CreateMemorySpace(MEMORY_SPACE_APPLICATION, &memorySpaceHandle);
    if (osStatus != OsOK) {
        ERROR("SpawnBootstrapper failed to create memory space for bootstrapper");
    }

    codeAddress = GetMachine()->MemoryMap.UserCode.Start;

    // Create one big continous mapping that maps in the entire image
    osStatus = MemorySpaceMapContiguous(
            MEMORYSPACE_GET(memorySpaceHandle),
            &codeAddress,
            (paddr_t)GetMachine()->BootInformation.Phoenix.Base,
            GetMachine()->BootInformation.Phoenix.Length,
            MAPPING_COMMIT | MAPPING_USERSPACE | MAPPING_EXECUTABLE | MAPPING_PERSISTENT,
            MAPPING_VIRTUAL_FIXED
    );
    if (osStatus != OsOK) {
        ERROR("SpawnBootstrapper failed to map image into bootstrapper memory space");
    }

    osStatus = ThreadCreate(
            "phoenix.mos",
            (ThreadEntry_t)GetMachine()->BootInformation.Phoenix.EntryPoint,
            NULL,
            THREADING_USERMODE,
            memorySpaceHandle,
            0,
            0,
            &threadHandle
    );
    if (osStatus != OsOK) {
        ERROR("SpawnBootstrapper failed to create thread for bootstrapper");
    }
}
