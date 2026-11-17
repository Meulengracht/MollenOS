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
 */

#include <arch/acpi.h>
#include <acpiinterface.h>
#include <interrupts/interrupts.h>

oserr_t
ArchAcpiGetPciDma(
    _In_  ACPI_HANDLE               rootHandle,
    _In_  const struct AcpiPciRoot* root,
    _Out_ AcpiPciDma_t*             dmaOut)
{
    AcpiPciDma_t dma = { 0 };

    if (rootHandle == NULL || root == NULL || dmaOut == NULL) {
        return OS_EINVALPARAMS;
    }

    // On x86 we keep the way things already worked: any address is allowed and
    // memory is cache coherent. We do not set up any DMA remapping hardware here,
    // and the stricter Arm checks (_CCA, _DMA, IORT) do not apply.
    dma.AddressLimit = UINT64_MAX;
    dma.CachePolicy = SHMDeviceCacheCoherent;
    dma.Flags = ACPI_PCI_DMA_DIRECT;
    
    *dmaOut = dma;
    return OS_EOK;
}

oserr_t
ArchAcpiResolvePciLink(
    _In_  ACPI_HANDLE                   bus,
    _In_  const ACPI_PCI_ROUTING_TABLE* route,
    _Out_ struct AcpiPciInterrupt*      result)
{
    AcpiDevice_t*            device;
    AcpiInterruptSource_t*   source;
    AcpiInterruptResource_t* interrupt;
    struct AcpiPciInterrupt  resolved;

    // The x86 interrupt link setup has already picked an IRQ for each link. We
    // find it by the link name in this route. Matching on the name keeps two
    // functions in the same slot from picking up each other's link.
    device = AcpiDeviceLookupHandle(bus);
    if (device == NULL || device->Routings == NULL || route->SourceIndex != 0) {
        return OS_ENOTSUPPORTED;
    }

    source = list_find_value(&device->Routings->Sources, (void*)route->Source);
    if (source == NULL || source->CurrentInterrupt == NULL) {
        return OS_ENOTSUPPORTED;
    }

    interrupt = source->CurrentInterrupt;
    resolved.Line = interrupt->Irq;
    resolved.Conform = INTERRUPT_ACPICONFORM_PRESENT;
    
    if (interrupt->Trigger == ACPI_LEVEL_SENSITIVE) {
        resolved.Conform |= INTERRUPT_ACPICONFORM_TRIGGERMODE;
    }
    if (interrupt->Polarity == ACPI_ACTIVE_LOW) {
        resolved.Conform |= INTERRUPT_ACPICONFORM_POLARITY;
    }
    if (interrupt->Shareable) {
        resolved.Conform |= INTERRUPT_ACPICONFORM_SHAREABLE;
    }
    
    *result = resolved;
    return OS_EOK;
}
