/**
 * MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 *
 * ACPI(CA) Device Scan Interface
 */

#define __MODULE "acpi"
//#define __TRACE

#include <arch/utils.h>
#include <acpiinterface.h>
#include <stddef.h>
#include <assert.h>
#include <debug.h>
#include <heap.h>
#include <interrupts/interrupts.h>

#include "pci.h"

struct IrqParseContext {
    list_t* AvailableIrqs;
};

static AcpiInterruptResource_t* __CreateRoutingEntry(
        _In_ uint8_t  resourceType,
        _In_ uint8_t  polarity,
        _In_ uint8_t  triggerMode,
        _In_ uint8_t  shareable,
        _In_ uint32_t irq)
{
    AcpiInterruptResource_t* routingEntry;
    TRACE("__CreateRoutingEntry(resourceType=%u, irq=%u)", resourceType, irq);

    routingEntry = (AcpiInterruptResource_t*)kmalloc(sizeof(AcpiInterruptResource_t));
    if (!routingEntry) {
        return NULL;
    }

    ELEMENT_INIT(&routingEntry->Header, (uintptr_t)resourceType, routingEntry);
    routingEntry->AcType    = resourceType;
    routingEntry->Polarity  = polarity;
    routingEntry->Trigger   = triggerMode;
    routingEntry->Shareable = shareable;
    routingEntry->Irq       = irq;
    return routingEntry;
}

ACPI_STATUS __GetPossibleResourcesCallback(
        _In_ ACPI_RESOURCE* acpiResource,
        _In_ void*          context)
{
    struct IrqParseContext* parseContext = context;

    TRACE("__GetPossibleResourcesCallback(acpiResource->Type %u)", acpiResource->Type);
    if (acpiResource->Type == ACPI_RESOURCE_TYPE_END_TAG) {
        return AE_OK;
    }

    // Right now we are just looking for Irq's
    if (acpiResource->Type == ACPI_RESOURCE_TYPE_IRQ) {
        ACPI_RESOURCE_IRQ* irq = &acpiResource->Data.Irq;
        UINT8              i;

        if (!irq->InterruptCount) {
            WARNING("__GetPossibleResourcesCallback blank _PRS IRQ resource entry.");
            return AE_OK;
        }

        TRACE("__GetPossibleResourcesCallback irq->InterruptCount=%u", irq->InterruptCount);
        for (i = 0; i < irq->InterruptCount; i++) {
            AcpiInterruptResource_t* routingEntry = __CreateRoutingEntry(ACPI_RESOURCE_TYPE_IRQ,
                                                                         irq->Polarity, irq->Triggering,
                                                                         irq->Shareable, irq->Interrupts[i]);
            if (!routingEntry) {
                return AE_NO_MEMORY;
            }
            list_append(parseContext->AvailableIrqs, &routingEntry->Header);
        }
    }
    else if (acpiResource->Type == ACPI_RESOURCE_TYPE_EXTENDED_IRQ) {
        ACPI_RESOURCE_EXTENDED_IRQ* irq = &acpiResource->Data.ExtendedIrq;
        UINT8                       i;

        if (!irq->InterruptCount) {
            WARNING("__GetPossibleResourcesCallback blank _PRS IRQ resource entry.");
            return AE_OK;
        }

        // If the device is producing these for a child-device, then we want to skip them
        if (irq->ProducerConsumer == ACPI_PRODUCER) {
            return AE_OK;
        }

        if (irq->ResourceSource.StringLength) {
            ACPI_HANDLE handle;
            ACPI_STATUS status;

            WARNING("__GetPossibleResourcesCallback we don't have support for ext-irq resource source");
            status = AcpiGetHandle(NULL, irq->ResourceSource.StringPtr, &handle);
            if (ACPI_FAILURE(status)) {
                return status;
            }

            // handle now points to the bus/device that produces interrupts for us
        }


        TRACE("__GetPossibleResourcesCallback ext-irq->InterruptCount=%u", irq->InterruptCount);
        for (i = 0; i < irq->InterruptCount; i++) {
            AcpiInterruptResource_t* routingEntry = __CreateRoutingEntry(ACPI_RESOURCE_TYPE_EXTENDED_IRQ,
                                                                         irq->Polarity, irq->Triggering,
                                                                         irq->Shareable, irq->Interrupts[i]);
            if (!routingEntry) {
                return AE_NO_MEMORY;
            }
            list_append(parseContext->AvailableIrqs, &routingEntry->Header);
        }
    }

    return AE_OK;
}

