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
 * Device Tree DMA helpers for deviced firmware parsing
 */

//#define __TRACE
#include <firmware/dma.h>
#include <firmware/pci.h>
#include <firmware/resources.h>
#include <ddk/utils.h>

// Known PCI hosts sometimes omit device_type = "pci". Fall back to the
// compatible-string check used during PCI host enumeration.
static bool
__DmaNodeIsPci(
    _In_ const struct FdtResources* node)
{
    enum FdtPciHostType type;

    return node != NULL && (node->IsPci || FdtPciHostType(&node->View, &type));
}

void
FdtDescribeDmaNode(
    _In_  const struct FdtResources* node,
    _In_  const struct FdtResources* parent,
    _Out_ FdtDmaNode_t*              description)
{
    uint32_t    compatibleLength;
    uint32_t    length;
    const void* compatible = FdtProperty(&node->View, "compatible", &compatibleLength);

    *description = (FdtDmaNode_t) {
        .Compatible = compatible,
        .CompatibleLength = compatible != NULL ? compatibleLength : 0,
        .IsRoot = parent == NULL,
        .IsPci = __DmaNodeIsPci(node),
        .ParentIsPci = __DmaNodeIsPci(parent),
        .Disabled = node->Disabled || node->ParentDisabled,
        .HasIommus = FdtProperty(&node->View, "iommus", &length) != NULL,
        .HasIommuMap = FdtProperty(&node->View, "iommu-map", &length) != NULL,
        .HasDmaParent = FdtProperty(&node->View, "dma-parent", &length) != NULL,
        .HasDmaRanges = node->DmaRanges != NULL,
        .HasMemoryRegion = FdtProperty(&node->View, "memory-region", &length) != NULL
    };
}

// Logs why FdtDmaDecode rejected the property of one bus. Every message names
// the node, so the failing bus can be found in the device tree.
static void
__LogDecodeFault(
    _In_ const struct FdtResources* bus,
    _In_ const FdtDmaFault_t*       fault,
    _In_ oserr_t                    status)
{
    switch (fault->Kind) {
        case FdtDmaFaultChildFormat:
            WARNING("FdtDecodeDmaRanges: node %u child address format unsupported (%u cells, %u)",
                bus->NodeOffset, bus->AddressCells, status);
            break;
        case FdtDmaFaultParentFormat:
            WARNING("FdtDecodeDmaRanges: node %u parent address format unsupported (%u cells, %u)",
                bus->NodeOffset, bus->ParentAddressCells, status);
            break;
        case FdtDmaFaultSizeCells:
            WARNING("FdtDecodeDmaRanges: node %u size cell count %u unsupported",
                bus->NodeOffset, bus->SizeCells);
            break;
        case FdtDmaFaultMissingBytes:
            ERROR("FdtDecodeDmaRanges: node %u lacks DMA property bytes", bus->NodeOffset);
            break;
        case FdtDmaFaultTruncatedWindow:
            ERROR("FdtDecodeDmaRanges: node %u has a truncated DMA window", bus->NodeOffset);
            break;
        case FdtDmaFaultTooManyWindows:
            WARNING("FdtDecodeDmaRanges: node %u has too many DMA windows (%u)",
                bus->NodeOffset, fault->Count);
            break;
        case FdtDmaFaultChildAddress:
            WARNING("FdtDecodeDmaRanges: node %u window %u child address unsupported (%u)",
                bus->NodeOffset, fault->Window, status);
            break;
        case FdtDmaFaultParentAddress:
            WARNING("FdtDecodeDmaRanges: node %u window %u parent address unsupported (%u)",
                bus->NodeOffset, fault->Window, status);
            break;
        case FdtDmaFaultParentExtent:
            ERROR("FdtDecodeDmaRanges: node %u window %u exceeds parent address space",
                bus->NodeOffset, fault->Window);
            break;
        case FdtDmaFaultChildExtent:
            ERROR("FdtDecodeDmaRanges: node %u window %u has an invalid extent",
                bus->NodeOffset, fault->Window);
            break;
        case FdtDmaFaultOverlap:
            ERROR("FdtDecodeDmaRanges: node %u windows %u and %u overlap",
                bus->NodeOffset, fault->Window, fault->OtherWindow);
            break;
        case FdtDmaFaultNone:
            ERROR("FdtDecodeDmaRanges: node %u DMA ranges rejected (%u)", bus->NodeOffset, status);
            break;
    }
}

