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

/* Includes
 * - System */
#include <os/osdefs.h>
#include <ds/collection.h>
#include "cpu.h"
#include "ic.h"

typedef struct _SystemDomain {
    CollectionItem_t            Header;
    SystemCpu_t                 Cpu;

    // Memory and Interrupt Controller must be sharable
    // between domains in this representation.
    // Memory
    SystemInterruptController_t InterruptController;
} SystemDomain_t;

/* InitializePrimaryDomain
 * Initializes the primary domain of the current machine. */
KERNELAPI void KERNELABI
InitializePrimaryDomain(void);

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