ACPI_STATUS
__GetCurrentResourceCallback(
    _In_ ACPI_RESOURCE* acpiResource,
    _In_ void*          context)
{
    AcpiInterruptSource_t* interruptSource = context;
    uint32_t               irqNumber = 0xFFFFFFFF;

    TRACE("__GetCurrentResourceCallback(acpiResource->Type %u)", acpiResource->Type);
    if (acpiResource->Type == ACPI_RESOURCE_TYPE_END_TAG) {
        return AE_OK;
    }

    // Right now we are just looking for Irq's
    if (acpiResource->Type == ACPI_RESOURCE_TYPE_IRQ) {
        ACPI_RESOURCE_IRQ* irq = &acpiResource->Data.Irq;

        if (!irq->InterruptCount) {
            WARNING("__GetCurrentResourceCallback blank _CSR IRQ resource entry. Device is disabled.");
            return AE_OK;
        }

        TRACE("__GetCurrentResourceCallback irq->InterruptCount=%u", irq->InterruptCount);
        irqNumber = irq->Interrupts[0];
    } else if (acpiResource->Type == ACPI_RESOURCE_TYPE_EXTENDED_IRQ) {
        ACPI_RESOURCE_EXTENDED_IRQ* irq = &acpiResource->Data.ExtendedIrq;

        if (!irq->InterruptCount) {
            WARNING("__GetCurrentResourceCallback blank _CSR IRQ resource entry. Device is disabled.");
            return AE_OK;
        }

        // If the device is producing these for a child-device, then we want to skip them
        if (irq->ProducerConsumer == ACPI_PRODUCER) {
            return AE_OK;
        }

        TRACE("__GetCurrentResourceCallback irq->InterruptCount=%u", irq->InterruptCount);
        irqNumber = irq->Interrupts[0];
    }

    foreach(element, &interruptSource->PossibleInterrupts) {
        AcpiInterruptResource_t* acpiInterrupt = element->value;
        if (acpiInterrupt->Irq == irqNumber) {
            TRACE("__GetCurrentResourceCallback acpiInterrupt->irq=%i", acpiInterrupt->Irq);
            interruptSource->CurrentInterrupt = acpiInterrupt;
            InterruptIncreasePenalty((int)irqNumber); // increase load on that irq
            break;
        }
    }
    return AE_OK;
}

static AcpiInterruptResource_t*
__GetLeastLoadedEntry(
    _In_ list_t* possibleInterrupts)
{
    int interruptList[64];
    int count = 0;

    TRACE("__GetLeastLoadedEntry()");

    foreach(i, possibleInterrupts) {
        AcpiInterruptResource_t* entry = (AcpiInterruptResource_t*)i->value;
        interruptList[count++] = entry->Irq;
        if (count >= 64) {
            WARNING("__GetLeastLoadedEntry reached maximum interrupt list size of 64");
            break;
        }
    }

    count = InterruptGetLeastLoaded(interruptList, count);
    if (count == INTERRUPT_NONE) {
        ERROR("__GetLeastLoadedEntry no valid interrupt found");
        return NULL;
    }

    foreach(i, possibleInterrupts) {
        AcpiInterruptResource_t* entry = (AcpiInterruptResource_t*)i->value;
        if (entry->Irq == count) {
            return entry;
        }
    }
    ERROR("__GetLeastLoadedEntry couldn't refind interrupt %" PRIiIN, count);
    return NULL;
}

