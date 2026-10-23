/**
 * MollenOS
 *
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
 */

#include "mip.h"

static oserr_t
__ReadCell(
    _In_  const DeviceTreeNode_t* node,
    _In_  const char*             name,
    _In_  uint32_t                defaultValue,
    _Out_ uint32_t*               valueOut)
{
    const DeviceTreeProperty_t* property;

    // Optional properties use the binding's default only when absent. A present
    // but incorrectly sized value is broken firmware, not a request for a default.
    property = DeviceTreeGetProperty(node, name);
    if (!property) {
        *valueOut = defaultValue;
        return OS_EOK;
    }
    
    if (property->Length != sizeof(uint32_t)) {
        return OS_EINVALPARAMS;
    }

    *valueOut = FdtReadBe32(property->Value);
    return OS_EOK;
}

static uint64_t
__ReadAddress(
    _In_ const unsigned char* bytes,
    _In_ uint32_t             cells)
{
    uint64_t value = 0;
    uint32_t index;

    // Firmware puts the most significant cell first. Callers limit the number
    // of cells to two so the complete value fits in the 64-bit result.
    for (index = 0; index < cells; index++) {
        value = (value << RPI_MSI_DT_CELL_BITS) |
            FdtReadBe32(bytes + index * RPI_MSI_DT_CELL_BYTES);
    }
    return value;
}

static oserr_t
__ReadProvider(
    _In_  const DeviceTree_t*     tree,
    _In_  const DeviceTreeNode_t* host,
    _Out_ struct Arm64MipRoute*   route)
{
    const DeviceTreeProperty_t* property;
    const DeviceTreeNode_t*     provider;
    uint32_t                    cells;
    oserr_t                     status;

    // Follow the host's explicit message receiver rather than guessing from
    // nearby nodes. This binding supplies a handle with no extra arguments.
    property = DeviceTreeGetProperty(host, "msi-parent");
    if (!property) {
        return OS_ENOTSUPPORTED;
    }
    
    if (property->Length != RPI_MSI_DT_PARENT_BYTES) {
        return OS_EINVALPARAMS;
    }
    
    provider = DeviceTreeFindPhandle(tree, FdtReadBe32(property->Value));
    if (!provider) {
        return OS_EINVALPARAMS;
    }
    
    if (!DeviceTreeIsEnabled(provider) || !DeviceTreeIsCompatible(provider, "brcm,bcm2712-mip")) {
        return OS_ENOTSUPPORTED;
    }
    
    // A compatible name alone is not enough: the empty marker declares that
    // this node provides message interrupts. Nonzero #msi-cells would require
    // a different parent format, which this implementation does not decode.
    property = DeviceTreeGetProperty(provider, "msi-controller");
    if (!property || property->Length != 0) {
        return OS_EINVALPARAMS;
    }
    
    status = __ReadCell(provider, "#msi-cells", 0, &cells);
    if (status != OS_EOK) {
        return status;
    }
    
    if (cells != 0) {
        return OS_ENOTSUPPORTED;
    }

    route->Host = host;
    route->Provider = provider;
    return OS_EOK;
}

