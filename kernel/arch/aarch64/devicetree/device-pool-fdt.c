/**
 * Copyright 2026, Philip Meulengracht
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
 * ARM64 Device Tree device pool selection
 *
 * On ARM64 boards, different buses can reach different windows of physical RAM
 * (for instance, a 32-bit bus may only reach below 4 GiB, or a controller may
 * shift addresses). Because the kernel reserves one shared device pool at boot
 * before enabling the MMU, that pool has to sit in physical RAM that every
 * active direct-DMA path can reach.
 *
 * This file walks the raw Device Tree blob without heap memory, locks, or
 * logging. It tracks each active node's reachable CPU RAM intervals, intersects
 * them across all direct-DMA routes, and passes the surviving ranges to
 * SystemMemoryFitDeviceRegionInRanges.
 */

#include "device-pool-fdt.h"
#include "../common/boot-memory.h"
#include <arch/aarch64/arch.h>
#include <fdt/board.h>
#include <fdt/dma.h>
#include <fdt/reader.h>
#include <string.h>

// Only these Device Tree properties affect DMA routing and board rules.
// Seen tracks presence separately from Length because an empty dma-ranges
// property (present, Length == 0) means 1:1 pass-through, while an absent
// property means no route was given.
enum __PoolProperty {
    __PoolAddressCells,
    __PoolSizeCells,
    __PoolCompatible,
    __PoolDeviceType,
    __PoolStatus,
    __PoolDmaRanges,
    __PoolRanges,
    __PoolReg,
    __PoolIommus,
    __PoolIommuMap,
    __PoolDmaParent,
    __PoolMemoryRegion,
    __PoolPropertyCount
};

static const char* const g_propertyNames[__PoolPropertyCount] = {
    "#address-cells", "#size-cells", "compatible", "device_type", "status",
    "dma-ranges", "ranges", "reg", "iommus", "iommu-map", "dma-parent", "memory-region"
};

struct __PoolPropertyValue {
    const uint8_t* Bytes;
    uint32_t       Length;
};

// Tracks how a single node's DMA route resolved once its properties arrived.
enum __PoolNodeState {
    // Properties are still being collected. __PrepareNode has not run yet.
    __PoolNodePending,
    // Disabled, or handled outside direct DMA (IOMMU, dma-parent, memory-region,
    // or a firmware-private bus). Neither this node nor its children narrow the pool.
    __PoolNodeSkipped,
    // No dma-ranges property on this bus. Children cannot build a route through it.
    __PoolNodeNoRoute,
    // Map holds the valid CPU RAM ranges reachable through this node.
    __PoolNodeValid,
    // Map holds a board fallback route for this controller only. Children cannot inherit it.
    __PoolNodeBoardRoute,
    // Building this node's route failed. Error holds the status code.
    __PoolNodeError
};

// Scratch frame for one active node along the path from root to current depth.
// Stored in static g_scan instead of the small early-boot stack.
struct __PoolNode {
    const char*                Name;
    struct __PoolPropertyValue Values[__PoolPropertyCount];
    uint32_t                   Seen;
    uint32_t                   AddressCells;
    uint32_t                   SizeCells;
    enum __PoolNodeState       State;
    oserr_t                    Error;
    bool                       HasChildren;
    bool                       HasActiveChild;
    bool                       DuplicateProperty;
    bool                       Pci;
    bool                       LimitsPool;
    FdtDmaMap_t                Map;
};

// Whole-scan state shared across FdtParseStructure callbacks.
// Ram holds all usable system RAM from the boot map. Common holds the running
// intersection of CPU physical intervals across all contributing routes.
struct __PoolScan {
    struct __PoolNode           Nodes[FDT_MAX_DEPTH];
    int                         Depth;
    FdtDmaMap_t                 Ram;
    SystemMemoryPhysicalRange_t Common[FDT_DMA_MAX_RANGES];
    SystemMemoryPhysicalRange_t Next[FDT_DMA_MAX_RANGES];
    uint32_t                    Count;
    uint32_t                    Routes;
    const char*                 Reason;
    const char*                 FailedNode;
};

static struct __PoolScan g_scan;

static bool
__HasString(
    _In_ const struct __PoolNode* node,
    _In_ enum __PoolProperty      property,
    _In_ const char*              string)
{
    return FdtStringListContains(
        node->Values[property].Bytes,
        node->Values[property].Length,
        string
    );
}