static ACPI_STATUS
__SetActiveInterrupt(
    _In_ ACPI_HANDLE              deviceHandle,
    _In_ AcpiInterruptResource_t* interrupt)
{
    ACPI_STATUS status = AE_OK;
    ACPI_BUFFER acpiBuffer;
    struct {
        ACPI_RESOURCE Irq;
        ACPI_RESOURCE End;
    }          *resource;

    TRACE("__SetActiveInterrupt updating device with irq %u", interrupt->Irq);
    resource = kmalloc(sizeof(*resource) + 1);
    if (!resource) {
        return AE_NO_MEMORY;
    }

    memset(resource, 0, sizeof(*resource) + 1);
    acpiBuffer.Length  = sizeof(*resource) + 1;
    acpiBuffer.Pointer = (void*)resource;

    // Fill out
    if (interrupt->AcType == ACPI_RESOURCE_TYPE_IRQ) {
        resource->Irq.Type                    = ACPI_RESOURCE_TYPE_IRQ;
        resource->Irq.Length                  = sizeof(ACPI_RESOURCE);
        resource->Irq.Data.Irq.Triggering     = interrupt->Trigger;
        resource->Irq.Data.Irq.Polarity       = interrupt->Polarity;
        resource->Irq.Data.Irq.Shareable      = interrupt->Shareable;
        resource->Irq.Data.Irq.InterruptCount = 1;
        resource->Irq.Data.Irq.Interrupts[0] = (UINT8)interrupt->Irq;
    }
    else {
        resource->Irq.Type                              = ACPI_RESOURCE_TYPE_EXTENDED_IRQ;
        resource->Irq.Length                            = sizeof(ACPI_RESOURCE);
        resource->Irq.Data.ExtendedIrq.ProducerConsumer = ACPI_CONSUMER;
        resource->Irq.Data.ExtendedIrq.Triggering       = interrupt->Trigger;
        resource->Irq.Data.ExtendedIrq.Polarity         = interrupt->Polarity;
        resource->Irq.Data.ExtendedIrq.Shareable        = interrupt->Shareable;
        resource->Irq.Data.ExtendedIrq.InterruptCount   = 1;
        resource->Irq.Data.ExtendedIrq.Interrupts[0] = (UINT8)interrupt->Irq;
    }

    // Setup end-tag
    resource->End.Type   = ACPI_RESOURCE_TYPE_END_TAG;
    resource->End.Length = sizeof(ACPI_RESOURCE);

    // Try to set current resource
    status = AcpiSetCurrentResources(deviceHandle, &acpiBuffer);
    if (ACPI_FAILURE(status)) {
        ERROR("__SetActiveInterrupt failed to update the current irq resource, code %" PRIuIN, status);
        return status;
    }
    return status;
}

ACPI_STATUS __SelectBestIrq(
        _In_ AcpiInterruptSource_t* interruptSource)
{
    AcpiInterruptResource_t* interruptResource;
    unsigned int             deviceStatus;
    ACPI_STATUS              status;
    TRACE("__SelectBestIrq(interruptSource=0x%" PRIxIN ")", interruptSource);

    // Get the best possible irq currently for load-balancing
    interruptResource = __GetLeastLoadedEntry(&interruptSource->PossibleInterrupts);
    if (!interruptResource) {
        TRACE("__SelectBestIrq no possible irq for device out of %i entries",
              list_count(&interruptSource->PossibleInterrupts));
        return AE_ERROR;
    }

    status = __SetActiveInterrupt(interruptSource->Handle, interruptResource);
    if (ACPI_FAILURE(status)) {
        ERROR("__SelectBestIrq failed to update the current irq resource, code %" PRIuIN "", status);
        return status;
    }
    interruptSource->CurrentInterrupt = interruptResource;

    // Get current source-handle status _STA
    status = AcpiDeviceQueryStatus(interruptSource->Handle, &deviceStatus);
    if (ACPI_FAILURE(status)) {
        ERROR("__SelectBestIrq failed to get device status: %u", status);
        return status;
    }
    return AE_OK;
}

