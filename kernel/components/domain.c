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
#include <component/domain.h>
#include <arch/utils.h>
#include <machine.h>

/* CreateNumaDomain
 * Creates a new domain with the given parameters and configuration. */
OsStatus_t
CreateNumaDomain(
    _In_  UUId_t            DomainId,
    _In_  int               NumberOfCores,
    _In_  uintptr_t         MemoryRangeStart, 
    _In_  uintptr_t         MemoryRangeLength,
    _Out_ SystemDomain_t**  Domain)
{
    return OsSuccess;
}

/* GetCurrentDomain
 * Retrieves a pointer for the current domain. The current domain
 * is the domain that the calling cpu is bound to. */
SystemDomain_t*
GetCurrentDomain(void)
{
    return NULL;
}

/* GetDomains
 * Retrieves the collection that contains all current domains. */
Collection_t*
GetDomains(void)
{
    return &GetMachine()->SystemDomains;
}

void
EnableMultiProcessoringMode(void)
{
    SystemDomain_t *CurrentDomain = GetCurrentDomain();
    SystemDomain_t *Domain;
    int i;

    // Boot all cores in our own domain, then boot the initial core
    // for all the other domains, they will boot up their own domains.
    foreach (DomainNode, GetDomains()) {
        Domain = (SystemDomain_t*)DomainNode->Data;
        if (Domain != CurrentDomain) {
            StartApplicationCore(&Domain->CoreGroup.PrimaryCore);
        }
    }

    if (CurrentDomain != NULL) {
        // Don't ever include ourself
        for (i = 0; i < (CurrentDomain->CoreGroup.NumberOfCores - 1); i++) {
            StartApplicationCore(&CurrentDomain->CoreGroup.ApplicationCores[i]);
        }
    }
    else {
        // No domains in system - boot all cores except ourself
        for (i = 0; i < (GetMachine()->Processor.NumberOfCores - 1); i++) {
            StartApplicationCore(&GetMachine()->Processor.ApplicationCores[i]);
        }
    }
}
