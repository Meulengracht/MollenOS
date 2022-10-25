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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * MollenOS System Component Infrastructure 
 * - Components mostly belong to system domains. This is the representation
 *   of a system domain.
 */

#include <component/domain.h>
#include <arch/utils.h>
#include <machine.h>

#include "cpu_private.h"

oserr_t
CreateNumaDomain(
        _In_  uuid_t            DomainId,
        _In_  int               NumberOfCores,
        _In_  uintptr_t         MemoryRangeStart,
        _In_  uintptr_t         MemoryRangeLength,
        _Out_ SystemDomain_t**  Domain)
{
    return OS_EOK;
}

SystemDomain_t*
GetCurrentDomain(void)
{
    return NULL;
}

list_t*
GetDomains(void)
{
    return &GetMachine()->SystemDomains;
}

static int
BootDomainCores(
    _In_ int        Index,
    _In_ element_t* Element,
    _In_ void*      Context)
{
    SystemDomain_t* ExcludeDomain = Context;
    SystemDomain_t* Domain        = Element->value;
    if (Domain != ExcludeDomain) {
        StartApplicationCore(Domain->CoreGroup.Cores);
    }
    return 0;
}

void
CpuEnableMultiProcessorMode(void)
{
    SystemDomain_t*  CurrentDomain = GetCurrentDomain();
    SystemCpuCore_t* Iter;

    // Boot all cores in our own domain, then boot the initial core
    // for all the other domains, they will boot up their own domains.
    list_enumerate(GetDomains(), BootDomainCores, CurrentDomain);
    if (CurrentDomain != NULL) {
        // Don't ever include ourself
        Iter = CurrentDomain->CoreGroup.Cores->Link;
    }
    else {
        // No domains in system - boot all cores except ourself
        Iter = GetMachine()->Processor.Cores->Link;
    }
    
    while (Iter) {
        StartApplicationCore(Iter);
        Iter = Iter->Link;
    }
}