static ACPI_STATUS __FillPossibleSources(
        _In_ ACPI_HANDLE*           tableHandle,
        _In_ AcpiInterruptSource_t* routingSource)
{
    struct IrqParseContext parseContext;
    ACPI_STATUS            acpiStatus;
    TRACE("__FillPossibleSources(tableHandle=0x%" PRIxIN ", routingSource=0x%" PRIxIN,
          tableHandle, routingSource);

    // Store the information for the callback
    parseContext.AvailableIrqs = &routingSource->PossibleInterrupts;

    acpiStatus = AcpiWalkResources(tableHandle, METHOD_NAME__PRS, __GetPossibleResourcesCallback, &parseContext);
    if (ACPI_FAILURE(acpiStatus)) {
        goto exit;
    }

exit:
    TRACE("__FillPossibleSources returns=%u", acpiStatus);
    return acpiStatus;
}

static ACPI_STATUS __FillCurrentSources(
        _In_ ACPI_HANDLE*           tableHandle,
        _In_ AcpiInterruptSource_t* interruptSource)
{
    ACPI_STATUS acpiStatus;
    TRACE("__FillCurrentSources(tableHandle=0x%" PRIxIN ", interruptSource=0x%" PRIxIN,
          tableHandle, interruptSource);

    // Walk the handle and call all __CRS methods
    acpiStatus = AcpiWalkResources(tableHandle, METHOD_NAME__CRS, __GetCurrentResourceCallback, interruptSource);
    if (ACPI_FAILURE(acpiStatus)) {
        goto exit;
    }

exit:
    TRACE("__FillCurrentSources returns=%u", acpiStatus);
    return acpiStatus;
}

static ACPI_STATUS __CreateInterruptSource(
        _In_  ACPI_HANDLE             deviceHandle,
        _In_  ACPI_PCI_ROUTING_TABLE* pciRoutingTable,
        _Out_ AcpiInterruptSource_t** interruptSourceOut)
{
    AcpiInterruptSource_t* interruptSource;
    ACPI_STATUS            acpiStatus;
    ACPI_HANDLE            handle;

    // Get handle of the source-table
    acpiStatus = AcpiGetHandle(deviceHandle, pciRoutingTable->Source, &handle);
    if (ACPI_FAILURE(acpiStatus)) {
        ERROR("Failed AcpiGetHandle");
        return acpiStatus;
    }

    interruptSource = kmalloc(sizeof(*interruptSource) + strlen(pciRoutingTable->Source) + 1);
    if (!interruptSource) {
        return AE_NO_MEMORY;
    }

    // The _PRT buffer is freed after parsing, so copy the source name into this
    // allocation. Later lookups by function then never read freed memory.
    strcpy((char*)(interruptSource + 1), pciRoutingTable->Source);
    ELEMENT_INIT(&interruptSource->Header, (char*)(interruptSource + 1), interruptSource);
    interruptSource->CurrentInterrupt = NULL;
    list_construct(&interruptSource->PossibleInterrupts);
    interruptSource->Handle = handle;

    *interruptSourceOut = interruptSource;
    return acpiStatus;
}