static oserr_t
__ReadInterruptRange(
    _In_    const DeviceTree_t*     tree,
    _In_    const DeviceTreeNode_t* gic,
    _In_    uint32_t                interruptCount,
    _InOut_ struct Arm64MipRoute*   route)
{
    const DeviceTreeProperty_t* property;
    DeviceTreeReference_t       reference;
    uint32_t                    first;
    oserr_t                     status;

    // Accept exactly one range. The final cell is a count, not part of the
    // GIC interrupt reference decoded by DeviceTreeReadReference.
    property = DeviceTreeGetProperty(route->Provider, "msi-ranges");
    if (!property || property->Length != MIP_DT_RANGE_CELLS * RPI_MSI_DT_CELL_BYTES) {
        return OS_EINVALPARAMS;
    }
    
    status = DeviceTreeReadReference(tree, route->Provider,
        "msi-ranges", "#interrupt-cells", 0, &reference);
    if (status != OS_EOK) {
        return status;
    }
    
    if (reference.Provider != gic || reference.CellCount != RPI_MSI_DT_GIC_CELLS) {
        return OS_ENOTSUPPORTED;
    }
    
    // Each message becomes a pulse on a shared GIC input. Level-triggered or
    // CPU-private interrupts would need different handling and cannot be used.
    if (reference.Cells[0] != RPI_MSI_DT_GIC_SHARED ||
        reference.Cells[2] != RPI_MSI_DT_IRQ_EDGE_RISING) {
        return OS_EINVALPARAMS;
    }
    
    route->Count = FdtReadBe32((const unsigned char*)property->Value + MIP_DT_COUNT_OFFSET);
    status = __ReadCell(route->Provider, "brcm,msi-offset", 0, &route->MessageOffset);
    if (status != OS_EOK) {
        return status;
    }
    
    // A host may use only a slice of the MIP's message numbers. Check the
    // starting offset before subtracting, so invalid firmware cannot wrap.
    if (route->Count == 0 || route->MessageOffset >= ARM64_MIP_VECTOR_COUNT) {
        return OS_EINVALPARAMS;
    }
    
    if (route->Count > ARM64_MIP_VECTOR_COUNT - route->MessageOffset) {
        return OS_EINVALPARAMS;
    }
    
    if (reference.Cells[1] >= RPI_MSI_DT_GIC_SPI_COUNT ||
        interruptCount > RPI_MSI_DT_GIC_ID_LIMIT) {
        return OS_EINVALPARAMS;
    }
    
    // The tree counts shared interrupts from zero; the GIC has 32 CPU-local
    // IDs before them. Apply the host's message offset to that hardware ID too.
    first = RPI_MSI_DT_GIC_SPI_BASE + reference.Cells[1] + route->MessageOffset;
    if (first >= interruptCount || route->Count > interruptCount - first) {
        return OS_EINVALPARAMS;
    }
    route->FirstInterrupt = first;
    return OS_EOK;
}

static oserr_t
__ReadRegisters(
    _InOut_ struct Arm64MipRoute* route)
{
    uint64_t length;
    uint64_t unused;
    oserr_t  status;

    // reg[0] is accessed by the CPU. Translate it through the parent buses so
    // the eventual device mapping uses a CPU physical address.
    status = DeviceTreeReadRegister(route->Provider, 0, &route->RegisterBase, &length);
    if (status != OS_EOK) {
        return status;
    }
    
    if (length < ARM64_MIP_REGISTER_LENGTH ||
        (route->RegisterBase & RPI_MSI_MESSAGE_ALIGNMENT_MASK)) {
        return OS_EINVALPARAMS;
    }
    
    // reg[1] is the address a PCI device writes to raise an interrupt. Keep it
    // in PCI bus address space: translating it like reg[0] would give drivers
    // the wrong destination. It must hold one complete aligned message write.
    status = DeviceTreeReadRegisterRaw(route->Provider, 1, &route->MessageAddress, &length);
    if (status != OS_EOK) {
        return status;
    }
    
    if (length < RPI_MSI_MESSAGE_BYTES ||
        (route->MessageAddress & RPI_MSI_MESSAGE_ALIGNMENT_MASK)) {
        return OS_EINVALPARAMS;
    }

    // Only the two defined register entries are supported. In particular,
    // an error reading a third entry must not be mistaken for its absence.
    status = DeviceTreeReadRegisterRaw(route->Provider, 2, &unused, &length);
    return status == OS_ENOENT ? OS_EOK : OS_EINVALPARAMS;
}

