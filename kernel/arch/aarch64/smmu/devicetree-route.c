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
 * ARM SMMU Device Tree Discovery
 * Reads DMA bus address limits, cache-coherency flags, and device ID mappings
 * (iommus and iommu-map) from a parsed Device Tree into ArmSMMUDiscovery_t.
 */

#include <fdt/dma.h>
#include <fdt/reader.h>
#include <string.h>

#include "devicetree-private.h"

// Reads a cell-count property (#address-cells or #size-cells) that tells how
// many 32-bit integers encode an address or length, falling back to
// defaultValue when the property is omitted.
static oserr_t
__ReadCellsOrDefault(
    _In_  const DeviceTreeNode_t* node,
    _In_  const char*             name,
    _In_  uint32_t                defaultValue,
    _Out_ uint32_t*               cells)
{
    oserr_t status = DeviceTreeReadCell(node, name, cells);

    if (status == OS_ENOENT) {
        *cells = defaultValue;
        return OS_EOK;
    }
    return status;
}

/**
 * Packages the dma-ranges property and cell widths of bus for FdtDmaDecode.
 * Each dma-ranges entry encodes <child-address parent-address length> using
 * #address-cells on bus, #address-cells on bus->Parent, and #size-cells on bus.
 */
static oserr_t
__ReadDmaProperty(
    _In_  const DeviceTreeNode_t* bus,
    _In_  bool                    pci,
    _Out_ FdtDmaProperty_t*       property)
{
    const DeviceTreeProperty_t* ranges = DeviceTreeGetProperty(bus, "dma-ranges");
    oserr_t                     status;

    *property = (FdtDmaProperty_t) {
        .ChildFormat = pci ? FdtDmaAddressPci : FdtDmaAddressSimple,
        .ParentFormat = FdtDmaAddressSimple
    };

    if (ranges != NULL) {
        property->Bytes = ranges->Value;
        property->Length = ranges->Length;
    }

    status = __ReadCellsOrDefault(bus, "#address-cells", 2, &property->ChildCells);
    if (status != OS_EOK) {
        return status;
    }

    status = __ReadCellsOrDefault(bus->Parent, "#address-cells", 2, &property->ParentCells);
    if (status != OS_EOK) {
        return status;
    }
    return __ReadCellsOrDefault(bus, "#size-cells", 1, &property->SizeCells);
}

/**
 * Narrows limit using the dma-ranges property on bus, which describes which
 * device DMA addresses travel upstream toward RAM. A missing dma-ranges returns
 * OS_ENOTSUPPORTED because reachability cannot be assumed. An empty property
 * passes addresses 1:1 up to the bus's address bit width. Listed entries are
 * supported only as a single 0-based 1:1 window [0, Length - 1], since
 * ArmSMMUDeviceRoute stores one upper physical limit. Pass pci=true for PCI
 * host bridges that use 3-cell PCI child addresses.
 */
static oserr_t
__ReadDmaWindow(
    _In_    const DeviceTreeNode_t* bus,
    _In_    bool                    pci,
    _InOut_ uint64_t*               limit)
{
    FdtDmaProperty_t   property;
    FdtDmaBusWindows_t windows;
    oserr_t            status;

    status = __ReadDmaProperty(bus, pci, &property);
    if (status != OS_EOK) {
        return status;
    }

    status = FdtDmaDecode(&property, &windows, NULL);
    if (status != OS_EOK) {
        return status;
    }

    switch (windows.State) {
        case FdtDmaPropertyIdentity:
            if (windows.ChildLimit < *limit) {
                *limit = windows.ChildLimit;
            }
            return OS_EOK;
        case FdtDmaPropertyWindows:
            if (windows.Count != 1 || windows.Windows[0].ChildBase != 0 ||
                windows.Windows[0].ParentBase != 0) {
                return OS_ENOTSUPPORTED;
            }

            if (windows.Windows[0].Length - 1 < *limit) {
                *limit = windows.Windows[0].Length - 1;
            }
            return OS_EOK;
        default:
            // Refuse DMA when dma-ranges is missing, since we cannot tell what
            // physical RAM addresses the bus actually forwards.
            return OS_ENOTSUPPORTED;
    }
}

