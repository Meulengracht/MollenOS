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
 * Device Tree DMA window decoder and composer
 * Decodes "dma-ranges" bus properties and follows them up the device tree to
 * find which CPU physical RAM addresses a device can reach over DMA.
 */

#ifndef __FDT_DMA_H__
#define __FDT_DMA_H__

#include <os/osdefs.h>

#define FDT_DMA_MAX_RANGES 64

/**
 * @brief One resolved DMA memory interval connecting a device to physical RAM.
 *
 * Devices do not always use the same numeric addresses as the CPU. When a
 * bus shifts addresses, DeviceBase is the starting address the device puts on
 * its bus, and PhysicalBase is the matching CPU physical RAM address. Length
 * is the interval size in bytes.
 *
 * Produced by FdtDmaCompose after following every bus step toward RAM. See
 * FdtDmaBusWindows_t for the raw windows of a single bus before combining.
 */
typedef struct FdtDmaRange {
    uint64_t PhysicalBase;
    uint64_t DeviceBase;
    uint64_t Length;
} FdtDmaRange_t;

// Sorted list of resolved DMA intervals. Only the first Count entries are
// valid. Touching intervals stay separate when their address offset changes
// at the boundary.
typedef struct FdtDmaMap {
    uint32_t      Count;
    FdtDmaRange_t Ranges[FDT_DMA_MAX_RANGES];
} FdtDmaMap_t;

// Maximum number of dma-ranges windows accepted from a single bus.
#define FDT_DMA_MAX_WINDOWS 16

// Selects how addresses in a dma-ranges property are encoded.
enum FdtDmaAddressFormat {
    // Standard Device Tree address made of one or two 32-bit numbers (cells).
    FdtDmaAddressSimple,
    // Three 32-bit cells for a PCI address: flags word plus 64-bit address.
    FdtDmaAddressPci
};

// Distinguishes the three states a bus's dma-ranges property can have.
enum FdtDmaPropertyState {
    // The bus has no dma-ranges property. No direct DMA route is known.
    FdtDmaPropertyAbsent,
    // The dma-ranges property exists and is empty. Addresses pass through 1:1.
    FdtDmaPropertyIdentity,
    // The dma-ranges property lists one or more explicit translation windows.
    FdtDmaPropertyWindows
};

/**
 * @brief One address translation entry from a bus's dma-ranges property.
 *
 * A window maps a block of addresses used by a child device (ChildBase) onto
 * the same-sized block on that bus's parent (ParentBase). This covers only
 * one hop in the tree. ParentBase is a parent-bus address, which can be
 * shifted again by buses higher up before reaching physical RAM.
 *
 * For PCI addresses, ChildAttributes and ParentAttributes hold the flags word
 * from the first PCI cell. For simple addresses, both fields are zero.
 */
typedef struct FdtDmaWindow {
    uint64_t ChildBase;
    uint64_t ParentBase;
    uint64_t Length;
    uint32_t ChildAttributes;
    uint32_t ParentAttributes;
} FdtDmaWindow_t;

/**
 * @brief Decoded DMA rules for one bus, stored independently of the raw FDT bytes.
 *
 * State tells callers whether the bus had no property, an empty 1:1 property,
 * or explicit windows in Windows[0..Count-1]. ChildLimit is the highest address
 * the child's address size can express (4 GiB - 1 for one cell, or full
 * 64-bit for two cells). When dma-ranges is empty, ChildLimit is narrowed if
 * the parent's address size is smaller.
 */
typedef struct FdtDmaBusWindows {
    enum FdtDmaPropertyState State;
    uint64_t                 ChildLimit;
    uint32_t                 Count;
    FdtDmaWindow_t           Windows[FDT_DMA_MAX_WINDOWS];
} FdtDmaBusWindows_t;

// Raw dma-ranges bytes and cell sizes for one bus. Set Bytes to NULL when
// the property is missing. Set Bytes to a non-NULL pointer with Length 0
// when the property is present and empty (1:1 pass-through).
typedef struct FdtDmaProperty {
    const uint8_t*           Bytes;
    uint32_t                 Length;
    uint32_t                 ChildCells;
    uint32_t                 ParentCells;
    uint32_t                 SizeCells;
    enum FdtDmaAddressFormat ChildFormat;
    enum FdtDmaAddressFormat ParentFormat;
} FdtDmaProperty_t;