// Reads a cell count property (like #address-cells), or uses defaultValue if
// the property is missing. Rejects properties that are not 4 bytes.
static oserr_t
__ReadCellCount(
    _In_  const struct __PoolNode* node,
    _In_  enum __PoolProperty      property,
    _In_  uint32_t                 defaultValue,
    _Out_ uint32_t*                valueOut)
{
    if (!(node->Seen & (1U << property))) {
        *valueOut = defaultValue;
        return OS_EOK;
    }

    if (node->Values[property].Length != 4) {
        return OS_EINVALPARAMS;
    }

    *valueOut = FdtReadBe32(node->Values[property].Bytes);
    return OS_EOK;
}

// Decodes one bus's dma-ranges property using the child and parent cell formats.
static oserr_t
__DecodeBus(
    _In_  const struct __PoolNode* node,
    _In_  const struct __PoolNode* parent,
    _Out_ FdtDmaBusWindows_t*      windows)
{
    FdtDmaProperty_t description = {
        .Bytes = node->Values[__PoolDmaRanges].Bytes,
        .Length = node->Values[__PoolDmaRanges].Length,
        .ChildCells = node->AddressCells,
        .ParentCells = parent->AddressCells,
        .SizeCells = node->SizeCells,
        .ChildFormat = node->Pci ? FdtDmaAddressPci : FdtDmaAddressSimple,
        .ParentFormat = parent->Pci ? FdtDmaAddressPci : FdtDmaAddressSimple
    };

    return FdtDmaDecode(&description, windows, NULL);
}

// Converts (base, length) to an inclusive [Base, Limit] interval so ranges
// ending at UINT64_MAX never overflow. Length must be nonzero.
static SystemMemoryPhysicalRange_t
__PhysicalRange(
    _In_ uint64_t base,
    _In_ uint64_t length)
{
    return (SystemMemoryPhysicalRange_t) {
        .Base = base,
        .Limit = base + (length - 1)
    };
}

/**
 * @brief Narrow the running pool intervals (scan->Common) with one node's DMA map.
 *
 * Intersects in CPU physical address space. Separate intervals stay separate
 * even if they touch, so a pool can never cross a point where a bus's address
 * offset changes. Duplicate intervals (from two bus aliases reaching the same
 * RAM) are dropped to save slots in Common[].
 */
static oserr_t
__IntersectRoute(
    _InOut_ struct __PoolScan* scan,
    _In_    const FdtDmaMap_t* map)
{
    uint32_t count = 0;

    for (uint32_t i = 0; i < scan->Count; i++) {
        for (uint32_t j = 0; j < map->Count; j++) {
            SystemMemoryPhysicalRange_t overlap = __PhysicalRange(
                map->Ranges[j].PhysicalBase,
                map->Ranges[j].Length
            );
            bool duplicate = false;

            if (overlap.Base < scan->Common[i].Base) {
                overlap.Base = scan->Common[i].Base;
            }

            if (overlap.Limit > scan->Common[i].Limit) {
                overlap.Limit = scan->Common[i].Limit;
            }

            if (overlap.Base > overlap.Limit) {
                continue;
            }

            for (uint32_t k = 0; k < count; k++) {
                if (scan->Next[k].Base == overlap.Base && scan->Next[k].Limit == overlap.Limit) {
                    duplicate = true;
                    break;
                }
            }

            if (duplicate) {
                continue;
            }

            if (count == FDT_DMA_MAX_RANGES) {
                return OS_ENOTSUPPORTED;
            }

            scan->Next[count++] = overlap;
        }
    }

    if (count == 0) {
        return OS_ENOENT;
    }

    memcpy(scan->Common, scan->Next, count * sizeof(scan->Common[0]));
    scan->Count = count;
    scan->Routes++;
    return OS_EOK;
}

// Populates FdtDmaNode_t from a scanned frame. Pass parent == NULL for the root.
static void
__DescribeNode(
    _In_  const struct __PoolNode* node,
    _In_  const struct __PoolNode* parent,
    _Out_ FdtDmaNode_t*            description)
{
    *description = (FdtDmaNode_t) {
        .Compatible = node->Values[__PoolCompatible].Bytes,
        .CompatibleLength = node->Values[__PoolCompatible].Length,
        .IsRoot = parent == NULL,
        .IsPci = node->Pci,
        .ParentIsPci = parent != NULL && parent->Pci,
        .Disabled = (node->Seen & (1U << __PoolStatus)) &&
            !__HasString(node, __PoolStatus, "okay") && !__HasString(node, __PoolStatus, "ok"),
        .HasIommus = (node->Seen & (1U << __PoolIommus)) != 0,
        .HasIommuMap = (node->Seen & (1U << __PoolIommuMap)) != 0,
        .HasDmaParent = (node->Seen & (1U << __PoolDmaParent)) != 0,
        .HasDmaRanges = node->Values[__PoolDmaRanges].Bytes != NULL,
        .HasMemoryRegion = (node->Seen & (1U << __PoolMemoryRegion)) != 0
    };
}

