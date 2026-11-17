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
 */

#include "iort-private.h"
#include <heap.h>

static ArmIortCatalogue_t* g_iortCatalogue;
static oserr_t             g_iortStatus = OS_ENOENT;
static bool                g_iortInitialized;

oserr_t
ArmIortInitialize(void)
{
    // Called once on the boot CPU before secondary cores start or PCI buses are
    // scanned, so no lock is needed. Any failure is cached too, so later
    // callers get the same error instead of parsing the table again.
    if (!g_iortInitialized) {
        g_iortStatus = ArmIortCreate(&g_iortCatalogue);
        g_iortInitialized = true;
    }
    return g_iortStatus;
}

oserr_t
ArmIortGetCatalogue(
    _Out_ const ArmIortCatalogue_t** catalogueOut)
{
    if (catalogueOut == NULL) {
        return OS_EINVALPARAMS;
    }

    if (g_iortStatus != OS_EOK) {
        return g_iortStatus;
    }

    *catalogueOut = g_iortCatalogue;
    return OS_EOK;
}

uint32_t
ArmIortGetNodeCount(
    _In_ const ArmIortCatalogue_t* catalogue)
{
    return catalogue == NULL ? 0 : catalogue->NodeCount;
}

const ArmIortNode_t*
ArmIortGetNode(
    _In_ const ArmIortCatalogue_t* catalogue,
    _In_ uint32_t                  index)
{
    if (catalogue == NULL || index >= catalogue->NodeCount) {
        return NULL;
    }
    return &catalogue->Nodes[index];
}

void
ArmIortDestroy(
    _InOut_ ArmIortCatalogue_t** catalogue)
{
    if (catalogue == NULL || *catalogue == NULL) {
        return;
    }
    
    // Mappings sits inline inside the catalogue struct, so only the per-node
    // heap allocations need individual frees.
    for (uint32_t i = 0; i < (*catalogue)->NodeCount; i++) {
        const ArmIortNode_t* node = &(*catalogue)->Nodes[i];

        kfree((void*)node->AcpiPath);
        kfree((void*)node->Smmu);
        kfree((void*)node->ItsIdentifiers);
    }
    kfree(*catalogue);
    *catalogue = NULL;
}
