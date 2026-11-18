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

#include <debug.h>
#include <heap.h>
#include <string.h>

#include "discovery-private.h"

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
 * Checks whether firmware already reserved a stream ID on this SMMU.
 * Devices using a firmware-held ID may still run pre-boot DMA (such as an
 * active boot display), so the kernel leaves them alone instead of moving them
 * into a new translation domain.
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
 * Inserts one stream ID into result->Requesters in ascending order while
 * dropping duplicates. Fails with OS_ENOTSUPPORTED if the ID is new and the
 * fixed array is full.
 */
static oserr_t
__AppendRequester(
    _InOut_ ArmSMMUDeviceRoute_t* result,
    _In_    uint32_t              requester)
{
    uint32_t position = 0;

    // Find the sorted insertion slot first so duplicate IDs return OS_EOK even
    // when the array has already hit DMA_MANAGER_MAX_ATTACHMENTS.
    while (position < result->RequesterCount && result->Requesters[position] < requester) {
        position++;
    }

    if (position < result->RequesterCount && result->Requesters[position] == requester) {
        return OS_EOK;
    }

    if (result->RequesterCount == DMA_MANAGER_MAX_ATTACHMENTS) {
        WARNING("__AppendRequester: route needs more than %u requester IDs",
                DMA_MANAGER_MAX_ATTACHMENTS);
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
 * Expands one SMMU stream ID and its MMU-500 ignored-bits mask into exact IDs.
 * Any bit set to 1 in mask is ignored during hardware matching, so that bit can
 * be either 0 or 1 in an incoming stream ID. Fails with OS_ENOTSUPPORTED if the
 * expanded list exceeds DMA_MANAGER_MAX_ATTACHMENTS or hits a firmware-held ID.
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

    // Each set bit in mask doubles how many stream IDs match. Stripping the
    // lowest set bit (bits & (bits - 1)) counts set bits directly and stops
    // before expanding if the total would exceed a route's capacity (including
    // when bit 31 is set). When mask is zero, the loop below runs once.
    for (uint32_t bits = mask; bits != 0; bits &= bits - 1) {
        count *= 2;
        if (count > DMA_MANAGER_MAX_ATTACHMENTS) {
            WARNING("__AppendMaskedRequesters: ignored-bits mask %x on unit %u expands past %u IDs",
                    mask, unitIndex, DMA_MANAGER_MAX_ATTACHMENTS);
            return OS_ENOTSUPPORTED;
        }
    }

    // Clear masked bits in requester so ORing subset fills every combination.
    requester &= ~mask;
    do {
        uint32_t exact = requester | subset;

        if (__IsIdHeldByFirmware(discovery, unitIndex, exact)) {
            WARNING("__AppendMaskedRequesters: ID %u on unit %u is held by firmware", exact, unitIndex);
            return OS_ENOTSUPPORTED;
        }

        status = __AppendRequester(result, exact);
        if (status != OS_EOK) {
            return status;
        }

        // Step through every subset of the bits set in mask in ascending order.
        // Subtracting mask borrows across cleared gaps, and masking back down
        // keeps only the bits in mask until subset wraps around to 0.
        subset = (subset - mask) & mask;
    } while (subset != 0);
    return OS_EOK;
}

/**
 * Finds the mapping range on one PCI host bridge covering requesterId.
 * PCI sources have NULL AcpiPath and DeviceTreeDevice and match by segment.
 * Returns NULL if the source belongs to another segment or omits the ID.
 * When matched, inputOut receives the ID after applying any DT mask.
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
 * Errors recorded on the host source or target range are returned as-is so
 * an unsupported route is never mistaken for a device without an SMMU.
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
        WARNING("__BuildPciRoute: PCI segment %u has an unusable DMA route: %u",
                source->Segment, source->Status);
        return source->Status;
    }

    if (range->Status != OS_EOK) {
        WARNING("__BuildPciRoute: PCI segment %u maps to an unusable target: %u",
                source->Segment, range->Status);
        return range->Status;
    }

    // Discovery verified earlier that adding the offset stays within 32 bits.
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
        WARNING("__BuildPciRoute: expanding requester %u on segment %u failed: %u",
                requester, source->Segment, status);
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

    // Return immediately on the first PCI host source covering this ID, even
    // when that source holds an error, so a later entry cannot mask a failure.
    for (uint32_t i = 0; i < discovery->SourceCount; i++) {
        const ArmSMMUSource_t*     source = &discovery->Sources[i];
        const ArmSMMURouteRange_t* range;
        uint32_t                   input;

        range = __FindPciRange(discovery, source, segment, requesterId, &input);
        if (range != NULL) {
            return __BuildPciRoute(discovery, source, range, input, routeOut);
        }
    }
    return OS_ENOENT;
}

/**
 * Adds every stream ID from one mapping range into a named device's route.
 * A PCI lookup picks a single device ID, whereas a named platform device uses
 * every ID in its range together, so all entries must fit in one route and
 * target the same SMMU controller.
 */
static oserr_t
__AppendRangeRequesters(
    _In_    const ArmSMMUDiscovery_t*  discovery,
    _In_    const ArmSMMURouteRange_t* range,
    _InOut_ ArmSMMUDeviceRoute_t*      result)
{
    uint64_t count;
    oserr_t  status;

    // One DMA translation domain binds to a single SMMU unit, so spanning two
    // different SMMU controllers is rejected.
    if (result->RequesterCount != 0 && result->UnitIndex != range->UnitIndex) {
        WARNING("__AppendRangeRequesters: device spans SMMU units %u and %u",
                result->UnitIndex, range->UnitIndex);
        return OS_ENOTSUPPORTED;
    }

    result->UnitIndex = range->UnitIndex;
    count = range->MapsToOneId ? 1 : (uint64_t)range->InputLast - range->InputFirst + 1;
    if (count > DMA_MANAGER_MAX_ATTACHMENTS) {
        WARNING("__AppendRangeRequesters: range of %llu IDs on unit %u exceeds %u",
                count, range->UnitIndex, DMA_MANAGER_MAX_ATTACHMENTS);
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
 * Collects every SMMU stream ID for one named ACPI or Device Tree device into
 * routeOut. Succeeds only when all SMMU mappings are valid and fit together.
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
        WARNING("__ResolveNamedRoute: device has an unusable DMA route: %u", source->Status);
        return source->Status;
    }

    for (uint32_t i = 0; i < source->RangeCount; i++) {
        const ArmSMMURouteRange_t* range = &discovery->Ranges[source->FirstRange + i];

        // A named device can carry both DMA routes (to an SMMU) and MSI
        // interrupt routes (to a GIC ITS controller). Skip ITS entries marked
        // OS_ENOENT, while failing immediately on any broken SMMU entry.
        if (range->Status == OS_ENOENT) {
            continue;
        } else if (range->Status != OS_EOK) {
            WARNING("__ResolveNamedRoute: mapping %u targets an unusable unit: %u", i, range->Status);
            return range->Status;
        }

        status = __AppendRangeRequesters(discovery, range, &result);
        if (status != OS_EOK) {
            WARNING("__ResolveNamedRoute: adding mapping %u failed: %u", i, status);
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
    // ACPI namespace paths always start at the root backslash.
    if (discovery == NULL || absolutePath == NULL || routeOut == NULL || absolutePath[0] != '\\') {
        return OS_EINVALPARAMS;
    }

    for (uint32_t i = 0; i < discovery->SourceCount; i++) {
        const ArmSMMUSource_t* source = &discovery->Sources[i];

        if (source->AcpiPath != NULL && !strcmp(source->AcpiPath, absolutePath)) {
            return __ResolveNamedRoute(discovery, source, routeOut);
        }
    }
    return OS_ENOENT;
}

/**
 * Verifies that a caller-supplied DeviceTreeNode_t pointer actually belongs to
 * tree by walking tree->Nodes. Compares pointers only without reading through
 * device, so a stale or invalid pointer from a caller cannot fault.
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

    // Confirm device lives inside the parsed tree before scanning sources.
    if (__FindTreeNode(discovery->DeviceTree, device) == NULL) {
        return OS_EINVALPARAMS;
    }

    for (uint32_t i = 0; i < discovery->SourceCount; i++) {
        if (discovery->Sources[i].DeviceTreeDevice == device) {
            return __ResolveNamedRoute(discovery, &discovery->Sources[i], routeOut);
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

    // Only AcpiPath strings are allocated separately on the heap, while every
    // other table lives inline inside ArmSMMUDiscovery_t.
    for (uint32_t i = 0; i < released->SourceCount; i++) {
        kfree(released->Sources[i].AcpiPath);
    }

    kfree(released);
    *discovery = NULL;
}