/**
 * @brief Read and translate a device's first MMIO register block to CPU addresses.
 *
 * Board fallback rules tell apart controllers with the same compatible string
 * by checking their CPU register address. Since the heap is not ready yet,
 * this translates @p device's first "reg" entry straight through its parent
 * simple-bus's "ranges" property (requiring 1 address cell and 1 size cell on
 * that bus). Returns false if the register cannot be decoded or mapped. That
 * just means no board rule can match this device.
 */
static bool
__ReadCpuRegister(
    _In_  const struct __PoolNode* root,
    _In_  const struct __PoolNode* bus,
    _In_  const struct __PoolNode* device,
    _Out_ uint64_t*                baseOut,
    _Out_ uint64_t*                lengthOut)
{
    const uint8_t*     reg = device->Values[__PoolReg].Bytes;
    const uint8_t*     ranges = bus->Values[__PoolRanges].Bytes;
    uint32_t           rangesLength = bus->Values[__PoolRanges].Length;
    uint32_t           stride;
    uint64_t           base;
    uint64_t           length;
    uint64_t           physical = 0;
    uint64_t           parentLimit;
    uint32_t           matches = 0;

    if (bus->AddressCells != 1 || bus->SizeCells != 1 || reg == NULL ||
        device->Values[__PoolReg].Length < 8 || device->Values[__PoolReg].Length % 8 != 0 ||
        (root->AddressCells != 1 && root->AddressCells != 2) || ranges == NULL) {
        return false;
    }

    base = FdtReadBe32(reg);
    length = FdtReadBe32(reg + 4);
    stride = (root->AddressCells + 2) * sizeof(uint32_t);
    if (length == 0 || length - 1 > UINT32_MAX - base || rangesLength % stride != 0) {
        return false;
    }

    // Each "ranges" entry holds <child-addr (1 cell), root-addr (root->AddressCells),
    // span (1 cell)>. Reject overflows and require exactly one enclosing range.
    parentLimit = root->AddressCells == 1 ? UINT32_MAX : UINT64_MAX;
    for (uint32_t offset = 0; offset < rangesLength; offset += stride) {
        uint64_t child = FdtReadBe32(ranges + offset);
        uint64_t parent = FdtReadCells(ranges + offset + 4, root->AddressCells);
        uint64_t span = FdtReadBe32(ranges + offset + stride - 4);

        if (span == 0 || span - 1 > UINT32_MAX - child || span - 1 > parentLimit - parent) {
            return false;
        }

        if (base <= child + (span - 1) && child <= base + (length - 1)) {
            if (base < child || length > span - (base - child)) {
                return false;
            }
            physical = parent + (base - child);
            matches++;
        }
    }
    if (matches != 1) {
        return false;
    }

    *baseOut = physical;
    *lengthOut = length;
    return true;
}

/**
 * @brief Ask libfdt for a board fallback route when a bus has no dma-ranges.
 *
 * Only applies to a three-level path (root -> simple-bus -> device) where the
 * bus omitted dma-ranges. Validates the device, bus, and root with the shared
 * direct-DMA checks, resolves the device's CPU register address so rules can
 * identify the controller, and composes any matching rule with root->Map.
 */
