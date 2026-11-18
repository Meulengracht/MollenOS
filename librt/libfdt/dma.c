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
 * Parses bus "dma-ranges" properties, validates address bounds and overlaps,
 * combines bus translations with parent RAM maps, and checks direct DMA paths.
 */

#include <fdt/dma.h>
#include <fdt/pci.h>
#include <fdt/reader.h>

// Checks that [base, base + length - 1] is non-empty and fits at or below limit.
static bool
__DmaExtentValid(
    _In_ uint64_t base,
    _In_ uint64_t length,
    _In_ uint64_t limit)
{
    // Subtract instead of adding base + length so ranges ending at UINT64_MAX
    // do not wrap around to zero.
    return length != 0 && base <= limit && length - 1 <= limit - base;
}

// Checks whether two half-open intervals [first, first + firstLength) and
// [second, second + secondLength) share any byte.
static bool
__DmaOverlaps(
    _In_ uint64_t first,
    _In_ uint64_t firstLength,
    _In_ uint64_t second,
    _In_ uint64_t secondLength)
{
    return first <= second ? second - first < firstLength : first - second < secondLength;
}

// Returns the highest numeric address representable for a given cell layout.
static oserr_t
__DmaAddressLimit(
    _In_  enum FdtDmaAddressFormat format,
    _In_  uint32_t                 cells,
    _Out_ uint64_t*                limit)
{
    if (format == FdtDmaAddressSimple && (cells == 1 || cells == 2)) {
        *limit = cells == 1 ? UINT32_MAX : UINT64_MAX;
        return OS_EOK;
    }

    // PCI addresses carry one flags cell plus two 32-bit address cells (64 bits total).
    if (format == FdtDmaAddressPci && cells == 3) {
        *limit = UINT64_MAX;
        return OS_EOK;
    }
    return OS_ENOTSUPPORTED;
}

// Reads either a 1-to-2-cell simple address or a 3-cell PCI memory address.
static oserr_t
__DmaDecodeAddress(
    _In_  const uint8_t*           bytes,
    _In_  uint32_t                 cells,
    _In_  enum FdtDmaAddressFormat format,
    _Out_ uint64_t*                address,
    _Out_ uint32_t*                attributes)
{
    *attributes = 0;

    if (format == FdtDmaAddressPci) {
        FdtPciSpace_t space;

        *attributes = FdtReadBe32(bytes);
        space = FDT_PCI_SPACE_FROM_ATTRIBUTES(*attributes);

        // The first PCI cell holds flags and the address-space kind. DMA can
        // target 32-bit or 64-bit PCI memory, with optional prefetch and
        // relocatable bits, while I/O and configuration spaces are invalid.
        if ((space != FdtPciSpaceMemory32 && space != FdtPciSpaceMemory64) ||
            (*attributes & ~FDT_DMA_PCI_VALID_FLAGS)) {
            return OS_ENOTSUPPORTED;
        }

        bytes += 4;
        cells--;
    }

    *address = FdtReadCells(bytes, cells);
    return OS_EOK;
}

// Saves failure details for callers that want diagnostics, or ignores NULL.
static void
__DmaSetFault(
    _Out_Opt_ FdtDmaFault_t*          fault,
    _In_      enum FdtDmaFaultKind    kind,
    _In_      uint32_t                window,
    _In_      uint32_t                otherWindow)
{
    if (fault != NULL) {
        fault->Kind = kind;
        fault->Window = window;
        fault->OtherWindow = otherWindow;
    }
}