oserr_t
FdtDecodeDmaRanges(
    _In_  const struct FdtResources* bus,
    _In_  enum FdtDmaAddressFormat   childFormat,
    _In_  enum FdtDmaAddressFormat   parentFormat,
    _Out_ FdtDmaBusWindows_t*        windows)
{
    FdtDmaProperty_t property;
    FdtDmaFault_t    fault = { FdtDmaFaultNone };
    oserr_t          status;

    TRACE("FdtDecodeDmaRanges(bus=%p, childFormat=%u, parentFormat=%u)",
        bus, childFormat, parentFormat);
    if (bus == NULL || windows == NULL) {
        ERROR("FdtDecodeDmaRanges: missing bus or output");
        return OS_EINVALPARAMS;
    }

    if (bus->Malformed || bus->ParentMalformed) {
        ERROR("FdtDecodeDmaRanges: node %u has malformed parent nodes", bus->NodeOffset);
        return OS_EINVALPARAMS;
    }

    property = (FdtDmaProperty_t) {
        .Bytes = bus->DmaRanges,
        .Length = bus->DmaRangesLength,
        .ChildCells = bus->AddressCells,
        .ParentCells = bus->ParentAddressCells,
        .SizeCells = bus->SizeCells,
        .ChildFormat = childFormat,
        .ParentFormat = parentFormat
    };

    status = FdtDmaDecode(&property, windows, &fault);
    if (status != OS_EOK) {
        __LogDecodeFault(bus, &fault, status);
        return status;
    }

    TRACE("FdtDecodeDmaRanges: node %u state %u, %u windows",
        bus->NodeOffset, windows->State, windows->Count);
    return OS_EOK;
}

oserr_t
FdtPciDmaMap(
    _In_  const struct FdtPciHost* host,
    _Out_ FdtDmaMap_t*        parent)
{
    oserr_t     status;
    FdtDmaMap_t all = { 0 };
    FdtDmaMap_t ram = { 0 };

    FdtDmaBusWindows_t identity = {
        .State = FdtDmaPropertyIdentity,
        .ChildLimit = UINT64_MAX
    };

    TRACE("FdtPciDmaMap(host=%p)", host);
    // The DMA map can be requested before hardware setup.
    if (host == NULL || parent == NULL) {
        ERROR("FdtPciDmaMap: missing host or output");
        return OS_EINVALPARAMS;
    }
    
    if (host->DmaWindowCount > sizeof(host->DmaWindows) / sizeof(host->DmaWindows[0])) {
        WARNING("FdtPciDmaMap: domain %u window count %u exceeds capacity",
            host->Segment, host->DmaWindowCount);
        return OS_ENOTSUPPORTED;
    }
    
    for (uint32_t i = 0; i < host->DmaWindowCount; i++) {
        const struct FdtPciWindow* window = &host->DmaWindows[i];
        if (window->Space != FdtPciSpaceMemory32 && window->Space != FdtPciSpaceMemory64) {
            WARNING("FdtPciDmaMap: domain %u window %u is not memory", host->Segment, i);
            return OS_ENOTSUPPORTED;
        }
        
        if (FDT_PCI_SPACE_FROM_ATTRIBUTES(window->Attributes) != window->Space ||
            (window->Attributes & ~FDT_DMA_PCI_VALID_FLAGS)) {
            WARNING("FdtPciDmaMap: domain %u window %u attributes 0x%x unsupported",
                host->Segment, i, window->Attributes);
            return OS_ENOTSUPPORTED;
        }
        
        all.Ranges[all.Count++] = (FdtDmaRange_t) {
            .DeviceBase = window->BusBase,
            .PhysicalBase = window->PhysicalBase,
            .Length = window->Length
        };
    }
    
    // Run an identity pass over every PCI window first to catch invalid extents
    // or overlaps. Even if a peer or MSI window gets filtered out later, an
    // overlap with RAM leaves the device address ambiguous in hardware.
    status = FdtDmaCompose(&identity, &all, &all);
    if (status != OS_EOK) {
        WARNING("FdtPciDmaMap: domain %u DMA windows rejected (%u)", host->Segment, status);
        return status;
    }
    
    for (uint32_t i = 0; i < host->DmaWindowCount; i++) {
        const struct FdtPciWindow* window = &host->DmaWindows[i];
        if (window->Kind != FdtDmaWindowRam) {
            continue;
        }

        ram.Ranges[ram.Count++] = (FdtDmaRange_t) {
            .DeviceBase = window->BusBase,
            .PhysicalBase = window->PhysicalBase,
            .Length = window->Length
        };
    }
    if (ram.Count == 0) {
        WARNING("FdtPciDmaMap: domain %u has no RAM windows", host->Segment);
        return OS_ENOENT;
    }
    
    // Run a second identity pass on the RAM-only list so the output is sorted
    // by bus address and written to parent only when everything succeeds.
    status = FdtDmaCompose(&identity, &ram, parent);
    if (status != OS_EOK) {
        WARNING("FdtPciDmaMap: domain %u RAM map rejected (%u)", host->Segment, status);
        return status;
    }
    
    TRACE("FdtPciDmaMap: domain %u has %u RAM ranges", host->Segment, parent->Count);
    return OS_EOK;
}