/**
 * Checks dma-coherent and dma-noncoherent on node. dma-coherent means hardware
 * keeps device DMA in sync with CPU caches automatically, while dma-noncoherent
 * means software must flush/invalidate CPU caches around DMA. Walking upward
 * lets the closest node to the device set CachePolicy. Setting both flags on
 * one node is invalid.
 */
static oserr_t
__ReadCacheFlags(
    _In_    const DeviceTreeNode_t* node,
    _InOut_ ArmSMMUSource_t* source,
    _InOut_ bool*                   cacheKnown)
{
    bool    coherent;
    bool    noncoherent;
    oserr_t status;

    status = DeviceTreeReadBoolean(node, "dma-coherent", &coherent);
    if (status != OS_EOK) {
        return status;
    }

    status = DeviceTreeReadBoolean(node, "dma-noncoherent", &noncoherent);
    if (status != OS_EOK) {
        return status;
    }

    if (coherent && noncoherent) {
        return OS_EINVALPARAMS;
    }

    if (!*cacheKnown && (coherent || noncoherent)) {
        source->CachePolicy = coherent ? SHMDeviceCacheCoherent : SHMDeviceCacheNonCoherent;
        *cacheKnown = true;
    }
    return OS_EOK;
}

// Returns true if node has device_type "pci".
static bool
__IsPciNode(
    _In_ const DeviceTreeNode_t* node)
{
    const char* type = node != NULL ? DeviceTreeReadString(node, "device_type") : NULL;

    return type != NULL && strcmp(type, "pci") == 0;
}

/**
 * Checks that a node along the device-to-root path uses only bus and DMA
 * features the kernel supports. The device's own iommus or iommu-map selects
 * its SMMU connection and is allowed, whereas parent IOMMU bindings,
 * dma-parent overrides, and memory-region overrides are rejected.
 */
static bool
__IsUnsupportedDmaNode(
    _In_ const DeviceTreeNode_t* device,
    _In_ const DeviceTreeNode_t* node)
{
    const DeviceTreeProperty_t* compatible = DeviceTreeGetProperty(node, "compatible");
    FdtDmaNode_t                description = {
        .Compatible = compatible != NULL ? compatible->Value : NULL,
        .CompatibleLength = compatible != NULL ? compatible->Length : 0,
        .IsRoot = node->Parent == NULL,
        .IsPci = __IsPciNode(node),
        .ParentIsPci = __IsPciNode(node->Parent),
        .Disabled = !DeviceTreeIsEnabled(node),
        .HasIommus = node != device && DeviceTreeGetProperty(node, "iommus") != NULL,
        .HasIommuMap = node != device && DeviceTreeGetProperty(node, "iommu-map") != NULL,
        .HasDmaParent = DeviceTreeGetProperty(node, "dma-parent") != NULL,
        .HasDmaRanges = DeviceTreeGetProperty(node, "dma-ranges") != NULL,
        .HasMemoryRegion = DeviceTreeGetProperty(node, "memory-region") != NULL
    };

    if (node != device) {
        return !FdtDmaBusIsDirect(&description, NULL);
    }
    return description.IsPci ? !FdtDmaNodeIsDirect(&description, NULL) :
                               !FdtDmaDeviceIsDirect(&description, NULL);
}

// Clamps source->AddressLimit to 4 GiB when the root node uses 32-bit
// addresses (#address-cells == 1) and no intermediate bus narrowed it first.
static oserr_t
__ReadRootAddressWidth(
    _In_    const DeviceTreeNode_t* root,
    _InOut_ ArmSMMUSource_t* source)
{
    uint32_t cells;
    oserr_t  status;

    status = __ReadCellsOrDefault(root, "#address-cells", 2, &cells);
    if (status != OS_EOK) {
        return status;
    }

    if (cells == 1 && source->AddressLimit > UINT32_MAX) {
        source->AddressLimit = UINT32_MAX;
    } else if (cells != 1 && cells != 2) {
        source->Status = OS_ENOTSUPPORTED;
    }
    return OS_EOK;
}

