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
 * MollenOS System Component Infrastructure 
 * - Components mostly belong to system domains. This is the representation
 *   of a system domain.
 */

#ifndef __COMPONENT_DOMAIN__
#define __COMPONENT_DOMAIN__

#include <os/osdefs.h>
#include <ds/collection.h>
#include "cpu.h"
#include "memory.h"

// NUMA domains always have their own number of cores and
// their own memory region. They can access all memory but
// there is penalty to not accessing local memory instead of 
// accessing foreign memory.
typedef struct _SystemDomain {
    CollectionItem_t            Header;
    UUId_t                      Id;
    SystemCpu_t                 CoreGroup;
    SystemMemory_t              Memory;
    SystemMemorySpace_t         SystemSpace;
} SystemDomain_t;

/* CreateNumaDomain
 * Creates a new domain with the given parameters and configuration. */
KERNELAPI OsStatus_t KERNELABI
CreateNumaDomain(
    _In_  UUId_t            DomainId,
    _In_  int               NumberOfCores,
    _In_  uintptr_t         MemoryRangeStart, 
    _In_  uintptr_t         MemoryRangeLength,
    _Out_ SystemDomain_t**  Domain);

/* GetCurrentDomain
 * Retrieves a pointer for the current domain. The current domain
 * is the domain that the calling cpu is bound to. */
KERNELAPI SystemDomain_t* KERNELABI
GetCurrentDomain(void);

/* GetDomains
 * Retrieves the collection that contains all current domains. */
KERNELAPI Collection_t* KERNELABI
GetDomains(void);

#endif // !__COMPONENT_DOMAIN__
