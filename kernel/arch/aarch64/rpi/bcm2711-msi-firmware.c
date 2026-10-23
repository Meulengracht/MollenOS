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

#include "bcm2711-msi.h"
#include <string.h>

static oserr_t
__Bcm2711MsiInterruptIndex(
    _In_  const DeviceTreeNode_t* host,
    _Out_ unsigned int*           indexOut)
{
    const DeviceTreeProperty_t* names;
    const char*                 name;
    const char*                 terminator;
    unsigned int                offset = 0;
    unsigned int                index = 0;
    unsigned int                found = 0;

    names = DeviceTreeGetProperty(host, "interrupt-names");
    if (!names) {
        return OS_EINVALPARAMS;
    }

    // interrupt-names and interrupts use matching positions. Locate "msi"
    // by name rather than assuming it follows the separate legacy PCI interrupt.
    // Bound each string search by the property length: firmware is not trusted
    // to supply a terminating zero byte.
    while (offset < names->Length) {
        name = (const char*)names->Value + offset;
        terminator = memchr(name, 0, names->Length - offset);
        if (!terminator) {
            return OS_EINVALPARAMS;
        }
        
        if (!strcmp(name, "msi")) {
            *indexOut = index;
            found++;
        }
        
        offset += (unsigned int)(terminator - name) + 1;
        index++;
    }
    // Duplicate names are ambiguous; choosing the last one could bind us to
    // a different interrupt than the firmware author intended.
    return found == 1 ? OS_EOK : OS_EINVALPARAMS;
}

oserr_t
Arm64Bcm2711MsiReadRoute(
    _In_  const DeviceTree_t*          tree,
    _In_  const DeviceTreeNode_t*      host,
    _In_  const DeviceTreeNode_t*      gic,
    _In_  uint32_t                     interruptCount,
    _Out_ struct Arm64Bcm2711MsiRoute* routeOut)
{
    struct Arm64Bcm2711MsiRoute route = { .Host = host };
    const DeviceTreeProperty_t* property;
    DeviceTreeReference_t       interrupt;
    unsigned int                index;
    uint64_t                    length;
    oserr_t                     status;

    // The boot scan visits every node, including Pi 5 hosts that use a separate
    // receiver. Only enabled Pi 4 hosts with an internal receiver belong here.
    if (!DeviceTreeIsEnabled(host) || !DeviceTreeIsCompatible(host, "brcm,bcm2711-pcie")) {
        return OS_ENOTSUPPORTED;
    }
    
    property = DeviceTreeGetProperty(host, "msi-parent");
    if (!property) {
        return OS_ENOTSUPPORTED;
    }
    
    if (property->Length != RPI_MSI_DT_PARENT_BYTES) {
        return OS_EINVALPARAMS;
    }
    
    // Pi 4's host is its own message provider. An external provider needs a
    // different backend, so do not silently treat it as this internal receiver.
    if (DeviceTreeFindPhandle(tree, FdtReadBe32(property->Value)) != host) {
        return OS_ENOTSUPPORTED;
    }
    
    // The marker must be empty, and this binding accepts no parent arguments.
    // Missing #msi-cells means zero; a present property still needs a full cell.
    property = DeviceTreeGetProperty(host, "msi-controller");
    if (!property || property->Length != 0) {
        return OS_EINVALPARAMS;
    }
    
    property = DeviceTreeGetProperty(host, "#msi-cells");
    if (property) {
        if (property->Length != RPI_MSI_DT_CELL_BYTES) {
            return OS_EINVALPARAMS;
        }
        
        if (FdtReadBe32(property->Value) != 0) {
            return OS_ENOTSUPPORTED;
        }
    }
    
    // Resolve the named interrupt through the common tree reader, which also
    // handles interrupts-extended and inherited interrupt-parent properties.
    status = __Bcm2711MsiInterruptIndex(host, &index);
    if (status != OS_EOK) {
        return status;
    }
    
    status = DeviceTreeReadInterrupt(tree, host, index, &interrupt);
    if (status != OS_EOK) {
        return status;
    }
    
    if (interrupt.Provider != gic || interrupt.CellCount != RPI_MSI_DT_GIC_CELLS) {
        return OS_ENOTSUPPORTED;
    }
    
    // All message bits feed one shared GIC input. It stays high until pending
    // unmasked messages are cleared, so edge-triggered setup could lose work.
    // This is not the host's separate interrupt for legacy PCI devices (INTx).
    if (interrupt.Cells[0] != RPI_MSI_DT_GIC_SHARED ||
        interrupt.Cells[2] != RPI_MSI_DT_IRQ_LEVEL_HIGH) {
        return OS_EINVALPARAMS;
    }
    
    if (interrupt.Cells[1] >= RPI_MSI_DT_GIC_SPI_COUNT ||
        interruptCount > RPI_MSI_DT_GIC_ID_LIMIT) {
        return OS_EINVALPARAMS;
    }
    
    // Firmware numbers shared interrupts from zero. Skip the GIC's CPU-local
    // IDs, then check against the interrupt count the actual GIC reported.
    route.ParentInterrupt = RPI_MSI_DT_GIC_SPI_BASE + interrupt.Cells[1];
    if (route.ParentInterrupt >= interruptCount) {
        return OS_EINVALPARAMS;
    }
    
    // These are CPU accesses, unlike the PCI message address selected later.
    // Translate through parent buses and require the entire register block.
    status = DeviceTreeReadRegister(host, 0, &route.RegisterBase, &length);
    if (status != OS_EOK) {
        return status;
    }

    if (length < BCM2711_MSI_REGISTER_LENGTH ||
        (route.RegisterBase & RPI_MSI_MESSAGE_ALIGNMENT_MASK)) {
        return OS_EINVALPARAMS;
    }
    
    // Do not expose a partially checked route on any failure path.
    *routeOut = route;
    return OS_EOK;
}
