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
 * ARM SMMU ACPI IORT Discovery
 * Copies SMMU descriptions, reserved device IDs, and routing ranges from
 * a validated IORT catalogue into an ArmSMMUDiscovery_t lookup table.
 */

#include "discovery-private.h"
#include "../acpi/iort.h"
#include <heap.h>
#include <string.h>

/**
 * Copies every supported SMMU description into discovery->Units and records
 * its local unit index in unitIndices[nodeIndex]. Unsupported SMMUs still
 * count toward ARM_SMMU_MAX_UNITS, and leave UINT32_MAX in unitIndices so
 * later route lookups spot and refuse them.
 */
static oserr_t
__AssignUnitIndices(
    _In_  const ArmIortCatalogue_t* catalogue,
    _In_  ArmSMMUDiscovery_t*       discovery,
    _Out_ uint32_t*                 unitIndices)
{
    uint32_t unitCount = 0;

    for (uint32_t i = 0; i < ArmIortGetNodeCount(catalogue); i++) {
        const ArmIortNode_t* node = ArmIortGetNode(catalogue, i);

        if (node->Type != ArmIortSmmu && node->Type != ArmIortSmmuV3) {
            continue;
        }

        if (++unitCount > ARM_SMMU_MAX_UNITS) {
            return OS_ENOTSUPPORTED;
        }

        if (node->Smmu != NULL) {
            unitIndices[i] = discovery->UnitCount;
            discovery->Units[discovery->UnitCount++] = *node->Smmu;
        }
    }
    return OS_EOK;
}

/**
 * Copies the SMMU IDs that firmware reserved for pre-boot DMA from one RMR
 * (Reserved Memory Region) node. Only mappings aimed at a supported SMMU
 * matter here. Any device route matching these IDs will be refused later.
 */
static oserr_t
__AddHeldIds(
    _In_ ArmSMMUDiscovery_t*  discovery,
    _In_ const ArmIortNode_t* node,
    _In_ const uint32_t*      unitIndices)
{
    for (uint32_t i = 0; i < node->MappingCount; i++) {
        const ArmIortMapping_t*   mapping = &node->Mappings[i];
        ArmSMMUFirmwareHeldIds_t* ids;

        if (unitIndices[mapping->TargetIndex] == UINT32_MAX) {
            continue;
        }

        if (discovery->HeldIdCount == ARM_SMMU_MAX_RANGES) {
            return OS_ENOTSUPPORTED;
        }

        // An RMR entry carries its span length via InputFirst..InputLast and
        // places that span at OutputFirst on the target SMMU.
        ids = &discovery->HeldIds[discovery->HeldIdCount++];
        ids->UnitIndex = unitIndices[mapping->TargetIndex];
        ids->First = mapping->OutputFirst;
        ids->Last = mapping->OutputFirst + mapping->InputLast - mapping->InputFirst;
    }
    return OS_EOK;
}

/**
 * Copies all ID mapping rules of one source node into discovery->Ranges.
 * Mappings pointing at an ITS interrupt group get OS_ENOENT because ITS blocks
 * handle interrupts, not DMA. Mappings to an SMMU inherit that SMMU's status,
 * and anything else gets OS_ENOTSUPPORTED. Caller must verify array space first.
 */
static void
__AddRouteRanges(
    _In_ const ArmIortCatalogue_t* catalogue,
    _In_ ArmSMMUDiscovery_t*       discovery,
    _In_ const ArmIortNode_t*      node,
    _In_ const uint32_t*           unitIndices)
{
    for (uint32_t i = 0; i < node->MappingCount; i++) {
        const ArmIortMapping_t* mapping = &node->Mappings[i];
        const ArmIortNode_t*    target = ArmIortGetNode(catalogue, mapping->TargetIndex);
        ArmSMMURouteRange_t*    range = &discovery->Ranges[discovery->RangeCount++];

        range->InputFirst = mapping->InputFirst;
        range->InputLast = mapping->InputLast;
        range->OutputFirst = mapping->OutputFirst;
        range->MapsToOneId = mapping->MapsAllInputsToOne;
        range->UnitIndex = unitIndices[mapping->TargetIndex];
        range->Status = OS_ENOTSUPPORTED;

        if (target->Type == ArmIortItsGroup) {
            range->Status = OS_ENOENT;
        } else if (target->Type == ArmIortSmmu || target->Type == ArmIortSmmuV3) {
            range->Status = target->DmaStatus;
        }
    }
}

