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
 * ARM SMMU Route Resolution
 * Looks up SMMU units and expands device requester IDs from a discovery table.
 */

#include "discovery-private.h"
#include <heap.h>
#include <string.h>

uint32_t
ArmSMMUGetUnitCount(
    _In_ const ArmSMMUDiscovery_t* discovery)
{
    return discovery == NULL ? 0 : discovery->UnitCount;
}

oserr_t
ArmSMMUGetUnitDescription(
    _In_  const ArmSMMUDiscovery_t* discovery,
    _In_  uint32_t                  unitIndex,
    _Out_ ArmSMMUDescription_t*      descriptionOut)
{
    if (discovery == NULL || descriptionOut == NULL) {
        return OS_EINVALPARAMS;
    }
    if (unitIndex >= discovery->UnitCount) {
        return OS_ENOENT;
    }
    *descriptionOut = discovery->Units[unitIndex];
    return OS_EOK;
}

/**
 * Checks whether firmware already reserved a requester ID on this SMMU.
 * Devices using a firmware-held ID may still have pre-boot DMA running (such
 * as a display scanout), so the kernel must leave them alone and never move
 * them into a new translation domain.
 */
static bool
__IsIdHeldByFirmware(
    _In_ const ArmSMMUDiscovery_t* discovery,
    _In_ uint32_t                  unitIndex,
    _In_ uint32_t                  requester)
{
    for (uint32_t i = 0; i < discovery->HeldIdCount; i++) {
        const ArmSMMUFirmwareHeldIds_t* ids = &discovery->HeldIds[i];

        if (ids->UnitIndex == unitIndex && requester >= ids->First && requester <= ids->Last) {
            return true;
        }
    }
    return false;
}

/**
 * Inserts one requester ID into route->Requesters in ascending order while
 * dropping duplicates. Returns OS_ENOTSUPPORTED if the ID is new and the
 * array is already full.
 */
static oserr_t
__AppendRequester(
    _InOut_ ArmSMMUDeviceRoute_t* result,
    _In_    uint32_t              requester)
{
    uint32_t position = 0;

    // Scan to the sorted insertion point and ignore repeated IDs before
    // checking capacity, so a duplicate does not fail a full list.
    while (position < result->RequesterCount && result->Requesters[position] < requester) {
        position++;
    }

    if (position < result->RequesterCount && result->Requesters[position] == requester) {
        return OS_EOK;
    }

    if (result->RequesterCount == DMA_MANAGER_MAX_ATTACHMENTS) {
        return OS_ENOTSUPPORTED;
    }

    memmove(
        &result->Requesters[position + 1],
        &result->Requesters[position],
        (result->RequesterCount - position) * sizeof(uint32_t)
    );
    result->Requesters[position] = requester;
    result->RequesterCount++;
    return OS_EOK;
}

/**
 * Expands one SMMU ID and its MMU-500 don't-care bitmask into exact IDs.
 * Every bit set to 1 in mask is ignored during hardware matching, so the rule
 * accepts both 0 and 1 in those bit positions. Fails with OS_ENOTSUPPORTED if
 * the expanded IDs exceed DMA_MANAGER_MAX_ATTACHMENTS or hit a firmware-held ID.
 */
static oserr_t
__AppendMaskedRequesters(
    _In_    const ArmSMMUDiscovery_t* discovery,
    _In_    uint32_t                  unitIndex,
    _In_    uint32_t                  requester,
    _In_    uint32_t                  mask,
    _InOut_ ArmSMMUDeviceRoute_t*     result)
{
    uint32_t subset = 0;
    uint32_t count = 1;
    oserr_t  status;

    // Each bit set in mask doubles the number of matching IDs. Clearing the
    // lowest set bit (bits & (bits - 1)) counts set bits quickly and stops
    // before enumerating if the expansion would overflow a route, including
    // when bit 31 is set in mask. A zero mask runs once for the exact ID.
    for (uint32_t bits = mask; bits != 0; bits &= bits - 1) {
        count *= 2;
        if (count > DMA_MANAGER_MAX_ATTACHMENTS) {
            return OS_ENOTSUPPORTED;
        }
    }
    
    // Clear the ignored bits in the base ID so subset can fill every
    // combination of those bits cleanly.
    requester &= ~mask;
    do {
        uint32_t exact = requester | subset;

        if (__IsIdHeldByFirmware(discovery, unitIndex, exact)) {
            return OS_ENOTSUPPORTED;
        }

        status = __AppendRequester(result, exact);
        if (status != OS_EOK) {
            return status;
        }
        // Standard bit-mask iteration: subtracting mask and masking back down
        // visits every subset of bits in mask in ascending order until it wraps to 0.
        subset = (subset - mask) & mask;
    } while (subset != 0);
    return OS_EOK;
}