static ACPI_STATUS __ParsePRTTable(
        _In_ ACPI_HANDLE             deviceHandle,
        _In_ AcpiBusRoutings_t*      routings,
        _In_ ACPI_PCI_ROUTING_TABLE* pciRoutingTable)
{
    ACPI_STATUS            acpiStatus = AE_OK;
    AcpiInterruptSource_t* interruptSource;
    unsigned int           deviceIndex;
    TRACE("__ParsePRTTable(deviceHandle=0x%" PRIxIN ")", deviceHandle);

    // Convert the addresses
    deviceIndex    = (unsigned int)((pciRoutingTable->Address >> 16) & 0xFFFF);

    // The fixed routing table has room for ACPI device numbers 0 through 31
    // and pins 0 through 3. Reject malformed firmware data before indexing.
    if (deviceIndex >= ACPI_PRT_DEVICE_COUNT ||
        pciRoutingTable->Pin >= ACPI_PRT_PIN_COUNT) {
        ERROR("Invalid _PRT device or pin");
        return AE_BAD_PARAMETER;
    }

    // Direct GSIs are read from the matching _PRT entry when queried. Only link
    // devices need to be cached, as selecting an IRQ for them may run _SRS.
    if (pciRoutingTable->Source[0] == '\0') {
        if (pciRoutingTable->SourceIndex > INT32_MAX) {
            return AE_BAD_DATA;
        }

        // Count fixed routes in the x86 IRQ penalty before IRQs are selected
        // for link devices. Queries must never change the penalty.
        InterruptIncreasePenalty((int)pciRoutingTable->SourceIndex);
        return AE_OK;
    }

    if (pciRoutingTable->SourceIndex != 0) {
        return AE_SUPPORT;
    }

    // Ok, so we have a valid handle, lets see if we already have the handle cached in memory
    interruptSource = (AcpiInterruptSource_t*)list_find_value(&routings->Sources, &pciRoutingTable->Source[0]);
    if (!interruptSource) {
        acpiStatus = __CreateInterruptSource(deviceHandle, pciRoutingTable, &interruptSource);
        if (ACPI_FAILURE(acpiStatus)) {
            goto exit;
        }
        list_append(&routings->Sources, &interruptSource->Header);

        acpiStatus = __FillPossibleSources(interruptSource->Handle, interruptSource);
        if (ACPI_FAILURE(acpiStatus)) {
            goto exit;
        }

        // get active source
        acpiStatus = __FillCurrentSources(interruptSource->Handle, interruptSource);
        if (ACPI_FAILURE(acpiStatus)) {
            goto exit;
        }

        if (!interruptSource->CurrentInterrupt) {
            acpiStatus = __SelectBestIrq(interruptSource);
            if (ACPI_FAILURE(acpiStatus)) {
                goto exit;
            }
        }
    }

    TRACE("__ParsePRTTable source=%s", &pciRoutingTable->Source[0]);

exit:
    TRACE("__ParsePRTTable returns=%u", acpiStatus);
    return acpiStatus;
}

static ACPI_STATUS __ValidatePrt(const ACPI_BUFFER* buffer);

static ACPI_STATUS __ParsePRTBuffer(
        _In_ ACPI_HANDLE        deviceHandle,
        _In_ AcpiBusRoutings_t* routings,
        _In_ void*              prtBuffer)
{
    ACPI_PCI_ROUTING_TABLE* pciRoutingTable = (ACPI_PCI_ROUTING_TABLE *)prtBuffer;
    ACPI_STATUS             acpiStatus = AE_OK;
    TRACE("__ParsePRTBuffer(deviceHandle=0x%" PRIxIN ")", deviceHandle);

    for (; pciRoutingTable->Length;
           pciRoutingTable = (ACPI_PCI_ROUTING_TABLE *)((char *)pciRoutingTable + pciRoutingTable->Length)) {
        acpiStatus = __ParsePRTTable(deviceHandle, routings, pciRoutingTable);
        if (ACPI_FAILURE(acpiStatus)) {
            ERROR("__ParsePRTBuffer failed to parse PRT table");
            break;
        }
    }
    return acpiStatus;
}

static AcpiBusRoutings_t* __CreateAcpiRoutings(void)
{
    AcpiBusRoutings_t* routings;

    routings = (AcpiBusRoutings_t*)kmalloc(sizeof(AcpiBusRoutings_t));
    if (!routings) {
        return NULL;
    }

    memset(routings, 0, sizeof(AcpiBusRoutings_t));
    list_construct_cmp(&routings->Sources, list_cmp_string);
    return routings;
}

static void
__DestroyRoutings(
    _In_ AcpiBusRoutings_t* routings)
{
    element_t*             entry;
    AcpiInterruptSource_t* source;

    while (routings->Sources.head != NULL) {
        entry = routings->Sources.head;
        source = entry->value;
        list_remove(&routings->Sources, entry);
        while (source->PossibleInterrupts.head != NULL) {
            entry = source->PossibleInterrupts.head;
            list_remove(&source->PossibleInterrupts, entry);
            kfree(entry->value);
        }
        kfree(source);
    }
    kfree(routings);
}