// Validates that every window fits inside both child and parent address limits
// and that no two windows cover the same child bus addresses.
static oserr_t
__DmaCheckWindows(
    _In_      const FdtDmaBusWindows_t* child,
    _In_      uint64_t                  parentLimit,
    _Out_Opt_ FdtDmaFault_t*            fault)
{
    const FdtDmaWindow_t* window;
    uint64_t              limit;

    for (uint32_t i = 0; i < child->Count; i++) {
        window = &child->Windows[i];

        // Even on a 64-bit bus, a window tagged as 32-bit PCI memory must stay
        // below 4 GiB.
        limit = child->ChildLimit;
        if (FDT_PCI_SPACE_FROM_ATTRIBUTES(window->ChildAttributes) == FdtPciSpaceMemory32 &&
            limit > UINT32_MAX) {
            limit = UINT32_MAX;
        }

        if (!__DmaExtentValid(window->ParentBase, window->Length, parentLimit)) {
            __DmaSetFault(fault, FdtDmaFaultParentExtent, i, 0);
            return OS_EINVALPARAMS;
        }

        if (!__DmaExtentValid(window->ChildBase, window->Length, limit)) {
            __DmaSetFault(fault, FdtDmaFaultChildExtent, i, 0);
            return OS_EINVALPARAMS;
        }

        // Reject overlapping child windows so one device address never maps
        // to two different parent addresses.
        for (uint32_t j = 0; j < i; j++) {
            if (__DmaOverlaps(window->ChildBase, window->Length,
                    child->Windows[j].ChildBase, child->Windows[j].Length)) {
                __DmaSetFault(fault, FdtDmaFaultOverlap, i, j);
                return OS_EINVALPARAMS;
            }
        }
    }
    return OS_EOK;
}

// Decodes each <child-address parent-address length> entry from raw property
// bytes and checks bounds and overlaps against parentLimit.
static oserr_t
__DmaDecodeWindows(
    _In_      const FdtDmaProperty_t* property,
    _In_      uint64_t                parentLimit,
    _Out_     FdtDmaBusWindows_t*     result,
    _Out_Opt_ FdtDmaFault_t*          fault)
{
    FdtDmaWindow_t* window;
    const uint8_t*  bytes;
    uint32_t        stride;
    oserr_t         status;

    stride = (property->ChildCells + property->ParentCells + property->SizeCells) * 4;
    if (property->Length % stride) {
        __DmaSetFault(fault, FdtDmaFaultTruncatedWindow, 0, 0);
        return OS_EINVALPARAMS;
    }

    result->Count = property->Length / stride;
    if (result->Count > FDT_DMA_MAX_WINDOWS) {
        __DmaSetFault(fault, FdtDmaFaultTooManyWindows, 0, 0);
        if (fault != NULL) {
            fault->Count = result->Count;
        }
        return OS_ENOTSUPPORTED;
    }

    for (uint32_t i = 0; i < result->Count; i++) {
        bytes = property->Bytes + i * stride;
        window = &result->Windows[i];

        status = __DmaDecodeAddress(
            bytes,
            property->ChildCells,
            property->ChildFormat,
            &window->ChildBase,
            &window->ChildAttributes
        );
        if (status != OS_EOK) {
            __DmaSetFault(fault, FdtDmaFaultChildAddress, i, 0);
            return status;
        }
        bytes += property->ChildCells * 4;

        status = __DmaDecodeAddress(
            bytes,
            property->ParentCells,
            property->ParentFormat,
            &window->ParentBase,
            &window->ParentAttributes
        );
        if (status != OS_EOK) {
            __DmaSetFault(fault, FdtDmaFaultParentAddress, i, 0);
            return status;
        }

        window->Length = FdtReadCells(bytes + property->ParentCells * 4, property->SizeCells);
    }
    return __DmaCheckWindows(result, parentLimit, fault);
}

oserr_t
FdtDmaDecode(
    _In_      const FdtDmaProperty_t* property,
    _Out_     FdtDmaBusWindows_t*     windows,
    _Out_Opt_ FdtDmaFault_t*          fault)
{
    FdtDmaBusWindows_t result = { 0 };
    uint64_t           parentLimit;
    oserr_t            status;

    if (property == NULL || windows == NULL) {
        return OS_EINVALPARAMS;
    }

    status = __DmaAddressLimit(property->ChildFormat, property->ChildCells, &result.ChildLimit);
    if (status != OS_EOK) {
        __DmaSetFault(fault, FdtDmaFaultChildFormat, 0, 0);
        return status;
    }

    status = __DmaAddressLimit(property->ParentFormat, property->ParentCells, &parentLimit);
    if (status != OS_EOK) {
        __DmaSetFault(fault, FdtDmaFaultParentFormat, 0, 0);
        return status;
    }

    if (property->SizeCells == 0 || property->SizeCells > 2) {
        __DmaSetFault(fault, FdtDmaFaultSizeCells, 0, 0);
        return OS_ENOTSUPPORTED;
    }

    // Keep missing, empty (1:1 pass-through), and listed windows distinct so
    // callers know whether a bus has no route at all or allows 1:1 DMA.
    if (property->Bytes == NULL) {
        if (property->Length != 0) {
            __DmaSetFault(fault, FdtDmaFaultMissingBytes, 0, 0);
            return OS_EINVALPARAMS;
        }
        result.State = FdtDmaPropertyAbsent;
    } else if (property->Length == 0) {
        result.State = FdtDmaPropertyIdentity;
        if (parentLimit < result.ChildLimit) {
            result.ChildLimit = parentLimit;
        }
    } else {
        result.State = FdtDmaPropertyWindows;
        status = __DmaDecodeWindows(property, parentLimit, &result, fault);
        if (status != OS_EOK) {
            return status;
        }
    }

    *windows = result;
    return OS_EOK;
}

