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
#include <component/cpu.h>

/* Global storage
 * Global static storage/variables for managing domains. */
static SystemDomain_t CurrentDomain = { { { 0 } } };
static Collection_t Domains         = COLLECTION_INIT(KeyInteger);

/* InitializePrimaryDomain
 * Initializes the primary domain of the current machine. */
void 
InitializePrimaryDomain(void)
{
    // Initialize the processor of the machine
    InitializeProcessor(&CurrentDomain.Cpu);
    RegisterPrimaryCore(&CurrentDomain.Cpu);

    // Initialize memory of the domain
    // @todo

    // Initialize the interrupt controller of the domain
    // @todo
}

/* GetCurrentDomain
 * Retrieves a pointer for the current domain. The current domain
 * is the domain that the calling cpu is bound to. */
SystemDomain_t*
GetCurrentDomain(void)
{
    return &CurrentDomain;
}

/* GetDomains
 * Retrieves the collection that contains all current domains. */
Collection_t*
GetDomains(void)
{
    return &Domains;
}
