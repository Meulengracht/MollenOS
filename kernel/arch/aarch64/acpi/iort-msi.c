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
#include "../common/private.h"

// Finds the mapping of a node that covers the input ID and stores it in
// *mappingOut. Callers pass NULL in *mappingOut to start a lookup. If it is
// already set when a mapping matches, two mappings cover the same ID and the
// table is ambiguous, so OS_EINVALPARAMS is returned. *mappingOut stays NULL
// if nothing matches.
static oserr_t
__FindMapping(
    _In_    const ArmIortNode_t*     node,
    _In_    uint32_t                 input,
    _InOut_ const ArmIortMapping_t** mappingOut)
{
    for (uint32_t i = 0; i < node->MappingCount; i++) {
        const ArmIortMapping_t* mapping = &node->Mappings[i];

        // Skip mappings reserved for an SMMU's or PMCG's own internal
        // interrupts. Those ignore the incoming device ID and would falsely
        // match every downstream device.
        if (mapping->IsOwnInterruptMapping) {
            continue;
        }

        if (!mapping->MapsAllInputsToOne && (input < mapping->InputFirst || input > mapping->InputLast)) {
            continue;
        }

        // Two mappings covering the same ID on one node means the firmware
        // table is ambiguous.
        if (*mappingOut != NULL) {
            return OS_EINVALPARAMS;
        }

        *mappingOut = mapping;
    }
    return OS_EOK;
}

static uint32_t
__TranslateId(
    _In_ const ArmIortMapping_t* mapping,
    _In_ uint32_t                input)
{
    // The parser already checked that every output ID in this range fits in
    // 32 bits.
    return mapping->OutputFirst + (mapping->MapsAllInputsToOne ? 0 : input - mapping->InputFirst);
}

// Collects the mapping of a PCI requester ID from every PCI root node in the
// segment. Multiple root nodes can share one segment if their requester ID
// ranges stay disjoint, so every root in the segment is scanned. *mappingOut
// stays NULL if no root covers the ID.
static oserr_t
__FindPciRootMapping(
    _In_    const ArmIortCatalogue_t* catalogue,
    _In_    uint32_t                  segment,
    _In_    uint16_t                  requesterId,
    _InOut_ const ArmIortMapping_t**  mappingOut)
{
    for (uint32_t i = 0; i < ArmIortGetNodeCount(catalogue); i++) {
        const ArmIortNode_t* node = ArmIortGetNode(catalogue, i);
        oserr_t              status;

        if (node->Type != ArmIortPciRoot || node->Segment != segment) {
            continue;
        }

        status = __FindMapping(node, requesterId, mappingOut);
        if (status != OS_EOK) {
            return status;
        }
    }
    return OS_EOK;
}

oserr_t
ArmIortResolvePciMsi(
    _In_  const ArmIortCatalogue_t* catalogue,
    _In_  uint32_t                  segment,
    _In_  uint16_t                  requesterId,
    _Out_ ArmIortMsiRoute_t*         routeOut)
{
    const ArmIortMapping_t* mapping = NULL;
    const ArmIortNode_t*    target;
    ArmIortMsiRoute_t       route = {0};
    uint32_t                deviceId;
    oserr_t                 status;

    if (catalogue == NULL || routeOut == NULL) {
        return OS_EINVALPARAMS;
    }

    status = __FindPciRootMapping(catalogue, segment, requesterId, &mapping);
    if (status != OS_EOK) {
        return status;
    }
    if (mapping == NULL) {
        return OS_ENOENT;
    }

    deviceId = __TranslateId(mapping, requesterId);
    target = ArmIortGetNode(catalogue, mapping->TargetIndex);
    if (target->Type == ArmIortSmmu || target->Type == ArmIortSmmuV3) {
        mapping = NULL;

        status = __FindMapping(target, deviceId, &mapping);
        if (status != OS_EOK) {
            return status;
        }

        if (mapping == NULL) {
            return OS_ENOENT;
        }

        deviceId = __TranslateId(mapping, deviceId);
        target = ArmIortGetNode(catalogue, mapping->TargetIndex);
    }

    // After passing through at most one SMMU, the path has to land on an ITS
    // group. Returning the group lets the caller choose a working controller
    // from it later.
    if (target->Type != ArmIortItsGroup || target->ItsCount == 0) {
        return OS_ENOTSUPPORTED;
    }

    route.ItsGroup = target;
    route.DeviceId = deviceId;
    
    *routeOut = route;
    return OS_EOK;
}

oserr_t
ArmIortGetItsController(
    _In_  const ArmIortNode_t*   group,
    _In_  uint32_t               identifier,
    _Out_ Arm64ItsDescription_t* controllerOut)
{
    const struct Arm64GicDescription* gic = &g_arm64Platform.Gic;
    bool                              member = false;

    if (group == NULL || controllerOut == NULL || group->Type != ArmIortItsGroup) {
        return OS_EINVALPARAMS;
    }

    for (uint32_t i = 0; i < group->ItsCount; i++) {
        if (group->ItsIdentifiers[i] == identifier) {
            member = true;
            break;
        }
    }
    if (!member) {
        return OS_ENOENT;
    }

    for (unsigned int i = 0; i < gic->ItsCount; i++) {
        if (gic->ItsControllers[i].Identifier == identifier) {
            *controllerOut = gic->ItsControllers[i];
            return OS_EOK;
        }
    }
    return OS_ENOENT;
}