/**
 * Adds a PCI host bridge or named ACPI device as a route source along with
 * its mapping ranges. Copies AcpiPath onto the heap so the caller can free the
 * IORT catalogue afterwards. Returns OS_ENOTSUPPORTED if the fixed tables fill up.
 */
static oserr_t
__AddRouteSource(
    _In_ const ArmIortCatalogue_t* catalogue,
    _In_ ArmSMMUDiscovery_t*       discovery,
    _In_ const ArmIortNode_t*      node,
    _In_ const uint32_t*           unitIndices)
{
    ArmSMMUSource_t* source;

    if (discovery->RouteCount == ARM_SMMU_MAX_NODES ||
        node->MappingCount > ARM_SMMU_MAX_RANGES - discovery->RangeCount) {
        return OS_ENOTSUPPORTED;
    }

    // Increment RouteCount before copying AcpiPath so ArmSMMUDiscoveryDestroy
    // frees any partial strings cleanly if a later step fails.
    source = &discovery->Routes[discovery->RouteCount++];
    source->Segment = node->Segment;
    source->Status = node->DmaStatus;
    source->CachePolicy = node->CachePolicy;
    source->AddressLimit = node->AddressLimit;
    source->FirstRange = discovery->RangeCount;
    source->RangeCount = node->MappingCount;
    __AddRouteRanges(catalogue, discovery, node, unitIndices);

    if (node->AcpiPath != NULL) {
        size_t length = strlen(node->AcpiPath) + 1;

        source->AcpiPath = kmalloc(length);
        if (source->AcpiPath == NULL) {
            return OS_EOOM;
        }
        memcpy(source->AcpiPath, node->AcpiPath, length);
    }
    return OS_EOK;
}

oserr_t
ArmSMMUDiscoverIort(
    _In_    const ArmIortCatalogue_t* catalogue,
    _InOut_ ArmSMMUDiscovery_t**      discoveryOut)
{
    ArmSMMUDiscovery_t* discovery;
    uint32_t            unitIndices[ARM_SMMU_MAX_NODES];
    oserr_t             status;

    if (catalogue == NULL || discoveryOut == NULL) {
        return OS_EINVALPARAMS;
    } 

    if (*discoveryOut != NULL) {
        return OS_EBUSY;
    }

    if (ArmIortGetNodeCount(catalogue) > ARM_SMMU_MAX_NODES) {
        return OS_ENOTSUPPORTED;
    }

    discovery = kmalloc(sizeof(ArmSMMUDiscovery_t));
    if (discovery == NULL) {
        return OS_EOOM;
    }

    // Initialize every unit index to UINT32_MAX (0xFFFFFFFF) so unsupported or
    // non-SMMU nodes are easy to spot during the second pass.
    memset(discovery, 0, sizeof(ArmSMMUDiscovery_t));
    memset(unitIndices, 0xFF, sizeof(unitIndices));

    // Pass 1: assign local unit indices to all SMMUs before any source or RMR
    // tries to reference them. These indices belong to discovery, not IORT.
    status = __AssignUnitIndices(catalogue, discovery, unitIndices);
    if (status != OS_EOK) {
        goto cleanup;
    }

    // Pass 2: record firmware-reserved IDs and device routing sources.
    for (uint32_t i = 0; i < ArmIortGetNodeCount(catalogue); i++) {
        const ArmIortNode_t* node = ArmIortGetNode(catalogue, i);

        if (node->Type == ArmIortRmr) {
            status = __AddHeldIds(discovery, node, unitIndices);
        } else if (node->Type == ArmIortPciRoot || node->Type == ArmIortNamedComponent) {
            status = __AddRouteSource(catalogue, discovery, node, unitIndices);
        } else {
            continue;
        }

        if (status != OS_EOK) {
            goto cleanup;
        }
    }

    *discoveryOut = discovery;
    return OS_EOK;

cleanup:
    ArmSMMUDiscoveryDestroy(&discovery);
    return status;
}