/**
 * Walks upward from device to the root node to determine its DMA cache policy
 * and highest reachable physical address. Every parent bus must be a simple
 * bus with a supported dma-ranges window. Unsupported bus features are saved
 * in source->Status so this device is blocked while other devices keep working.
 * Platform devices have no dma-ranges of their own, whereas PCI host bridges do.
 */
static oserr_t
__ReadDmaProperties(
    _In_    const DeviceTreeNode_t* device,
    _In_    bool                    pci,
    _InOut_ ArmSMMUSource_t* source)
{
    const DeviceTreeNode_t* node;
    bool                    cacheKnown = false;

    source->AddressLimit = UINT64_MAX;
    source->CachePolicy = SHMDeviceCacheNonCoherent;
    for (node = device; node != NULL; node = node->Parent) {
        oserr_t status;

        status = __ReadCacheFlags(node, source, &cacheKnown);
        if (status != OS_EOK) {
            return status;
        }

        if (__IsUnsupportedDmaNode(device, node)) {
            source->Status = OS_ENOTSUPPORTED;
        }

        if (node->Parent == NULL) {
            status = __ReadRootAddressWidth(node, source);
            if (status != OS_EOK) {
                return status;
            }
            continue;
        }

        if (node == device && !pci) {
            continue;
        }

        status = __ReadDmaWindow(node, node == device && pci, &source->AddressLimit);
        if (status != OS_EOK && status != OS_ENOTSUPPORTED) {
            return status;
        } else if (status != OS_EOK) {
            source->Status = status;
        }
    }
    return OS_EOK;
}

/**
 * Resolves the target SMMU unit, base stream ID, and ignored-bits mask from
 * one iommus or iommu-map entry. arguments holds the cells that follow the
 * SMMU phandle. Leaves range->Status as OS_ENOTSUPPORTED if the target SMMU
 * is missing, unsupported, or expands to more than DMA_MANAGER_MAX_ATTACHMENTS
 * stream IDs.
 */
static oserr_t
__ReadMapTarget(
    _In_    const struct ArmSMMUTreeParser* parser,
    _In_    const DeviceTreeNode_t*        target,
    _In_    const uint32_t*                arguments,
    _In_    uint32_t                       cells,
    _InOut_ ArmSMMURouteRange_t*           range)
{
    uint32_t i;

    if (!DeviceTreeIsEnabled(target)) {
        return OS_EINVALPARAMS;
    }

    range->Status = OS_ENOTSUPPORTED;
    for (i = 0; i < parser->UnitCount; i++) {
        const struct ArmSMMUTreeUnit* unit = &parser->Units[i];
        uint32_t                      count = 1;
        uint32_t                      bits;

        if (unit->Node != target) {
            continue;
        }

        if (unit->Status != OS_EOK) {
            return OS_EOK;
        }

        range->UnitIndex = unit->Index;
        range->OutputFirst = arguments[0];
        range->IgnoredBitsMask = cells == 2 ? arguments[1] : unit->Mask;

        // Each bit set in IgnoredBitsMask doubles the matching stream ID count.
        // Clearing (bits & (bits - 1)) counts set bits and stops on overflow.
        for (bits = range->IgnoredBitsMask; bits != 0; bits &= bits - 1) {
            count *= 2;
            if (count > DMA_MANAGER_MAX_ATTACHMENTS) {
                return OS_EOK;
            }
        }

        range->Status = OS_EOK;
        return OS_EOK;
    }
    return OS_EOK;
}

/**
 * Checks whether a single-ID rule overlaps an existing rule on the same SMMU
 * that uses a different IgnoredBitsMask. Two rules match a common stream ID
 * whenever every bit position ignored by neither mask has the same value in
 * both OutputFirst IDs. Matching the exact same mask describes the same slot,
 * while overlapping rules with different masks make hardware matching ambiguous.
 */