/**
 * Finds the mapping range inside one PCI host source that covers requesterId.
 * PCI sources have NULL AcpiPath and DeviceTreeDevice and match by segment.
 * Returns NULL when the source belongs to another segment or does not cover
 * the ID. On a match, inputOut receives the ID after any DT mask is applied.
 */
static const ArmSMMURouteRange_t*
__FindPciRange(
    _In_  const ArmSMMUDiscovery_t* discovery,
    _In_  const ArmSMMUSource_t*    source,
    _In_  uint32_t                  segment,
    _In_  uint16_t                  requesterId,
    _Out_ uint32_t*                 inputOut)
{
    uint32_t input = requesterId;

    if (source->AcpiPath != NULL || source->DeviceTreeDevice != NULL || source->Segment != segment) {
        return NULL;
    }

    if (discovery->DeviceTree != NULL) {
        // Check bus-range against the raw PCI requester ID first, because
        // iommu-map-mask can strip upper bus bits before matching iommu-map.
        if (requesterId < source->FirstRequesterId || requesterId > source->LastRequesterId) {
            return NULL;
        }
        input &= source->RequesterIdMask;
    }

    for (uint32_t i = 0; i < source->RangeCount; i++) {
        const ArmSMMURouteRange_t* range = &discovery->Ranges[source->FirstRange + i];

        if (input < range->InputFirst || input > range->InputLast) {
            continue;
        }

        *inputOut = input;
        return range;
    }
    return NULL;
}

/**
 * Builds the final route for a PCI requester ID that matched range.
 * Errors saved on the host source or range are returned as-is so an
 * unsupported route is never mistaken for a device without an SMMU.
 */
static oserr_t
__BuildPciRoute(
    _In_  const ArmSMMUDiscovery_t*  discovery,
    _In_  const ArmSMMUSource_t*     source,
    _In_  const ArmSMMURouteRange_t* range,
    _In_  uint32_t                   input,
    _Out_ ArmSMMUDeviceRoute_t*      routeOut)
{
    ArmSMMUDeviceRoute_t result = { 0 };
    uint32_t             requester;
    oserr_t              status;

    if (source->Status != OS_EOK) {
        return source->Status;
    }

    if (range->Status != OS_EOK) {
        return range->Status;
    }

    // Discovery already checked that adding the offset inside this interval
    // cannot overflow 32 bits.
    requester = range->OutputFirst;
    if (!range->MapsToOneId) {
        requester += input - range->InputFirst;
    }

    status = __AppendMaskedRequesters(
        discovery,
        range->UnitIndex,
        requester,
        range->IgnoredBitsMask,
        &result
    );
    if (status != OS_EOK) {
        return status;
    }

    result.UnitIndex = range->UnitIndex;
    result.CachePolicy = source->CachePolicy;
    result.AddressLimit = source->AddressLimit;

    *routeOut = result;
    return OS_EOK;
}

oserr_t
ArmSMMUResolvePci(
    _In_  const ArmSMMUDiscovery_t* discovery,
    _In_  uint32_t                  segment,
    _In_  uint16_t                  requesterId,
    _Out_ ArmSMMUDeviceRoute_t*     routeOut)
{
    if (discovery == NULL || routeOut == NULL) {
        return OS_EINVALPARAMS;
    }

    // Stop at the first PCI host source covering this ID, even if it recorded
    // an error, so a later entry never silently masks a broken route.
    for (uint32_t i = 0; i < discovery->RouteCount; i++) {
        const ArmSMMUSource_t* source = &discovery->Routes[i];
        const ArmSMMURouteRange_t*    range;
        uint32_t                      input;

        range = __FindPciRange(discovery, source, segment, requesterId, &input);
        if (range != NULL) {
            return __BuildPciRoute(discovery, source, range, input, routeOut);
        }
    }
    return OS_ENOENT;
}

/**
 * Adds every output ID from one mapping range into a named device's route.
 * Unlike PCI lookups that pick one device ID, a named device can own its whole
 * range, so every entry must fit in one route and target the same SMMU unit.
 */
static oserr_t
__AppendRangeRequesters(
    _In_    const ArmSMMUDiscovery_t*  discovery,
    _In_    const ArmSMMURouteRange_t* range,
    _InOut_ ArmSMMUDeviceRoute_t*      result)
{
    uint64_t count;
    oserr_t  status;

    // One DMA domain attaches to a single SMMU unit, so splitting a device
    // across two different SMMUs is unsupported.
    if (result->RequesterCount != 0 && result->UnitIndex != range->UnitIndex) {
        return OS_ENOTSUPPORTED;
    }

    result->UnitIndex = range->UnitIndex;
    count = range->MapsToOneId ? 1 : (uint64_t)range->InputLast - range->InputFirst + 1;
    if (count > DMA_MANAGER_MAX_ATTACHMENTS) {
        return OS_ENOTSUPPORTED;
    }

    for (uint32_t i = 0; i < count; i++) {
        status = __AppendMaskedRequesters(
            discovery,
            range->UnitIndex,
            range->OutputFirst + i,
            range->IgnoredBitsMask,
            result
        );
        if (status != OS_EOK) {
            return status;
        }
    }
    return OS_EOK;
}