ACPI_STATUS
AcpiDeviceGetIrqRoutings(
    _In_ AcpiDevice_t* device)
{
    AcpiBusRoutings_t* routings;
    ACPI_STATUS        status;
    ACPI_BUFFER        buffer = { ACPI_ALLOCATE_BUFFER, NULL };

    status = AcpiGetIrqRoutingTable(device->Handle, &buffer);
    if (ACPI_FAILURE(status)) {
        return status;
    }

    status = __ValidatePrt(&buffer);
    if (ACPI_FAILURE(status)) {
        AcpiOsFree(buffer.Pointer);
        return status;
    }

    routings = __CreateAcpiRoutings();
    if (!routings) {
        AcpiOsFree(buffer.Pointer);
        return AE_NO_MEMORY;
    }

    status = __ParsePRTBuffer(device->Handle, routings, buffer.Pointer);
    AcpiOsFree(buffer.Pointer);
    if (ACPI_FAILURE(status)) {
        __DestroyRoutings(routings);
        return status;
    }

    device->Routings = routings;
    return AE_OK;
}

static ACPI_STATUS
__ValidatePrt(
    _In_ const ACPI_BUFFER* buffer)
{
    ACPI_PCI_ROUTING_TABLE* entry;
    size_t                  offset = 0;
    size_t                  sourceOffset = offsetof(ACPI_PCI_ROUTING_TABLE, Source);
    uint32_t                length;
    uint32_t                function;

    while (buffer->Length - offset >= sizeof(length)) {
        memcpy(&length, (const char*)buffer->Pointer + offset, sizeof(length));
        if (!length) {
            return AE_OK;
        }

        if (length > buffer->Length - offset || length <= sourceOffset ||
            (length & (sizeof(UINT64) - 1))) {
            return AE_BAD_DATA;
        }

        entry = (void*)((char*)buffer->Pointer + offset);
        function = entry->Address & 0xffff;
        if (entry->Address >> 32 || (entry->Address >> 16) > 31 || entry->Pin > 3) {
            return AE_BAD_DATA;
        }

        if (function > 7 && function != 0xffff) {
            return AE_BAD_DATA;
        }

        if (!memchr(entry->Source, 0, length - sourceOffset)) {
            return AE_BAD_DATA;
        }

        offset += length;
    }
    return AE_BAD_DATA;
}

