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
 * Translates an ACPI IORT (Input Output Remapping Table) catalogue into an
 * ArmSMMUDiscovery_t lookup table. IORT is the ACPI firmware table that lists
 * ARM SMMUs, interrupt controllers, and which device IDs map to each one.
 */

#include "discovery-private.h"
#include "../acpi/iort.h"
#include <debug.h>
#include <heap.h>
#include <string.h>

/**
 * Copies every supported SMMU description into discovery->Units and records its
 * compact discovery index in unitIndices[nodeIndex]. Unsupported SMMUs still
 * count toward ARM_SMMU_MAX_UNITS and leave UINT32_MAX in unitIndices so later
 * route lookups reject devices routed through them.
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
            ERROR("__AssignUnitIndices: IORT describes more than %u SMMUs", ARM_SMMU_MAX_UNITS);
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
 * Records SMMU stream IDs reserved by firmware for pre-boot DMA from one RMR
 * (Reserved Memory Region) node, such as an active boot display framebuffer.
 * Only mappings targeting a supported SMMU matter here. Any device route
 * matching these IDs will be refused later so active firmware DMA stays safe.
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

        if (discovery->HeldIdCount == ARM_SMMU_MAX_HELD_IDS) {
            ERROR("__AddHeldIds: IORT reserves more than %u firmware ID spans", ARM_SMMU_MAX_HELD_IDS);
            return OS_ENOTSUPPORTED;
        }

        // An RMR mapping uses InputFirst..InputLast to express the count of
        // reserved IDs and places that span starting at OutputFirst.
        ids = &discovery->HeldIds[discovery->HeldIdCount++];
        ids->UnitIndex = unitIndices[mapping->TargetIndex];
        ids->First = mapping->OutputFirst;
        ids->Last = mapping->OutputFirst + mapping->InputLast - mapping->InputFirst;
    }
    return OS_EOK;
}

/**
 * Copies every ID mapping rule from one source node into discovery->Ranges.
 * Mappings aimed at an ITS controller get OS_ENOENT because ITS blocks route
 * MSI interrupts rather than DMA. Mappings to an SMMU inherit that SMMU's
 * DmaStatus, and other targets get OS_ENOTSUPPORTED. Caller must verify array
 * room before calling this.
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
        } else {
            WARNING("__AddRouteRanges: mapping %u targets IORT node type %u, which cannot translate DMA",
                    i, (uint32_t)target->Type);
        }
    }
}

/**
 * Adds a PCI host bridge or named ACPI device source along with its mapping
 * ranges. Heap-copies AcpiPath for named devices so the caller can release
 * the raw IORT catalogue as soon as discovery returns.
 */
static oserr_t
__AddRouteSource(
    _In_ const ArmIortCatalogue_t* catalogue,
    _In_ ArmSMMUDiscovery_t*       discovery,
    _In_ const ArmIortNode_t*      node,
    _In_ const uint32_t*           unitIndices)
{
    ArmSMMUSource_t* source;

    if (discovery->SourceCount == ARM_SMMU_MAX_SOURCES) {
        ERROR("__AddRouteSource: IORT describes more than %u DMA sources", ARM_SMMU_MAX_SOURCES);
        return OS_ENOTSUPPORTED;
    }

    if (node->MappingCount > ARM_SMMU_MAX_RANGES - discovery->RangeCount) {
        ERROR("__AddRouteSource: IORT needs more than %u ID mapping ranges", ARM_SMMU_MAX_RANGES);
        return OS_ENOTSUPPORTED;
    }

    // Advance SourceCount before copying AcpiPath so ArmSMMUDiscoveryDestroy
    // frees earlier strings cleanly if kmalloc fails below.
    source = &discovery->Sources[discovery->SourceCount++];
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
            ERROR("__AddRouteSource: allocating %u bytes for the device path failed", (uint32_t)length);
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
        ERROR("ArmSMMUDiscoverIort: IORT has %u nodes, more than the supported %u",
              ArmIortGetNodeCount(catalogue), ARM_SMMU_MAX_NODES);
        return OS_ENOTSUPPORTED;
    }

    discovery = kmalloc(sizeof(ArmSMMUDiscovery_t));
    if (discovery == NULL) {
        ERROR("ArmSMMUDiscoverIort: allocating the discovery table failed");
        return OS_EOOM;
    }

    // Fill unitIndices with UINT32_MAX (0xFFFFFFFF) so Pass 2 easily tells
    // supported SMMUs apart from unsupported SMMUs and non-SMMU nodes.
    memset(discovery, 0, sizeof(ArmSMMUDiscovery_t));
    memset(unitIndices, 0xFF, sizeof(unitIndices));

    // Pass 1: assign compact discovery indices to supported SMMUs before any
    // device source or RMR references them.
    status = __AssignUnitIndices(catalogue, discovery, unitIndices);
    if (status != OS_EOK) {
        goto cleanup;
    }

    // Pass 2: record firmware-reserved stream IDs and device routing sources.
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