// Validates that parent ranges are non-empty, fit in 64 bits, and do not
// overlap in the parent bus's device-address space.
static oserr_t
__DmaCheckParent(
    _In_ const FdtDmaMap_t* parent)
{
    const FdtDmaRange_t* range;

    if (parent->Count > FDT_DMA_MAX_RANGES) {
        return OS_ENOTSUPPORTED;
    }

    for (uint32_t i = 0; i < parent->Count; i++) {
        range = &parent->Ranges[i];
        if (!__DmaExtentValid(range->DeviceBase, range->Length, UINT64_MAX) ||
            !__DmaExtentValid(range->PhysicalBase, range->Length, UINT64_MAX)) {
            return OS_EINVALPARAMS;
        }

        for (uint32_t j = 0; j < i; j++) {
            if (__DmaOverlaps(range->DeviceBase, range->Length,
                    parent->Ranges[j].DeviceBase, parent->Ranges[j].Length)) {
                return OS_EINVALPARAMS;
            }
        }
    }
    return OS_EOK;
}

// Inserts range into map sorted by DeviceBase without merging touching entries.
static oserr_t
__DmaAppend(
    _InOut_ FdtDmaMap_t*         map,
    _In_    const FdtDmaRange_t* range)
{
    uint32_t index;

    if (map->Count == FDT_DMA_MAX_RANGES) {
        return OS_ENOTSUPPORTED;
    }

    // Keep entries sorted by device address even when firmware lists windows
    // out of order. Keep touching ranges separate since their physical mapping
    // offset can change right at the boundary.
    index = map->Count++;
    while (index != 0 && map->Ranges[index - 1].DeviceBase > range->DeviceBase) {
        map->Ranges[index] = map->Ranges[index - 1];
        index--;
    }

    map->Ranges[index] = *range;
    return OS_EOK;
}