/**
 * @brief Plain facts about one Device Tree node used to decide its DMA path.
 *
 * Different callers read the Device Tree in different ways (flat FDT scan or
 * parsed tree objects). Filling this struct lets every caller share the exact
 * same direct-DMA rules.
 *
 * Compatible points to the node's raw "compatible" string list, or NULL if
 * missing. The Has* fields record whether each named property is present, even
 * if it has zero length. Disabled covers the whole chain above this node: set
 * it if this node or any parent has a status other than "okay" or "ok". All
 * other fields describe this node by itself.
 */
typedef struct FdtDmaNode {
    const void* Compatible;
    uint32_t    CompatibleLength;
    bool        IsRoot;
    bool        IsPci;
    bool        ParentIsPci;
    bool        Disabled;
    bool        HasIommus;
    bool        HasIommuMap;
    bool        HasDmaParent;
    bool        HasDmaRanges;
    bool        HasMemoryRegion;
} FdtDmaNode_t;

// Explains why a node cannot use a direct DMA path to RAM.
enum FdtDmaIndirectReason {
    // This node or one of its parents is disabled in the tree.
    FdtDmaIndirectDisabled,
    // The node has iommus or iommu-map, so an IOMMU hardware translator handles its DMA.
    FdtDmaIndirectIommu,
    // The node has dma-parent, which sends DMA to another bus instead of its tree parent.
    FdtDmaIndirectDmaParent,
    // The node has memory-region, which ties it to a reserved block with custom rules.
    FdtDmaIndirectMemoryRegion,
    // The node has dma-ranges for sub-drivers (like a simple-mfd block) and is
    // not a simple-bus, a PCI host, or a bus below PCI.
    FdtDmaIndirectFirmwareBus,
    // The node is a PCI device or host. PCI uses the host controller's own windows.
    FdtDmaIndirectPci,
    // Reported by FdtDmaBusIsDirect when a parent bus lacks "simple-bus" compatibility.
    FdtDmaIndirectNotSimpleBus
};

/**
 * @brief Check whether a node can sit on a direct DMA path to RAM.
 *
 * A direct path goes only through simple-bus nodes, PCI hosts, or buses below
 * a PCI host. Any disabled node, IOMMU-translated node, dma-parent redirect,
 * memory-region binding, or non-standard firmware bus with dma-ranges stops
 * the path from being direct. The root node itself is never treated as a
 * firmware bus.
 *
 * Disabled already covers parents. Every other flag only checks this single
 * node, so callers must check every node along the path. Allocates no memory.
 *
 * Device pool scans call this directly so PCI hosts can narrow the shared pool.
 * Platform device resolution uses FdtDmaDeviceIsDirect on the device and
 * FdtDmaBusIsDirect on every bus above it.
 *
 * @param node   Node properties to check.
 * @param reason Optional output set to the first failing rule. Unchanged when direct.
 * @return true if the node is allowed on a direct DMA path.
 */
__EXTERN bool
FdtDmaNodeIsDirect(
    _In_      const FdtDmaNode_t*        node,
    _Out_Opt_ enum FdtDmaIndirectReason* reason);

/**
 * @brief Check whether a leaf device can use a direct platform DMA path.
 *
 * Runs FdtDmaNodeIsDirect and rejects PCI nodes, since PCI devices resolve
 * DMA through their PCI host controller instead of platform bus rules. A leaf
 * device does not need to be a "simple-bus". Callers still check every parent
 * bus separately with FdtDmaBusIsDirect.
 *
 * @param node   Device node properties to check.
 * @param reason Optional output set to the failing rule. Unchanged when direct.
 * @return true if the device can use direct DMA.
 */
__EXTERN bool
FdtDmaDeviceIsDirect(
    _In_      const FdtDmaNode_t*        node,
    _Out_Opt_ enum FdtDmaIndirectReason* reason);

/**
 * @brief Check whether a parent bus sits on a direct simple-bus DMA path.
 *
 * Runs FdtDmaNodeIsDirect, rejects PCI nodes, and requires the bus to be
 * compatible with "simple-bus" (except the root node, which only needs to pass
 * the non-PCI and direct checks). Use this on buses above a device, never on
 * the leaf device itself.
 *
 * @param node   Bus node properties to check.
 * @param reason Optional output set to the failing rule. Unchanged when direct.
 * @return true if this bus can carry direct DMA for devices below it.
 */