static oserr_t
__BoardRoute(
    _InOut_ struct __PoolScan* scan,
    _InOut_ struct __PoolNode* device)
{
    struct __PoolNode* root = &scan->Nodes[0];
    struct __PoolNode* bus = &scan->Nodes[1];
    FdtDmaBoardPath_t  path = { 0 };
    FdtDmaBusWindows_t dma;
    FdtDmaNode_t       description;
    oserr_t            status;

    if (scan->Depth != 2 || bus->Values[__PoolDmaRanges].Bytes != NULL ||
        !__ReadCpuRegister(root, bus, device, &path.RegisterBase, &path.RegisterLength)) {
        return OS_ENOTSUPPORTED;
    }

    __DescribeNode(device, bus, &description);
    if (!FdtDmaDeviceIsDirect(&description, NULL)) {
        return OS_ENOTSUPPORTED;
    }
    __DescribeNode(bus, root, &description);
    if (!FdtDmaBusIsDirect(&description, NULL)) {
        return OS_ENOTSUPPORTED;
    }
    __DescribeNode(root, NULL, &description);
    if (!FdtDmaBusIsDirect(&description, NULL)) {
        return OS_ENOTSUPPORTED;
    }

    path.RootCompatible = root->Values[__PoolCompatible].Bytes;
    path.RootCompatibleLength = root->Values[__PoolCompatible].Length;
    path.BusCompatible = bus->Values[__PoolCompatible].Bytes;
    path.BusCompatibleLength = bus->Values[__PoolCompatible].Length;
    path.BusAddressCells = bus->AddressCells;
    path.BusSizeCells = bus->SizeCells;
    path.DeviceCompatible = device->Values[__PoolCompatible].Bytes;
    path.DeviceCompatibleLength = device->Values[__PoolCompatible].Length;
    status = FdtDmaBoardRoute(&path, &dma);
    if (status != OS_EOK) {
        return status;
    }

    return FdtDmaCompose(&dma, &root->Map, &device->Map);
}

/**
 * Skip nodes that FdtDmaNodeIsDirect rejects: disabled nodes, nodes DMA
 * reaches through an IOMMU or dma-parent, nodes with memory-region, and simple-mfd
 * nodes holding dma-ranges for sub-drivers. A node below a skipped node
 * is skipped too. This check does not use FdtDmaBusIsDirect. It must see every
 * bus that constrains DMA, including PCI hosts and non-simple-bus bridges,
 * because it narrows the shared pool rather than resolving one device's route.
 */
static bool
__IsSkipped(
    _In_ const struct __PoolNode* node,
    _In_ const struct __PoolNode* parent)
{
    FdtDmaNode_t description;

    if (parent != NULL && parent->State == __PoolNodeSkipped) {
        return true;
    }

    __DescribeNode(node, parent, &description);
    return !FdtDmaNodeIsDirect(&description, NULL);
}

/**
 * Computes the DMA route and resulting state for the node at scan->Depth.
 * Any returned error is stored on the node by __PrepareNode rather than failing
 * immediately, in case the node turns out to be an unused bus.
 */
static oserr_t
__ReadNodeRoute(
    _InOut_ struct __PoolScan* scan)
{
    struct __PoolNode* node = &scan->Nodes[scan->Depth];
    struct __PoolNode* parent = scan->Depth == 0 ? NULL : &scan->Nodes[scan->Depth - 1];
    FdtDmaBusWindows_t windows;
    oserr_t            status;

    // FdtDmaNodeIsDirect needs to know if this node is a PCI host or bridge.
    node->Pci = __HasString(node, __PoolDeviceType, "pci");
    if (__IsSkipped(node, parent)) {
        node->State = __PoolNodeSkipped;
        return OS_EOK;
    }

    if (parent != NULL) {
        parent->HasActiveChild = true;
    }

    if (node->DuplicateProperty) {
        return OS_EINVALPARAMS;
    }

    // Device Tree defaults are #address-cells = 2 and #size-cells = 1 when omitted.
    status = __ReadCellCount(node, __PoolAddressCells, 2, &node->AddressCells);
    if (status != OS_EOK) {
        return status;
    }

    status = __ReadCellCount(node, __PoolSizeCells, 1, &node->SizeCells);
    if (status != OS_EOK) {
        return status;
    }

    if (parent == NULL) {
        // The root node starts with all system RAM clipped to its #address-cells width.
        if (node->AddressCells != 1 && node->AddressCells != 2) {
            return OS_ENOTSUPPORTED;
        }

        windows = (FdtDmaBusWindows_t) {
            .State = FdtDmaPropertyIdentity,
            .ChildLimit = node->AddressCells == 1 ? UINT32_MAX : UINT64_MAX
        };

        status = FdtDmaCompose(&windows, &scan->Ram, &node->Map);
        if (status != OS_EOK) {
            return status;
        }

        node->State = __PoolNodeValid;
        return OS_EOK;
    }

    if (node->Values[__PoolDmaRanges].Bytes == NULL) {
        // No dma-ranges property here. Check for a board fallback route first.
        status = __BoardRoute(scan, node);
        if (status == OS_EOK) {
            node->State = __PoolNodeBoardRoute;
            node->LimitsPool = true;
            return OS_EOK;
        }
        if (status != OS_ENOTSUPPORTED) {
            return status;
        }

        // Leave this node with no route rather than assuming 1:1 DMA. If any
        // child later tries to use dma-ranges through this bus, it will fail.
        node->State = __PoolNodeNoRoute;
        return OS_EOK;
    }

    if (parent->State != __PoolNodeValid) {
        return parent->State == __PoolNodeError ? parent->Error : OS_ENOTSUPPORTED;
    }

    status = __DecodeBus(node, parent, &windows);
    if (status != OS_EOK) {
        return status;
    }

    status = FdtDmaCompose(&windows, &parent->Map, &node->Map);
    if (status != OS_EOK) {
        return status;
    }

    node->State = __PoolNodeValid;
    node->LimitsPool = true;
    return OS_EOK;
}

