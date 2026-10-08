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
 */

#ifndef __FIRMWARE_DMA_H__
#define __FIRMWARE_DMA_H__

#include <os/osdefs.h>

// Maximum number of address windows accepted from one bus.
#define FDT_DMA_MAX_WINDOWS 16

// Maximum number of resulting device-to-physical-memory ranges.
#define FDT_DMA_MAX_RANGES 64

struct FdtPciHost;
struct FdtResources;

// Describes how addresses are stored in the firmware device-tree data.
enum FdtDmaAddressFormat {
    FdtDmaAddressSimple, /**< One or two 32-bit values form the address. */
    FdtDmaAddressPci    /**< Three values form a PCI address, including flags. */
};

// Tells whether a bus has no DMA mapping, a direct mapping, or listed windows.
enum FdtDmaPropertyState {
    FdtDmaPropertyAbsent,   /**< No dma-ranges property was supplied. */
    FdtDmaPropertyIdentity, /**< An empty property means addresses pass through unchanged. */
    FdtDmaPropertyWindows   /**< The property contains one or more address mappings. */
};

/**
 * @brief One address mapping listed for a bus.
 *
 * The window maps a continuous block of addresses used by a child device to
 * the same-sized block of addresses on its parent bus. It describes only this
 * one step in the path; the parent address is not necessarily a physical RAM
 * address. A FdtDmaRange is different: it is produced after bus mappings have
 * been followed far enough to identify the physical memory reachable by a
 * device.
 *
 * For PCI addresses, the attribute fields hold the PCI flags separately from
 * the numeric address. For simple addresses, both attribute fields are zero.
 */
struct FdtDmaWindow {
    uint64_t ChildBase;
    uint64_t ParentBase;
    uint64_t Length;
    uint32_t ChildAttributes;
    uint32_t ParentAttributes;
};

/**
 * @brief Decoded description of one bus's DMA address rules.
 *
 * This structure owns the decoded values; it does not refer back to the
 * firmware property's bytes. State distinguishes a missing property from an
 * empty property and from a property containing mappings. Count is the number
 * of entries in Windows. ChildLimit is the largest address the child's
 * address format can represent, further limited when the property says
 * addresses pass through unchanged.
 */
struct FdtDmaRanges {
    enum FdtDmaPropertyState State;
    uint64_t                 ChildLimit;
    uint32_t                 Count;
    struct FdtDmaWindow      Windows[FDT_DMA_MAX_WINDOWS];
};

/**
 * @brief One continuous device-address block that maps to physical memory.
 *
 * Unlike FdtDmaWindow, this is not just one bus-to-parent mapping. It is a
 * result of following the bus mappings and matching them with the parent map:
 * DeviceBase is an address the device can use, and PhysicalBase is the
 * corresponding address in physical memory. Length gives the number of bytes
 * covered. This describes where DMA could reach; it does not say that memory
 * has been allocated, that the full buffer fits, or that DMA hardware is ready.
 */
struct FdtDmaRange {
    uint64_t PhysicalBase;
    uint64_t DeviceBase;
    uint64_t Length;
};

/**
 * @brief List of device-to-physical-memory ranges available for DMA.
 *
 * The ranges are copied into this structure and sorted by DeviceBase. Their
 * device-address blocks do not overlap. Two different device-address blocks
 * can still refer to some or all of the same physical memory. Adjacent entries
 * are kept separate when the address mapping changes at their boundary.
 */
struct FdtDmaMap {
    uint32_t Count;
    struct FdtDmaRange Ranges[FDT_DMA_MAX_RANGES];
};

/**
 * @brief Read and validate one bus's DMA address rules.
 *
 * The function reads the bus's dma-ranges property and copies its address
 * mappings into ranges. Each mapping relates addresses used by a device below
 * this bus to addresses on this bus's parent. It does not choose a DMA address
 * or access hardware. A missing property and an empty property are recorded as
 * different states because they have different meanings to the caller.
 *
 * @param bus Parsed bus information, including address sizes and property bytes.
 * @param childFormat Address format used for addresses below this bus.
 * @param parentFormat Address format used for addresses on this bus's parent.
 * @param ranges Receives the decoded description only if the function succeeds.
 * @return OS_EOK on success. OS_EINVALPARAMS means the property is malformed
 *         or contains invalid or overlapping address blocks. OS_ENOTSUPPORTED
 *         means the address format, PCI address type, or number of entries is
 *         not supported. A missing or empty property still succeeds and is
 *         identified by ranges->State.
 */
oserr_t
FdtDecodeDmaRanges(
    _In_  const struct FdtResources* bus,
    _In_  enum FdtDmaAddressFormat   childFormat,
    _In_  enum FdtDmaAddressFormat   parentFormat,
    _Out_ struct FdtDmaRanges*      ranges);

/**
 * @brief Combine one bus's mappings with the parent's known physical memory.
 *
 * The child description says how addresses used below this bus map to addresses
 * on its parent. The parent map says which of the parent's addresses reach
 * physical memory. This function combines those steps and returns only the
 * portions that reach physical memory. The result is a list of
 * FdtDmaRange entries, not a list of the original bus windows. A missing
 * dma-ranges property is not treated as a direct mapping; an empty property is.
 *
 * @param child Decoded address rules for the child bus.
 * @param parent Known device-address-to-physical-memory ranges for the parent.
 *               Parent device-address blocks must not overlap, and every range
 *               must have a nonzero length that fits in the 64-bit address space.
 * @param map Receives the sorted result only if the function succeeds. This may
 *            point to the same structure as parent. Separate result entries
 *            are kept even when they touch, since their mappings may differ.
 * @return OS_EOK on success. OS_ENOENT means none of the child addresses reach
 *         known physical memory. OS_EINVALPARAMS means an input range is
 *         invalid. OS_ENOTSUPPORTED means the child's mapping is absent or the
 *         result would exceed the available space in map. Portions not covered
 *         by both inputs are left out, so callers must check that a complete
 *         buffer fits within the returned ranges.
 */
oserr_t
FdtComposeDmaRanges(
    _In_  const struct FdtDmaRanges* child,
    _In_  const struct FdtDmaMap*    parent,
    _Out_ struct FdtDmaMap*          map);

/**
 * @brief Copy a PCI host's RAM windows into the common DMA range format.
 *
 * Both firmware inspection and host drivers need the same filtering. Check all
 * windows before removing peer-register and interrupt destinations, because an
 * overlap with RAM would make a device address ambiguous. Preserve the original
 * lengths: hardware rounding must not enlarge the memory we report as usable.
 *
 * @param host Decoded host; this helper does not check hardware setup.
 * @param map Receives sorted RAM ranges only on success; otherwise unchanged.
 * @return OS_EOK, OS_ENOENT for no RAM, OS_EINVALPARAMS for invalid ranges or
 *         arguments, or OS_ENOTSUPPORTED for unsupported encodings or capacity.
 */
oserr_t
FdtPciDmaMap(
    _In_  const struct FdtPciHost* host,
    _Out_ struct FdtDmaMap*        map);

#endif