__EXTERN bool
FdtDmaBusIsDirect(
    _In_      const FdtDmaNode_t*        node,
    _Out_Opt_ enum FdtDmaIndirectReason* reason);

// Names the exact validation check that rejected a property in FdtDmaDecode.
enum FdtDmaFaultKind {
    FdtDmaFaultNone,
    // Child cell count and address format do not match a supported pairing.
    FdtDmaFaultChildFormat,
    // Parent cell count and address format do not match a supported pairing.
    FdtDmaFaultParentFormat,
    // Size cell count is zero or larger than two.
    FdtDmaFaultSizeCells,
    // Length is nonzero while Bytes is NULL.
    FdtDmaFaultMissingBytes,
    // Property byte length is not a whole multiple of one window entry.
    FdtDmaFaultTruncatedWindow,
    // Property lists more than FDT_DMA_MAX_WINDOWS entries.
    FdtDmaFaultTooManyWindows,
    // Child address in Window is not a supported PCI memory address.
    FdtDmaFaultChildAddress,
    // Parent address in Window is not a supported PCI memory address.
    FdtDmaFaultParentAddress,
    // Window has zero length or wraps past the parent address space limit.
    FdtDmaFaultParentExtent,
    // Window has zero length or wraps past the child address space limit.
    FdtDmaFaultChildExtent,
    // Window and OtherWindow cover overlapping child addresses.
    FdtDmaFaultOverlap
};

// Details of a failed FdtDmaDecode call. Window is the zero-based index of
// the offending entry, and OtherWindow is the earlier entry it overlaps. Both
// apply only to the fault kinds that mention them. Count is set only for
// FdtDmaFaultTooManyWindows and reports how many windows the property held.
typedef struct FdtDmaFault {
    enum FdtDmaFaultKind Kind;
    uint32_t             Window;
    uint32_t             OtherWindow;
    uint32_t             Count;
} FdtDmaFault_t;

/**
 * @brief Decode and validate one bus's dma-ranges property without allocating
 *        or logging.
 *
 * Checks cell counts, PCI memory flags, address bounds, and child-address
 * overlaps. A 32-bit PCI memory window must end at or below 4 GiB. Output
 * stays untouched on failure. A missing property (State = FdtDmaPropertyAbsent)
 * and an empty property (State = FdtDmaPropertyIdentity) both succeed and
 * stay distinct.
 *
 * @param property Raw property bytes, length, and address formats for one bus.
 * @param windows  Receives the decoded windows on success.
 * @param fault    Optional output describing which check failed. Unchanged on success.
 * @return OS_EOK on success, OS_EINVALPARAMS for malformed windows or NULL
 *         arguments, or OS_ENOTSUPPORTED for unsupported formats or counts.
 */
__EXTERN oserr_t
FdtDmaDecode(
    _In_      const FdtDmaProperty_t* property,
    _Out_     FdtDmaBusWindows_t*     windows,
    _Out_Opt_ FdtDmaFault_t*          fault);

/**
 * @brief Combine one child bus's DMA windows with its parent's RAM map.
 *
 * Matches the child's parent-side windows against the parent's reachable
 * ranges, keeps only the overlapping bytes, and outputs the resulting
 * child-address to physical-RAM intervals sorted by DeviceBase. Touching
 * ranges stay separate because address offsets can change at boundaries.
 *
 * Allocates no memory. Output may point to the same struct as @p parent and
 * remains unchanged on error.
 *
 * @param child  Decoded DMA windows for the child bus.
 * @param parent Resolved RAM ranges for the parent bus.
 * @param map    Receives the combined DMA map on success.
 * @return OS_EOK on success, OS_ENOENT if no addresses overlap,
 *         OS_ENOTSUPPORTED if the child has no route or exceeds FDT_DMA_MAX_RANGES,
 *         or OS_EINVALPARAMS for NULL or invalid inputs.
 */
__EXTERN oserr_t
FdtDmaCompose(
    _In_  const FdtDmaBusWindows_t* child,
    _In_  const FdtDmaMap_t*        parent,
    _Out_ FdtDmaMap_t*              map);

#endif //!__FDT_DMA_H__