// Saves the node name and generic reason for MmuGetDevicePool to log later.
// The root node's name is empty in FDT, so display it as "/".
static oserr_t
__FailNode(
    _InOut_ struct __PoolScan*       scan,
    _In_    const struct __PoolNode* node,
    _In_    oserr_t                  status)
{
    scan->Reason = "DMA route rejected by node";
    scan->FailedNode = node->Name[0] != 0 ? node->Name : "/";
    return status;
}

/**
 * Evaluates the current node once all of its properties have been read.
 *
 * Route errors on non-root nodes are saved in node->Error and checked when the
 * node closes in __EndNode. That way, a bus whose children are all disabled is
 * ignored instead of breaking DMA for working devices. Root errors fail right
 * away because every direct route starts at the root.
 */
static oserr_t
__PrepareNode(
    _InOut_ struct __PoolScan* scan)
{
    struct __PoolNode* node = &scan->Nodes[scan->Depth];
    oserr_t            status;

    if (node->State == __PoolNodePending) {
        status = __ReadNodeRoute(scan);
        if (status != OS_EOK) {
            node->State = __PoolNodeError;
            node->Error = status;
        }
    }

    if (scan->Depth == 0 && node->State == __PoolNodeError) {
        return __FailNode(scan, node, node->Error);
    }
    return OS_EOK;
}

// FDT streams all properties of a node before entering its first child node.
// Finalize the parent's route before pushing a child onto scan->Nodes.
static oserr_t
__BeginNode(
    _InOut_ void*       context,
    _In_    const char* name,
    _In_    uint32_t    nameLength)
{
    struct __PoolScan* scan = context;
    struct __PoolNode* node;
    oserr_t            status;

    (void)nameLength;
    if (scan->Depth >= 0) {
        scan->Nodes[scan->Depth].HasChildren = true;
        status = __PrepareNode(scan);
        if (status != OS_EOK) {
            return status;
        }
    }
    
    if (scan->Depth + 1 == FDT_MAX_DEPTH) {
        return OS_ENOTSUPPORTED;
    }

    node = &scan->Nodes[++scan->Depth];
    memset(node, 0, sizeof(struct __PoolNode));
    node->Name = name;
    return OS_EOK;
}

// Records any DMA-relevant property seen on the node at scan->Depth.
static oserr_t
__Property(
    _InOut_ void*       context,
    _In_    const char* name,
    _In_    const void* bytes,
    _In_    uint32_t    length)
{
    struct __PoolScan* scan = context;
    struct __PoolNode* node = &scan->Nodes[scan->Depth];

    for (uint32_t i = 0; i < __PoolPropertyCount; i++) {
        if (strcmp(name, g_propertyNames[i]) == 0) {
            if (node->Seen & (1U << i)) {
                node->DuplicateProperty = true;
            }

            node->Seen |= 1U << i;
            node->Values[i] = (struct __PoolPropertyValue) { bytes, length };
            break;
        }
    }
    return OS_EOK;
}

// Finalizes leaf nodes and applies active nodes' DMA bounds when closing.
static oserr_t
__EndNode(
    _InOut_ void* context)
{
    struct __PoolScan* scan = context;
    struct __PoolNode* node = &scan->Nodes[scan->Depth];
    oserr_t            status;

    status = __PrepareNode(scan);
    if (status != OS_EOK) {
        return status;
    }

    // Only nodes that can have real DMA devices narrow the shared pool: leaves,
    // buses with at least one enabled child, and PCI hosts (whose devices are
    // discovered by probing PCI rather than listed in the Device Tree).
    if (node->State != __PoolNodeSkipped &&
        (node->Pci || !node->HasChildren || node->HasActiveChild)) {
        if (node->State == __PoolNodeError) {
            return __FailNode(scan, node, node->Error);
        }

        if (node->LimitsPool) {
            status = __IntersectRoute(scan, &node->Map);
            if (status != OS_EOK) {
                return __FailNode(scan, node, status);
            }
        }
    }
    scan->Depth--;
    return OS_EOK;
}

