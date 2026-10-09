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

#include <firmware/dma.h>
#include <firmware/pci.h>
#include <firmware/resources.h>

static int
__DmaExtentValid(
    _In_ uint64_t base,
    _In_ uint64_t length,
    _In_ uint64_t limit)
{
    return length != 0 && base <= limit && length - 1 <= limit - base;
}

static int
__DmaOverlaps(
    _In_ uint64_t first,
    _In_ uint64_t firstLength,
    _In_ uint64_t second,
    _In_ uint64_t secondLength)
{
    return first <= second ? second - first < firstLength : first - second < secondLength;
}

static oserr_t
__DmaAddressLimit(
    _In_  enum FdtDmaAddressFormat format,
    _In_  uint32_t                  cells,
    _Out_ uint64_t*                 limit)
{
    if (format == FdtDmaAddressSimple && (cells == 1 || cells == 2)) {
        *limit = cells == 1 ? UINT32_MAX : UINT64_MAX;
        return OS_EOK;
    }
    
    if (format == FdtDmaAddressPci && cells == 3) {
        *limit = UINT64_MAX;
        return OS_EOK;
    }
    return OS_ENOTSUPPORTED;
}

static oserr_t
__DmaDecodeAddress(
    _In_  const uint8_t*           bytes,
    _In_  uint32_t                 cells,
    _In_  enum FdtDmaAddressFormat format,
    _Out_ uint64_t*                address,
    _Out_ uint32_t*                attributes)
{
    enum FdtPciSpace space;

    *attributes = 0;
    if (format == FdtDmaAddressPci) {
        *attributes = FdtReadBe32(bytes);
        space = FDT_PCI_SPACE_FROM_ATTRIBUTES(*attributes);
        
        // The first PCI value contains flags as well as the address type. Accept
        // memory addresses, including the supported prefetch and relocation
        // flags, but reject other PCI address types. Those types need different
        // rules and cannot be used to describe the physical-memory paths here.
        if ((space != FdtPciSpaceMemory32 && space != FdtPciSpaceMemory64) ||
            (*attributes & ~0xc3000000U)) {
            return OS_ENOTSUPPORTED;
        }

        bytes += 4;
        cells--;
    }

    *address = FdtReadCells(bytes, cells);
    return OS_EOK;
}

static oserr_t
__DmaCheckChild(
    _In_ const struct FdtDmaRanges* child)
{
    const struct FdtDmaWindow* window;
    uint32_t i;
    uint32_t j;

    if (child->Count > FDT_DMA_MAX_WINDOWS) {
        return OS_ENOTSUPPORTED;
    }

    for (i = 0; i < child->Count; i++) {
        window = &child->Windows[i];
        if (!__DmaExtentValid(window->ChildBase, window->Length, child->ChildLimit) ||
            !__DmaExtentValid(window->ParentBase, window->Length, UINT64_MAX)) {
            return OS_EINVALPARAMS;
        }
        
        for (j = 0; j < i; j++) {
            if (__DmaOverlaps(window->ChildBase, window->Length,
                    child->Windows[j].ChildBase, child->Windows[j].Length)) {
                return OS_EINVALPARAMS;
            }
        }
    }
    return OS_EOK;
}

oserr_t
FdtDecodeDmaRanges(
    _In_  const struct FdtResources* bus,
    _In_  enum FdtDmaAddressFormat   childFormat,
    _In_  enum FdtDmaAddressFormat   parentFormat,
    _Out_ struct FdtDmaRanges*       ranges)
{
    struct FdtDmaRanges  result = { 0 };
    struct FdtDmaWindow* window;
    const uint8_t*       bytes;
    uint64_t             parentLimit;
    uint32_t             stride;
    uint32_t             i;
    oserr_t              status;

    if (bus == NULL || ranges == NULL) {
        return OS_EINVALPARAMS;
    }
    if (bus->Malformed || bus->AncestorMalformed) {
        return OS_EINVALPARAMS;
    }
    
    status = __DmaAddressLimit(childFormat, bus->AddressCells, &result.ChildLimit);
    if (status != OS_EOK) {
        return status;
    }
    
    status = __DmaAddressLimit(parentFormat, bus->ParentAddressCells, &parentLimit);
    if (status != OS_EOK) {
        return status;
    }
    if (bus->SizeCells == 0 || bus->SizeCells > 2) {
        return OS_ENOTSUPPORTED;
    }

    if (bus->DmaRanges == NULL) {
        if (bus->DmaRangesLength != 0) {
            return OS_EINVALPARAMS;
        }
        result.State = FdtDmaPropertyAbsent;
    } else if (bus->DmaRangesLength == 0) {
        result.State = FdtDmaPropertyIdentity;
        if (parentLimit < result.ChildLimit) {
            result.ChildLimit = parentLimit;
        }
    } else {
        result.State = FdtDmaPropertyWindows;
        stride = (bus->AddressCells + bus->ParentAddressCells + bus->SizeCells) * 4;
        if (bus->DmaRangesLength % stride) {
            return OS_EINVALPARAMS;
        }
        result.Count = bus->DmaRangesLength / stride;
        if (result.Count > FDT_DMA_MAX_WINDOWS) {
            return OS_ENOTSUPPORTED;
        }

        for (i = 0; i < result.Count; i++) {
            bytes = bus->DmaRanges + i * stride;
            window = &result.Windows[i];
            
            status = __DmaDecodeAddress(
                bytes,
                bus->AddressCells,
                childFormat,
                &window->ChildBase,
                &window->ChildAttributes
            );
            if (status != OS_EOK) {
                return status;
            }
            bytes += bus->AddressCells * 4;
            
            status = __DmaDecodeAddress(
                bytes,
                bus->ParentAddressCells,
                parentFormat,
                &window->ParentBase,
                &window->ParentAttributes
            );
            if (status != OS_EOK) {
                return status;
            }
            
            window->Length = FdtReadCells(bytes + bus->ParentAddressCells * 4, bus->SizeCells);
            if (!__DmaExtentValid(window->ParentBase, window->Length, parentLimit)) {
                return OS_EINVALPARAMS;
            }
        }

        status = __DmaCheckChild(&result);
        if (status != OS_EOK) {
            return status;
        }
    }

    *ranges = result;
    return OS_EOK;
}

