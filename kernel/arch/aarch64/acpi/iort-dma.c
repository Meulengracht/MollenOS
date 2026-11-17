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

#include "iort.h"

// Finds the one PCI root node of a segment. We store one DMA policy per PCI
// segment. Reject the segment if firmware splits it across multiple root nodes,
// since each root could carry different address limits or cache settings.
static oserr_t
__FindPciRoot(
    _In_  const ArmIortCatalogue_t* catalogue,
    _In_  uint32_t                  segment,
    _Out_ const ArmIortNode_t**     rootOut)
{
    const ArmIortNode_t* root = NULL;

    for (uint32_t i = 0; i < ArmIortGetNodeCount(catalogue); i++) {
        const ArmIortNode_t* node = ArmIortGetNode(catalogue, i);

        if (node->Type != ArmIortPciRoot || node->Segment != segment) {
            continue;
        }

        if (root != NULL) {
            return OS_ENOTSUPPORTED;
        }
        root = node;
    }

    if (root == NULL) {
        return OS_ENOTSUPPORTED;
    }

    *rootOut = root;
    return OS_EOK;
}

// Checks that the mappings of a PCI root send every requester ID straight to
// an ITS group, with no SMMU in between and no IDs left out.
static bool
__MappingsCoverAllRequesterIds(
    _In_ const ArmIortCatalogue_t* catalogue,
    _In_ const ArmIortNode_t*      root)
{
    uint64_t covered = 0;

    for (uint32_t i = 0; i < root->MappingCount; i++) {
        const ArmIortMapping_t* mapping = &root->Mappings[i];
        const ArmIortNode_t*    target = ArmIortGetNode(catalogue, mapping->TargetIndex);

        if (target->Type != ArmIortItsGroup) {
            return false;
        }
        covered += mapping->MapsAllInputsToOne ? UINT16_MAX + 1U : mapping->InputLast - mapping->InputFirst + 1U;
    }

    // PCI requester IDs are 16 bits wide (65,536 IDs total). A mapping with
    // MapsAllInputsToOne set covers all of them at once. Because the parser already rejects
    // overlapping ranges, summing to 65,536 guarantees there are no gaps. A
    // root with no mappings at all is also valid, meaning devices on it don't
    // route through an SMMU or ITS.
    if (root->MappingCount != 0 && covered != UINT16_MAX + 1U) {
        return false;
    }
    return true;
}

oserr_t
ArmIortGetPciDirectLimit(
    _In_  const ArmIortCatalogue_t* catalogue,
    _In_  uint32_t                  segment,
    _Out_ uint64_t*                 addressLimitOut)
{
    const ArmIortNode_t* root;
    oserr_t              status;

    if (catalogue == NULL || addressLimitOut == NULL) {
        return OS_EINVALPARAMS;
    }

    status = __FindPciRoot(catalogue, segment, &root);
    if (status != OS_EOK) {
        return status;
    }

    if (root->DmaStatus != OS_EOK ||
        root->CachePolicy != SHMDeviceCacheCoherent || !root->HardwareCoherent ||
        !root->UsesDeviceMemoryAttributes) {
        return OS_ENOTSUPPORTED;
    }

    if (!__MappingsCoverAllRequesterIds(catalogue, root)) {
        return OS_ENOTSUPPORTED;
    }
    
    *addressLimitOut = root->AddressLimit;
    return OS_EOK;
}