// Verifies the DTB lives in mapped firmware RAM, seeds Ram/Common from the
// boot memory map, and walks the raw FDT structure block.
static oserr_t
__ScanTree(
    _In_    const struct VBoot* boot,
    _InOut_ struct __PoolScan*  scan)
{
    const struct VBootMemoryEntry* entries = (const void*)(uintptr_t)boot->Memory.Entries;
    const uint8_t*                 bytes = (const void*)(uintptr_t)boot->DeviceTree.PhysicalBase;
    struct FDTHeader               header;
    oserr_t                        status;

    struct FdtParser parser = {
        .BeginNode = __BeginNode,
        .Property = __Property,
        .EndNode = __EndNode,
        .UserData = scan
    };

    // Confirm every byte of the DTB sits inside mapped Firmware RAM before
    // reading its header.
    if (boot->DeviceTree.PhysicalBase == 0 || boot->DeviceTree.Length < sizeof(struct FDTHeader) ||
        !Arm64BootMemoryRangeIsFirmware(
            boot,
            boot->DeviceTree.PhysicalBase,
            boot->DeviceTree.Length,
            MEMORY_LOCATION_RING3_CODE)) {
        return OS_EINVALPARAMS;
    }

    status = FdtParseHeader(bytes, boot->DeviceTree.Length, &header);
    if (status != OS_EOK) {
        return status;
    }

    // Seed the starting pool bounds from usable RAM entries. Loader-marked
    // DevicePool regions stay included as candidates, while other reserved
    // ranges and NO_MAP entries are left out.
    for (uint32_t i = 0; i < boot->Memory.NumberOfEntries; i++) {
        const struct VBootMemoryEntry* entry = &entries[i];

        if (entry->Type != VBootMemoryType_Available && entry->Type != VBootMemoryType_DevicePool) {
            continue;
        }

        if (scan->Count == FDT_DMA_MAX_RANGES) {
            return OS_ENOTSUPPORTED;
        }

        scan->Ram.Ranges[scan->Count] = (FdtDmaRange_t) {
            entry->PhysicalBase,
            entry->PhysicalBase,
            entry->Length
        };
        scan->Common[scan->Count++] = __PhysicalRange(entry->PhysicalBase, entry->Length);
    }

    scan->Ram.Count = scan->Count;
    status = FdtParseStructure(
        bytes + header.OffDtStruct,
        header.SizeDtStruct,
        (const char*)bytes + header.OffDtStrings,
        header.SizeDtStrings,
        &parser
    );
    if (status != OS_EOK) {
        return status;
    }

    // Require at least one node to contribute a DMA route so a tree with no
    // DMA descriptions does not silently pass.
    if (scan->Routes == 0) {
        scan->Reason = "no node in the device tree has a DMA route";
        return OS_ENOTSUPPORTED;
    }
    return OS_EOK;
}

oserr_t
Arm64FindDevicePool(
    _In_  const struct VBoot*   boot,
    _In_  size_t                size,
    _In_  size_t                alignment,
    _Out_ PlatformDevicePool_t* poolOut,
    _Out_ const char**          reasonOut,
    _Out_ const char**          nodeOut)
{
    PlatformDevicePool_t pool = { .Cached = false };
    oserr_t              status;

    if (boot == NULL || poolOut == NULL || reasonOut == NULL || nodeOut == NULL) {
        return OS_EINVALPARAMS;
    }

    memset(&g_scan, 0, sizeof(struct __PoolScan));
    g_scan.Depth = -1;
    g_scan.Reason = "the device tree could not be scanned";
    g_scan.FailedNode = "";

    status = __ScanTree(boot, &g_scan);
    if (status == OS_EOK) {
        status = SystemMemoryFitDeviceRegionInRanges(
            boot, size, alignment, g_scan.Common, g_scan.Count, &pool.Base, &pool.Length
        );
        if (status != OS_EOK) {
            g_scan.Reason = "no RAM reachable by every DMA route fits the pool";
        }
    }

    if (status != OS_EOK) {
        *reasonOut = g_scan.Reason;
        *nodeOut = g_scan.FailedNode;
        return status;
    }

    *poolOut = pool;
    return OS_EOK;
}