static bool
__HasConflictingMask(
    _In_ const ArmSMMUDiscovery_t*  discovery,
    _In_ const ArmSMMURouteRange_t* range)
{
    uint32_t i;

    for (i = 0; i < discovery->RangeCount; i++) {
        const ArmSMMURouteRange_t* other = &discovery->Ranges[i];

        if (other->MapsToOneId && range->Status == OS_EOK && other->Status == OS_EOK &&
            range->UnitIndex == other->UnitIndex && range->IgnoredBitsMask != other->IgnoredBitsMask &&
            ((range->OutputFirst ^ other->OutputFirst) & ~(range->IgnoredBitsMask | other->IgnoredBitsMask)) == 0) {
            return true;
        }
    }
    return false;
}

/**
 * Reads every entry in a platform device's iommus property. Each entry binds
 * the device to one stream ID (and optional MMU-500 mask) on an SMMU, and at
 * least one entry must be present.
 */
static oserr_t
__ReadPlatformRoutes(
    _InOut_ struct ArmSMMUTreeParser* parser,
    _In_    const DeviceTreeNode_t*  node,
    _InOut_ ArmSMMUSource_t*  source)
{
    ArmSMMUDiscovery_t* discovery = parser->Discovery;
    uint32_t            index;

    source->DeviceTreeDevice = node;
    for (index = 0;; index++) {
        DeviceTreeReference_t reference;
        ArmSMMURouteRange_t    range = {.MapsToOneId = true};
        oserr_t               status;

        status = DeviceTreeReadReference(parser->Tree, node, "iommus", "#iommu-cells", index, &reference);
        if (status == OS_ENOENT) {
            return index == 0 ? OS_EINVALPARAMS : OS_EOK;
        } else if (status != OS_EOK) {
            return status;
        }

        if (discovery->RangeCount == ARM_SMMU_MAX_RANGES) {
            return OS_ENOTSUPPORTED;
        }

        status = __ReadMapTarget(
            parser,
            reference.Node,
            reference.Cells,
            reference.CellCount,
            &range
        );
        if (status != OS_EOK) {
            return status;
        }

        if (__HasConflictingMask(discovery, &range)) {
            return OS_EINVALPARAMS;
        }

        discovery->Ranges[discovery->RangeCount++] = range;
        source->RangeCount++;
    }
}

/**
 * Reads the PCI segment number from linux,pci-domain and confirms no other
 * enabled PCI host bridge in the Device Tree shares that segment.
 */
static oserr_t
__ReadPciSegment(
    _In_  const struct ArmSMMUTreeParser* parser,
    _In_  const DeviceTreeNode_t*        node,
    _Out_ uint32_t*                      segmentOut)
{
    uint32_t                segment;
    const DeviceTreeNode_t* other;
    oserr_t                 status;

    status = DeviceTreeReadCell(node, "linux,pci-domain", &segment);
    if (status != OS_EOK) {
        // Require an explicit domain number so PCI lookups match deviced reliably.
        return status == OS_ENOENT ? OS_ENOTSUPPORTED : status;
    }

    for (other = parser->Tree->Nodes; other != NULL; other = other->NextNode) {
        const char* type = DeviceTreeReadString(other, "device_type");
        uint32_t    otherSegment;

        if (other == node || !DeviceTreeIsEnabled(other) || type == NULL || strcmp(type, "pci")) {
            continue;
        }

        // Check untranslated PCI hosts too so no two bridges share a segment.
        status = DeviceTreeReadCell(other, "linux,pci-domain", &otherSegment);
        if (status == OS_ENOENT) {
            continue;
        } else if (status != OS_EOK) {
            return status;
        }

        if (otherSegment == segment) {
            return OS_EINVALPARAMS;
        }
    }

    *segmentOut = segment;
    return OS_EOK;
}

