/**
 * Copyright, Philip Meulengracht
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
 * Capabilities Interface
 * - Defines the capabilities that can be granted to processes. Mostly this
 *   applies to privileged operations within the kernel, that drivers or 
 *   services may require.
 */


#include <debug.h>
#include <capabilities.h>
#include <handle.h>
#include <memoryspace.h>
#include "memory/private.h"

oserr_t
CapabilityGrant(
    _In_ Capabilities_t capabilities,
    _In_ uuid_t         memorySpaceHandle)
{
    MemorySpace_t*    memorySpace;
    struct MSContext* context;
    oserr_t           oserr;

    if ((uint32_t)capabilities & ~(uint32_t)CAPABILITY_ALL) {
        ERROR("CapabilityGrant: unknown capability bits 0x%x", capabilities);
        return OS_EINVALPARAMS;
    }

    // Ensure the current caller can actually grant others capabilities
    oserr = CapabilityVerify(CAPABILITY_GRANT_CAPABILITY);
    if (oserr != OS_EOK) {
        return oserr;
    }

    // Get a temporary reference on the memory space
    oserr = AcquireHandleOfType(memorySpaceHandle, HandleTypeMemorySpace, (void**)&memorySpace);
    if (oserr != OS_EOK) {
        return oserr;
    }

    // Grant the specified capabilities to the memory space.
    context = __MSContextGet(memorySpace);
    if (context == NULL) {
        DestroyHandle(memorySpaceHandle);
        return OS_EINVALPARAMS;
    }

    context->Security.Capabilities |= capabilities;
    TRACE("CapabilityGrant: principal=%llu capabilities=0x%x",
          (unsigned long long)context->Security.Handle, capabilities);

    DestroyHandle(memorySpaceHandle);
    return OS_EOK;
}

oserr_t
CapabilityVerify(
    _In_ Capabilities_t capabilities)
{
    MemorySpace_t*    memorySpace = GetCurrentMemorySpace();
    struct MSContext* context;
    
    if (memorySpace == NULL || memorySpace->GroupHandle == UUID_INVALID) {
        // Only kernel threads or early system init code can have no context.
        // In this case, we assume the caller has all capabilities.
        return OS_EOK;
    }

    context = __MSContextGet(memorySpace);
    
    // For the check to pass, all capabilities requested must hold
    if ((context->Security.Capabilities & capabilities) == capabilities) {
        return OS_EOK;
    }

    // Otherwise permission denied
    ERROR("CapabilityVerify missing capabilities 0x%x", capabilities);
    return OS_EPERMISSIONS;
}