static oserr_t
__ValidateDoorbellWindow(
    _In_ const struct Arm64MipRoute* route)
{
    const DeviceTreeProperty_t* ranges;
    const unsigned char*        tuple;
    uint32_t                    childCells;
    uint32_t                    parentCells;
    uint32_t                    sizeCells;
    uint32_t                    stride;
    uint32_t                    offset;
    uint32_t                    space;
    uint64_t                    registers;
    uint64_t                    unused;
    uint64_t                    bus;
    uint64_t                    physical;
    uint64_t                    length;
    uint64_t                    displacement;
    unsigned int                matches = 0;
    oserr_t                     status;

    // A "doorbell" is the address a device writes to request an interrupt.
    // Prove that the host forwards that write to this MIP, not to RAM or to
    // another device. BCM2712 puts the host and MIP under the same bus, so
    // their untranslated parent addresses can be compared directly.
    if (!route->Host->Parent || route->Host->Parent != route->Provider->Parent) {
        return OS_ENOTSUPPORTED;
    }
    status = DeviceTreeReadRegisterRaw(route->Provider, 0, &registers, &unused);
    if (status != OS_EOK) {
        return status;
    }

    // PCI uses three address cells: space/flags, then a 64-bit address.
    // Parent addresses and lengths may use one or two cells. Reject wider
    // encodings before reading them into our 64-bit variables.
    status = __ReadCell(route->Host, "#address-cells", RPI_MSI_DT_PCI_ADDRESS_CELLS, &childCells);
    if (status != OS_EOK) {
        return status;
    }
    
    status = __ReadCell(route->Host->Parent, "#address-cells", RPI_MSI_DT_U64_CELLS, &parentCells);
    if (status != OS_EOK) {
        return status;
    }
    
    status = __ReadCell(route->Host, "#size-cells", RPI_MSI_DT_U64_CELLS, &sizeCells);
    if (status != OS_EOK) {
        return status;
    }
    
    if (childCells != RPI_MSI_DT_PCI_ADDRESS_CELLS ||
        parentCells == 0 || parentCells > RPI_MSI_DT_U64_CELLS) {
        return OS_ENOTSUPPORTED;
    }
    
    if (sizeCells == 0 || sizeCells > RPI_MSI_DT_U64_CELLS) {
        return OS_ENOTSUPPORTED;
    }

    // Each dma-ranges entry describes a device-visible start address, its
    // parent-bus destination, and a byte length. Reject incomplete entries.
    ranges = DeviceTreeGetProperty(route->Host, "dma-ranges");
    stride = (childCells + parentCells + sizeCells) * RPI_MSI_DT_CELL_BYTES;
    if (!ranges || ranges->Length == 0 || ranges->Length % stride) {
        return OS_EINVALPARAMS;
    }

    for (offset = 0; offset < ranges->Length; offset += stride) {
        tuple = (const unsigned char*)ranges->Value + offset;
        space = FdtReadBe32(tuple) & RPI_MSI_DT_PCI_SPACE_MASK;
        bus = __ReadAddress(tuple + RPI_MSI_DT_CELL_BYTES, RPI_MSI_DT_U64_CELLS);
        physical = __ReadAddress(tuple + childCells * RPI_MSI_DT_CELL_BYTES, parentCells);
        length = __ReadAddress(tuple + (childCells + parentCells) * RPI_MSI_DT_CELL_BYTES, sizeCells);

        // Check both ends for overflow before testing whether the message
        // falls inside. Subtracting the start avoids overflowing start + size.
        if (length == 0 || length - 1 > UINT64_MAX - bus || length - 1 > UINT64_MAX - physical) {
            return OS_EINVALPARAMS;
        }
        if (route->MessageAddress < bus || route->MessageAddress - bus >= length) {
            continue;
        }
        
        displacement = route->MessageAddress - bus;
        if ((space != RPI_MSI_DT_PCI_MEMORY32 && space != RPI_MSI_DT_PCI_MEMORY64) ||
            length - displacement < RPI_MSI_MESSAGE_BYTES) {
            return OS_EINVALPARAMS;
        }

        // The whole write must reach the beginning of this MIP's register
        // block. Merely finding the PCI address in some window is not enough.
        if (physical + displacement != registers) {
            return OS_EINVALPARAMS;
        }
        matches++;
    }

    // Multiple matching windows make the destination ambiguous.
    return matches == 1 ? OS_EOK : OS_EINVALPARAMS;
}

oserr_t
Arm64MipReadRoute(
    _In_  const DeviceTree_t*     tree,
    _In_  const DeviceTreeNode_t* host,
    _In_  const DeviceTreeNode_t* gic,
    _In_  uint32_t                interruptCount,
    _Out_ struct Arm64MipRoute*   routeOut)
{
    struct Arm64MipRoute route = { 0 };
    oserr_t              status;

    // The boot scan visits every node. Unrelated or disabled hosts are not
    // errors; malformed routing on a supported host is an error.
    if (!DeviceTreeIsEnabled(host) || !DeviceTreeIsCompatible(host, "brcm,bcm2712-pcie")) {
        return OS_ENOTSUPPORTED;
    }
    
    status = __ReadProvider(tree, host, &route);
    if (status != OS_EOK) {
        return status;
    }
    
    status = __ReadInterruptRange(tree, gic, interruptCount, &route);
    if (status != OS_EOK) {
        return status;
    }
    
    status = __ReadRegisters(&route);
    if (status != OS_EOK) {
        return status;
    }
    
    status = __ValidateDoorbellWindow(&route);
    if (status != OS_EOK) {
        return status;
    }
    
    // Publish only a fully checked route, leaving the caller's output intact
    // if any earlier firmware check failed.
    *routeOut = route;
    return OS_EOK;
}