/**
 * Converts a PCI host bridge's bus-range property into the inclusive 16-bit
 * requester ID span [FirstRequesterId, LastRequesterId]. A PCI requester ID
 * packs the 8-bit bus number into bits [15:8] and the device/function into
 * bits [7:0]. When bus-range is omitted, all 16-bit IDs are accepted.
 */
static oserr_t
__ReadPciBusRange(
    _In_    const DeviceTreeNode_t* node,
    _InOut_ ArmSMMUSource_t* source)
{
    const DeviceTreeProperty_t* buses = DeviceTreeGetProperty(node, "bus-range");
    const uint8_t*              bytes;
    uint32_t                    first;
    uint32_t                    last;

    source->FirstRequesterId = 0;
    source->LastRequesterId = UINT16_MAX;
    if (buses == NULL) {
        return OS_EOK;
    }

    if (buses->Length != 8) {
        return OS_EINVALPARAMS;
    }

    bytes = buses->Value;
    first = FdtReadBe32(bytes);
    last = FdtReadBe32(bytes + 4);
    if (first > last || last > UINT8_MAX) {
        return OS_EINVALPARAMS;
    }

    source->FirstRequesterId = (uint16_t)(first << 8);
    source->LastRequesterId = (uint16_t)((last << 8) | UINT8_MAX);
    return OS_EOK;
}

/**
 * Parses one entry of a PCI iommu-map property:
 *   <rid-base phandle iommu-specifier[cells] length>
 * Because the target SMMU picks #iommu-cells, entrySize returns the entry's
 * byte length ((cells + 3) * 4) so the caller can advance to the next entry.
 */
static oserr_t
__ReadPciMapEntry(
    _In_  const struct ArmSMMUTreeParser* parser,
    _In_  const uint8_t*                 tuple,
    _In_  uint32_t                       available,
    _Out_ ArmSMMURouteRange_t*           rangeOut,
    _Out_ uint32_t*                      entrySize)
{
    const DeviceTreeNode_t* target;
    ArmSMMURouteRange_t     range = {0};
    uint32_t                arguments[16];
    uint32_t                cells;
    uint32_t                length;
    uint32_t                i;
    oserr_t                 status;

    if (available < 8) {
        return OS_EINVALPARAMS;
    }

    target = DeviceTreeFindPhandle(parser->Tree, FdtReadBe32(tuple + 4));
    if (target == NULL) {
        return OS_EINVALPARAMS;
    }

    status = DeviceTreeReadCell(target, "#iommu-cells", &cells);
    if (status != OS_EOK) {
        return status == OS_ENOENT ? OS_EINVALPARAMS : status;
    }

    if (cells > 16) {
        return OS_ENOTSUPPORTED;
    }

    // Entry layout in 32-bit words: rid-base (1) + phandle (1) + cells + length (1).
    if (available < (cells + 3) * 4) {
        return OS_EINVALPARAMS;
    }

    for (i = 0; i < cells; i++) {
        arguments[i] = FdtReadBe32(tuple + 8 + i * 4);
    }

    range.InputFirst = FdtReadBe32(tuple);
    length = FdtReadBe32(tuple + (cells + 2) * 4);
    if (length == 0 || range.InputFirst > UINT16_MAX || length - 1 > UINT16_MAX - range.InputFirst) {
        return OS_EINVALPARAMS;
    }

    range.InputLast = range.InputFirst + length - 1;

    status = __ReadMapTarget(parser, target, arguments, cells, &range);
    if (status != OS_EOK) {
        return status;
    }

    // Make sure adding the offset stays inside 32 bits for every ID in length.
    if (range.Status == OS_EOK && length - 1 > UINT32_MAX - range.OutputFirst) {
        return OS_EINVALPARAMS;
    }

    *rangeOut = range;
    *entrySize = (cells + 3) * 4;
    return OS_EOK;
}