oserr_t
AcpiPciRoute(
    _In_  ACPI_HANDLE                bus,
    _In_  const struct AcpiPciQuery* query,
    _Out_ struct AcpiPciInterrupt*   result)
{
    ACPI_BUFFER             buffer = { ACPI_ALLOCATE_BUFFER, NULL };
    ACPI_PCI_ROUTING_TABLE* entry;
    ACPI_PCI_ROUTING_TABLE* match = NULL;
    ACPI_STATUS             acpiStatus;
    oserr_t                 status = OS_ENOENT;
    size_t                  offset;
    uint32_t                function;
    int                     exact;
    int                     matchedExact = 0;

#if defined(__i386__) || defined(__amd64__)
    AcpiDevice_t*            device;
    AcpiInterruptSource_t*   source;
    AcpiInterruptResource_t* interrupt;
#endif

    acpiStatus = AcpiGetIrqRoutingTable(bus, &buffer);
    if (ACPI_FAILURE(acpiStatus)) {
        return acpiStatus == AE_NOT_FOUND ? OS_ENOENT : OS_EUNKNOWN;
    }

    if (ACPI_FAILURE(__ValidatePrt(&buffer))) {
        status = OS_EINVALPARAMS;
        goto exit;
    }

    // Match the device/function from _ADR, including the ACPI function wildcard.
    // A route for the exact function wins over a wildcard route, and two routes
    // that match equally well are ambiguous. Swizzling is only done by deviced,
    // never here.
    for (offset = 0; ; offset += entry->Length) {
        entry = (void*)((char*)buffer.Pointer + offset);
        if (!entry->Length) {
            break;
        }
        function = entry->Address & 0xffff;
        if ((entry->Address >> 16) != query->Address.Device || entry->Pin != query->Pin - 1U) {
            continue;
        }
        exact = function == query->Address.Function;
        if (!exact && function != 0xffff) {
            continue;
        }
        if (match && matchedExact == exact) {
            status = OS_EINVALPARAMS;
            goto exit;
        }
        if (!match || exact) {
            match = entry;
            matchedExact = exact;
        }
    }

    if (!match) {
        goto exit;
    }

    if (!match->Source[0]) {
        if (match->SourceIndex > INT32_MAX) {
            status = OS_EINVALPARAMS;
            goto exit;
        }
        result->Line = (int32_t)match->SourceIndex;
        result->Conform = INTERRUPT_ACPICONFORM_PRESENT | INTERRUPT_ACPICONFORM_FIXED |
            INTERRUPT_ACPICONFORM_TRIGGERMODE | INTERRUPT_ACPICONFORM_POLARITY |
            INTERRUPT_ACPICONFORM_SHAREABLE;
        status = OS_EOK;
        goto exit;
    }

    status = OS_ENOTSUPPORTED;

#if defined(__i386__) || defined(__amd64__)
    // On x86 we keep the existing link device setup and the IRQ it selected.
    // Match on the _PRT source of the route we found, instead of the old
    // device/pin cache index, so a route for one function cannot pick up the
    // link of another function.
    device = AcpiDeviceLookupHandle(bus);
    if (!device || !device->Routings || match->SourceIndex != 0) {
        goto exit;
    }

    source = list_find_value(&device->Routings->Sources, match->Source);
    if (!source || !source->CurrentInterrupt) {
        goto exit;
    }

    interrupt = source->CurrentInterrupt;
    result->Line = interrupt->Irq;
    result->Conform = INTERRUPT_ACPICONFORM_PRESENT;
    if (interrupt->Trigger == ACPI_LEVEL_SENSITIVE) {
        result->Conform |= INTERRUPT_ACPICONFORM_TRIGGERMODE;
    }
    if (interrupt->Polarity == ACPI_ACTIVE_LOW) {
        result->Conform |= INTERRUPT_ACPICONFORM_POLARITY;
    }
    if (interrupt->Shareable) {
        result->Conform |= INTERRUPT_ACPICONFORM_SHAREABLE;
    }

    status = OS_EOK;
#endif

exit:
    AcpiOsFree(buffer.Pointer);
    return status;
}

int
AcpiGetPolarityMode(
        _In_ uint16_t intiFlags,
        _In_ int      source)
{
    // Parse the different polarity flags
    switch (intiFlags & ACPI_MADT_POLARITY_MASK) {
        case ACPI_MADT_POLARITY_CONFORMS: {
            if (source == (int)AcpiGbl_FADT.SciInterrupt)
                return 1;
            else
                return 0;
        } break;
        case ACPI_MADT_POLARITY_ACTIVE_HIGH:
            return 0;
        case ACPI_MADT_POLARITY_ACTIVE_LOW:
            return 1;
    }
    return 0;
}

int
AcpiGetTriggerMode(
        _In_ uint16_t intiFlags,
        _In_ int      source)
{
    // Parse the different trigger mode flags
    switch (intiFlags & ACPI_MADT_TRIGGER_MASK) {
        case ACPI_MADT_TRIGGER_CONFORMS: {
            if (source == (int)AcpiGbl_FADT.SciInterrupt)
                return 1;
            else
                return 0;
        } break;
        case ACPI_MADT_TRIGGER_EDGE:
            return 0;
        case ACPI_MADT_TRIGGER_LEVEL:
            return 1;
    }
    return 0;
}

unsigned int
ConvertAcpiFlagsToConformFlags(
        _In_ uint16_t intiFlags,
        _In_ int      source)
{
    unsigned int conformFlags = 0;

    if (AcpiGetPolarityMode(intiFlags, source) == 1) {
        conformFlags |= INTERRUPT_ACPICONFORM_POLARITY;
    }
    if (AcpiGetTriggerMode(intiFlags, source) == 1) {
        conformFlags |= INTERRUPT_ACPICONFORM_TRIGGERMODE;
    }
    return conformFlags;
}