static oserr_t
__DmaCheckParent(
    _In_ const struct FdtDmaMap* parent)
{
    if (parent->Count > FDT_DMA_MAX_RANGES) {
        return OS_ENOTSUPPORTED;
    }

    for (uint32_t i = 0; i < parent->Count; i++) {
        const struct FdtDmaRange* range = &parent->Ranges[i];
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

static oserr_t
__DmaAppend(
    _InOut_ struct FdtDmaMap*        map,
    _In_    const struct FdtDmaRange* range)
{
    uint32_t index;

    if (map->Count == FDT_DMA_MAX_RANGES) {
        return OS_ENOTSUPPORTED;
    }
    
    // Keep the output in device-address order even if firmware lists its
    // windows in a different order. Keep touching ranges separate because the
    // address mapping may change at their boundary.
    index = map->Count++;
    while (index != 0 && map->Ranges[index - 1].DeviceBase > range->DeviceBase) {
        map->Ranges[index] = map->Ranges[index - 1];
        index--;
    }
    
    map->Ranges[index] = *range;
    return OS_EOK;
}

oserr_t
FdtComposeDmaRanges(
    _In_  const struct FdtDmaRanges* child,
    _In_  const struct FdtDmaMap*    parent,
    _Out_ struct FdtDmaMap*          map)
{
    struct FdtDmaMap   result = { 0 };
    struct FdtDmaRange range;
    oserr_t            status;

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
    
    status = __DmaCheckChild(child);
    if (status != OS_EOK) {
        return status;
    }
    
    status = __DmaCheckParent(parent);
    if (status != OS_EOK) {
        return status;
    }
    
    for (uint32_t i = 0; i < parent->Count; i++) {
        const struct FdtDmaRange* source = &parent->Ranges[i];
        uint64_t                  parentEnd = source->DeviceBase + (source->Length - 1);
        uint64_t                  end;
        
        if (child->State == FdtDmaPropertyIdentity) {
            if (source->DeviceBase > child->ChildLimit) {
                continue;
            }
            
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
            const struct FdtDmaWindow* window = &child->Windows[j];
            uint64_t                   start;
            
            start = source->DeviceBase > window->ParentBase ? source->DeviceBase : window->ParentBase;
            end = window->ParentBase + (window->Length - 1);
            if (parentEnd < end) {
                end = parentEnd;
            }
            
            if (start > end) {
                continue;
            }
            
            // Work with the address of the last byte so a range can end at
            // UINT64_MAX without adding one and wrapping back to zero. The
            // input checks ensure these calculated addresses stay in range.
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

oserr_t
FdtPciDmaMap(
    _In_  const struct FdtPciHost* host,
    _Out_ struct FdtDmaMap*        parent)
{
    const struct FdtPciWindow* window;
    struct FdtDmaMap           all = { 0 };
    struct FdtDmaMap           ram = { 0 };
    struct FdtDmaRanges        identity = {
        .State = FdtDmaPropertyIdentity,
        .ChildLimit = UINT64_MAX
    };
    oserr_t status;

    // This is also used before hardware setup; reject an incomplete request.
    if (host == NULL || parent == NULL) {
        return OS_EINVALPARAMS;
    }
    
    if (host->DmaWindowCount > sizeof(host->DmaWindows) / sizeof(host->DmaWindows[0])) {
        return OS_ENOTSUPPORTED;
    }
    
    for (uint32_t i = 0; i < host->DmaWindowCount; i++) {
        window = &host->DmaWindows[i];
        if (window->Space != FdtPciSpaceMemory32 && window->Space != FdtPciSpaceMemory64) {
            return OS_ENOTSUPPORTED;
        }
        
        if (FDT_PCI_SPACE_FROM_ATTRIBUTES(window->Attributes) != window->Space ||
            (window->Attributes & ~0xc3000000U)) {
            return OS_ENOTSUPPORTED;
        }
        
        all.Ranges[all.Count++] = (struct FdtDmaRange) {
            .DeviceBase = window->BusBase,
            .PhysicalBase = window->PhysicalBase,
            .Length = window->Length
        };
    }
    
    // Validate the entire PCI address space before filtering. A peer or MSI
    // window overlapping RAM is ambiguous even if it would later be omitted.
    status = FdtComposeDmaRanges(&identity, &all, &all);
    if (status != OS_EOK) {
        return status;
    }
    
    for (uint32_t i = 0; i < host->DmaWindowCount; i++) {
        window = &host->DmaWindows[i];
        if (window->Kind != FdtDmaWindowRam) {
            continue;
        }
        ram.Ranges[ram.Count++] = (struct FdtDmaRange) {
            .DeviceBase = window->BusBase,
            .PhysicalBase = window->PhysicalBase,
            .Length = window->Length
        };
    }
    if (ram.Count == 0) {
        return OS_ENOENT;
    }
    
    // Reuse the composer to sort copied ranges and publish only a complete map.
    return FdtComposeDmaRanges(&identity, &ram, parent);
}
