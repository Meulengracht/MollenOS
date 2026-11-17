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
#include <debug.h>
#include <interrupts/interrupts.h>

// GIC (Arm interrupt controller) interrupt ids below 32 belong to a single CPU,
// so they cannot be used by PCI devices. Ids from 1020 up are reserved. We do not
// check the upper limit of the real hardware here, the controller does that when
// the interrupt is allocated.
#define ACPI_PCI_SPI_MIN 32U
#define ACPI_PCI_SPI_MAX 1019U

// Holds the interrupt found while reading a link's _CRS (its current resources).
// The result is only used once the whole list has been checked. We accept a
// list with exactly one interrupt entry followed by the end marker.
struct __CurrentPciLink {
    struct AcpiPciInterrupt Interrupt;
    bool                    Found;
    bool                    Ended;
};

static ACPI_STATUS
__ReadCurrentPciLinkResource(
    _In_    ACPI_RESOURCE*          resource,
    _InOut_ void*                   context)
{
    struct __CurrentPciLink*     link = context;
    ACPI_RESOURCE_EXTENDED_IRQ* irq;

    if (resource->Type == ACPI_RESOURCE_TYPE_END_TAG) {
        link->Ended = true;
        return AE_OK;
    }

    if (link->Found || resource->Type != ACPI_RESOURCE_TYPE_EXTENDED_IRQ) {
        return AE_SUPPORT;
    }

    if (resource->Length < ACPI_RS_SIZE(ACPI_RESOURCE_EXTENDED_IRQ)) {
        return AE_AML_BAD_RESOURCE_LENGTH;
    }

    irq = &resource->Data.ExtendedIrq;
    if (irq->InterruptCount != 1 || irq->ProducerConsumer != ACPI_CONSUMER ||
        irq->Triggering != ACPI_LEVEL_SENSITIVE || irq->Polarity != ACPI_ACTIVE_HIGH ||
        irq->Shareable > ACPI_SHARED || irq->ResourceSource.StringLength != 0) {
        return AE_SUPPORT;
    }
    if (irq->Interrupts[0] < ACPI_PCI_SPI_MIN || irq->Interrupts[0] > ACPI_PCI_SPI_MAX) {
        return AE_SUPPORT;
    }

    // The _CRS gives us the interrupt number (GSI) to use as it is. We keep its
    // shared flag. Interrupts in this range (SPIs) are always shareable between
    // devices on the Arm side, even when the resource says exclusive.
    link->Interrupt.Line = (int32_t)irq->Interrupts[0];
    link->Interrupt.Conform = INTERRUPT_ACPICONFORM_PRESENT | INTERRUPT_ACPICONFORM_FIXED |
        INTERRUPT_ACPICONFORM_TRIGGERMODE;
    if (irq->Shareable == ACPI_SHARED) {
        link->Interrupt.Conform |= INTERRUPT_ACPICONFORM_SHAREABLE;
    }
    link->Found = true;
    return AE_OK;
}

oserr_t
ArchAcpiResolvePciLink(
    _In_  ACPI_HANDLE                   bus,
    _In_  const ACPI_PCI_ROUTING_TABLE* route,
    _Out_ struct AcpiPciInterrupt*      result)
{
    struct __CurrentPciLink link = { 0 };
    ACPI_HANDLE             handle;
    ACPI_STATUS             status;

    // SourceIndex picks an entry in the link's _CRS list. We only handle a list
    // with a single interrupt, so anything other than 0 is not supported.
    if (route->SourceIndex != 0) {
        return OS_ENOTSUPPORTED;
    }

    status = AcpiGetHandle(bus, (char*)route->Source, &handle);
    if (ACPI_FAILURE(status)) {
        ERROR("ACPI PCI: link %s lookup failed (%u)", route->Source, status);
        if (status == AE_NOT_FOUND) {
            return OS_ENOTSUPPORTED;
        }
        return status == AE_NO_MEMORY ? OS_EOOM : OS_EUNKNOWN;
    }

    // ACPICA converts the resource buffer and frees it for us. The callback
    // copies the one interrupt it finds and rejects extra entries, so link only
    // holds a result we can trust when the walk succeeds.
    status = AcpiWalkResources(
        handle,
        METHOD_NAME__CRS,
        __ReadCurrentPciLinkResource,
        &link
    );
    if (ACPI_FAILURE(status)) {
        ERROR("ACPI PCI: link %s current resources failed (%u)", route->Source, status);
        if (status == AE_SUPPORT || status == AE_NOT_FOUND) {
            return OS_ENOTSUPPORTED;
        }
        if (status == AE_AML_BAD_RESOURCE_LENGTH) {
            return OS_EINVALPARAMS;
        }
        return status == AE_NO_MEMORY ? OS_EOOM : OS_EUNKNOWN;
    }

    if (!link.Found || !link.Ended) {
        WARNING("ACPI PCI: link %s has no complete single-interrupt resource list", route->Source);
        return OS_ENOTSUPPORTED;
    }
    
    *result = link.Interrupt;
    return OS_EOK;
}