oserr_t
FdtDmaCompose(
    _In_  const FdtDmaBusWindows_t* child,
    _In_  const FdtDmaMap_t*        parent,
    _Out_ FdtDmaMap_t*              map)
{
    FdtDmaMap_t           result = { 0 };
    FdtDmaRange_t         range;
    const FdtDmaRange_t*  source;
    const FdtDmaWindow_t* window;
    uint64_t              parentEnd;
    uint64_t              start;
    uint64_t              end;
    oserr_t               status;

    if (child == NULL || parent == NULL || map == NULL) {
        return OS_EINVALPARAMS;
    }

    if (child->State == FdtDmaPropertyAbsent) {
        return OS_ENOTSUPPORTED;
    }

    if (child->State != FdtDmaPropertyIdentity && child->State != FdtDmaPropertyWindows) {
        return OS_EINVALPARAMS;
    }

    if ((child->State == FdtDmaPropertyIdentity && child->Count != 0) ||
        (child->State == FdtDmaPropertyWindows && child->Count == 0)) {
        return OS_EINVALPARAMS;
    }

    if (child->Count > FDT_DMA_MAX_WINDOWS) {
        return OS_ENOTSUPPORTED;
    }

    status = __DmaCheckWindows(child, UINT64_MAX, NULL);
    if (status != OS_EOK) {
        return status;
    }

    status = __DmaCheckParent(parent);
    if (status != OS_EOK) {
        return status;
    }

    // Walk every parent-bus RAM interval and match it against the child's
    // pass-through limit or explicit translation windows.
    for (uint32_t i = 0; i < parent->Count; i++) {
        source = &parent->Ranges[i];
        parentEnd = source->DeviceBase + (source->Length - 1);

        if (child->State == FdtDmaPropertyIdentity) {
            if (source->DeviceBase > child->ChildLimit) {
                continue;
            }

            // Clip the parent range to ChildLimit when addresses pass straight through.
            range = *source;
            end = parentEnd < child->ChildLimit ? parentEnd : child->ChildLimit;
            range.Length = end - source->DeviceBase + 1;

            status = __DmaAppend(&result, &range);
            if (status != OS_EOK) {
                return status;
            }
            continue;
        }

        for (uint32_t j = 0; j < child->Count; j++) {
            window = &child->Windows[j];

            // Intersect in the parent bus's address space using inclusive
            // last-byte bounds so a range reaching UINT64_MAX never wraps.
            start = source->DeviceBase > window->ParentBase ? source->DeviceBase : window->ParentBase;
            end = window->ParentBase + (window->Length - 1);
            if (parentEnd < end) {
                end = parentEnd;
            }

            if (start > end) {
                continue;
            }

            // Shift both the child device base and the CPU physical base by how
            // far the overlap starts past each interval's beginning.
            range.DeviceBase = window->ChildBase + (start - window->ParentBase);
            range.PhysicalBase = source->PhysicalBase + (start - source->DeviceBase);
            range.Length = end - start + 1;

            status = __DmaAppend(&result, &range);
            if (status != OS_EOK) {
                return status;
            }
        }
    }

    if (result.Count == 0) {
        return OS_ENOENT;
    }

    *map = result;
    return OS_EOK;
}

// Base check shared by pool scans, device checks, and bus checks.
bool
FdtDmaNodeIsDirect(
    _In_      const FdtDmaNode_t*        node,
    _Out_Opt_ enum FdtDmaIndirectReason* reason)
{
    enum FdtDmaIndirectReason found;

    if (node->Disabled) {
        found = FdtDmaIndirectDisabled;
    } else if (node->HasIommus || node->HasIommuMap) {
        found = FdtDmaIndirectIommu;
    } else if (node->HasDmaParent) {
        found = FdtDmaIndirectDmaParent;
    } else if (node->HasMemoryRegion) {
        found = FdtDmaIndirectMemoryRegion;
    } else if (!node->IsRoot && node->HasDmaRanges && node->Compatible != NULL &&
               !node->IsPci && !node->ParentIsPci &&
               !FdtStringListContains(node->Compatible, node->CompatibleLength, "simple-bus")) {
        // Non-root nodes with dma-ranges that are neither simple-bus nor PCI
        // (such as simple-mfd blocks) serve private sub-drivers and do not
        // form a standard direct DMA bus.
        found = FdtDmaIndirectFirmwareBus;
    } else {
        return true;
    }

    if (reason != NULL) {
        *reason = found;
    }
    return false;
}

// Leaf check for platform devices: direct node check plus rejecting PCI nodes.
bool
FdtDmaDeviceIsDirect(
    _In_      const FdtDmaNode_t*        node,
    _Out_Opt_ enum FdtDmaIndirectReason* reason)
{
    if (!FdtDmaNodeIsDirect(node, reason)) {
        return false;
    }

    if (node->IsPci) {
        if (reason != NULL) {
            *reason = FdtDmaIndirectPci;
        }
        return false;
    }
    return true;
}

// Parent bus check: direct device check plus requiring simple-bus below root.
bool
FdtDmaBusIsDirect(
    _In_      const FdtDmaNode_t*        node,
    _Out_Opt_ enum FdtDmaIndirectReason* reason)
{
    if (!FdtDmaDeviceIsDirect(node, reason)) {
        return false;
    }

    if (!node->IsRoot &&
        !FdtStringListContains(node->Compatible, node->CompatibleLength, "simple-bus")) {
        if (reason != NULL) {
            *reason = FdtDmaIndirectNotSimpleBus;
        }
        return false;
    }
    return true;
}