// Appends range to a PCI host bridge after confirming it does not overlap any
// range already stored for that bridge, keeping PCI ID lookups unambiguous.
static oserr_t
__AddPciRange(
    _In_ ArmSMMUDiscovery_t*        discovery,
    _In_ ArmSMMUSource_t*    source,
    _In_ const ArmSMMURouteRange_t* range)
{
    uint32_t i;

    for (i = 0; i < source->RangeCount; i++) {
        const ArmSMMURouteRange_t* other = &discovery->Ranges[source->FirstRange + i];

        if (range->InputFirst <= other->InputLast && other->InputFirst <= range->InputLast) {
            return OS_EINVALPARAMS;
        }
    }

    if (discovery->RangeCount == ARM_SMMU_MAX_RANGES) {
        return OS_ENOTSUPPORTED;
    }

    discovery->Ranges[discovery->RangeCount++] = *range;
    source->RangeCount++;
    return OS_EOK;
}

/**
 * Reads the iommu-map of a PCI host bridge. Before matching iommu-map entries,
 * lookups apply iommu-map-mask (default 0xffff) to the 16-bit PCI requester ID,
 * for instance to collapse all functions on one device onto one stream ID.
 */
static oserr_t
__ReadPciRoutes(
    _InOut_ struct ArmSMMUTreeParser* parser,
    _In_    const DeviceTreeNode_t*  node,
    _InOut_ ArmSMMUSource_t*  source)
{
    const DeviceTreeProperty_t* map = DeviceTreeGetProperty(node, "iommu-map");
    uint32_t                    offset = 0;
    oserr_t                     status;

    status = __ReadPciSegment(parser, node, &source->Segment);
    if (status != OS_EOK) {
        return status;
    }

    status = __ReadPciBusRange(node, source);
    if (status != OS_EOK) {
        return status;
    }

    source->RequesterIdMask = UINT16_MAX;
    status = DeviceTreeReadCell(node, "iommu-map-mask", &source->RequesterIdMask);
    if (status != OS_EOK && status != OS_ENOENT) {
        return status;
    }

    if (source->RequesterIdMask > UINT16_MAX || map->Length == 0 || map->Length % 4 != 0) {
        return OS_EINVALPARAMS;
    }

    while (offset < map->Length) {
        ArmSMMURouteRange_t range;
        uint32_t            entrySize;

        status = __ReadPciMapEntry(
            parser,
            (const uint8_t*)map->Value + offset,
            map->Length - offset,
            &range,
            &entrySize
        );
        if (status != OS_EOK) {
            return status;
        }

        status = __AddPciRange(parser->Discovery, source, &range);
        if (status != OS_EOK) {
            return status;
        }
        offset += entrySize;
    }
    return OS_EOK;
}

oserr_t
ArmSMMUDeviceTreeReadRoute(
    _InOut_ struct ArmSMMUTreeParser* parser,
    _In_    const DeviceTreeNode_t*  node)
{
    const DeviceTreeProperty_t* map = DeviceTreeGetProperty(node, "iommu-map");
    const DeviceTreeProperty_t* iommus = DeviceTreeGetProperty(node, "iommus");
    ArmSMMUSource_t*     source;
    const char*                 type;
    oserr_t                     status;

    if (!DeviceTreeIsEnabled(node) || (map == NULL && iommus == NULL)) {
        return OS_EOK;
    }

    // ArmSMMUDeviceTreeCollectUnits already capped total nodes at ARM_SMMU_MAX_NODES
    // (which matches ARM_SMMU_MAX_SOURCES), so Sources cannot overflow here.
    source = &parser->Discovery->Sources[parser->Discovery->SourceCount++];
    source->FirstRange = parser->Discovery->RangeCount;

    // PCI host bridges must use iommu-map alone, while platform devices use iommus.
    type = DeviceTreeReadString(node, "device_type");
    if (map != NULL) {
        if (iommus != NULL || type == NULL || strcmp(type, "pci")) {
            return OS_ENOTSUPPORTED;
        }
        status = __ReadPciRoutes(parser, node, source);
    } else {
        status = __ReadPlatformRoutes(parser, node, source);
    }
    if (status != OS_EOK) {
        return status;
    }
    return __ReadDmaProperties(node, map != NULL, source);
}