/**
 * Collects all SMMU IDs for one named ACPI or Device Tree device into a single
 * route. Succeeds only when every SMMU mapping is valid and fits in routeOut.
 * Returns OS_ENOENT if the device has only interrupt routes and no SMMU route.
 */
static oserr_t
__ResolveNamedRoute(
    _In_  const ArmSMMUDiscovery_t* discovery,
    _In_  const ArmSMMUSource_t*    source,
    _Out_ ArmSMMUDeviceRoute_t*     routeOut)
{
    ArmSMMUDeviceRoute_t result = { 0 };
    oserr_t              status;

    if (source->Status != OS_EOK) {
        return source->Status;
    }

    for (uint32_t i = 0; i < source->RangeCount; i++) {
        const ArmSMMURouteRange_t* range = &discovery->Ranges[source->FirstRange + i];

        // A named device can list both DMA mappings to an SMMU and MSI
        // mappings to an ITS group. Skip ITS entries (OS_ENOENT), while failing
        // immediately on any unsupported or broken SMMU mapping.
        if (range->Status == OS_ENOENT) {
            continue;
        } else if (range->Status != OS_EOK) {
            return range->Status;
        }

        status = __AppendRangeRequesters(discovery, range, &result);
        if (status != OS_EOK) {
            return status;
        }
    }

    if (result.RequesterCount == 0) {
        return OS_ENOENT;
    }

    result.CachePolicy = source->CachePolicy;
    result.AddressLimit = source->AddressLimit;

    *routeOut = result;
    return OS_EOK;
}

oserr_t
ArmSMMUResolveAcpiDevice(
    _In_  const ArmSMMUDiscovery_t* discovery,
    _In_  const char*               absolutePath,
    _Out_ ArmSMMUDeviceRoute_t*     routeOut)
{
    // ACPI absolute paths always start at the root backslash.
    if (discovery == NULL || absolutePath == NULL || routeOut == NULL || absolutePath[0] != '\\') {
        return OS_EINVALPARAMS;
    }

    for (uint32_t i = 0; i < discovery->RouteCount; i++) {
        const ArmSMMUSource_t* source = &discovery->Routes[i];

        if (source->AcpiPath != NULL && !strcmp(source->AcpiPath, absolutePath)) {
            return __ResolveNamedRoute(discovery, source, routeOut);
        }
    }
    return OS_ENOENT;
}

/**
 * Verifies that a caller-supplied DeviceTreeNode_t pointer belongs to tree
 * by walking the tree's node list. Compares pointers only and never reads
 * fields through device, so invalid caller pointers cannot fault.
 */
static const DeviceTreeNode_t*
__FindTreeNode(
    _In_ const DeviceTree_t*     tree,
    _In_ const DeviceTreeNode_t* device)
{
    for (const DeviceTreeNode_t* node = tree->Nodes; node != NULL; node = node->NextNode) {
        if (node == device) {
            return node;
        }
    }
    return NULL;
}

oserr_t
ArmSMMUResolveDeviceTreeDevice(
    _In_  const ArmSMMUDiscovery_t* discovery,
    _In_  const DeviceTreeNode_t*   device,
    _Out_ ArmSMMUDeviceRoute_t*     routeOut)
{
    if (discovery == NULL || device == NULL || routeOut == NULL || discovery->DeviceTree == NULL) {
        return OS_EINVALPARAMS;
    }

    // Verify device belongs to the parsed tree before matching it against routes.
    if (__FindTreeNode(discovery->DeviceTree, device) == NULL) {
        return OS_EINVALPARAMS;
    }

    for (uint32_t i = 0; i < discovery->RouteCount; i++) {
        if (discovery->Routes[i].DeviceTreeDevice == device) {
            return __ResolveNamedRoute(discovery, &discovery->Routes[i], routeOut);
        }
    }
    return OS_ENOENT;
}

void
ArmSMMUDiscoveryDestroy(
    _InOut_ ArmSMMUDiscovery_t** discovery)
{
    ArmSMMUDiscovery_t* released;

    if (discovery == NULL || *discovery == NULL) {
        return;
    }

    released = *discovery;

    // Only ACPI device paths are heap-allocated per route. All other arrays
    // live inline inside ArmSMMUDiscovery_t.
    for (uint32_t i = 0; i < released->RouteCount; i++) {
        kfree(released->Routes[i].AcpiPath);
    }
    
    kfree(released);
    *discovery = NULL;
}
