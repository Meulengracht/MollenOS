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
#include <assert.h>
#include <debug.h>
#include <heap.h>
#include <interrupts.h>

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

ACPI_STATUS __GetCurrentResourceCallback(
        _In_ ACPI_RESOURCE* acpiResource,
        _In_ void*          context)
{
    AcpiInterruptSource_t* interruptSource = context;
    uint32_t               irqNumber;

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
    }
    else if (acpiResource->Type == ACPI_RESOURCE_TYPE_EXTENDED_IRQ) {
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

static AcpiInterruptResource_t* __GetLeastLoadedEntry(
        _In_ list_t* possibleInterrupts)
{
    int interruptList[64];
    int count = 0;

    TRACE("__GetLeastLoadedEntry()");

    foreach(i, possibleInterrupts) {
        AcpiInterruptResource_t* entry = (AcpiInterruptResource_t*)i->value;
        interruptList[count] = entry->Irq;
        count++;
    }

    count = InterruptGetLeastLoaded(interruptList, count);
    if (count == INTERRUPT_NONE) {
        ERROR("__GetLeastLoadedEntry no valid interrupt found");
        return NULL;
    }

    _foreach(i, possibleInterrupts) {
        AcpiInterruptResource_t* entry = (AcpiInterruptResource_t*)i->value;
        if (entry->Irq == count) {
            return entry;
        }
    }
    ERROR("__GetLeastLoadedEntry couldn't refind interrupt %" PRIiIN, count);
    return NULL;
}

static ACPI_STATUS __SetActiveInterrupt(
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

static ACPI_STATUS __CreateFixedInterruptSource(
        _In_ ACPI_PCI_ROUTING_TABLE* pciRoutingTable,
        _In_ AcpiRoutingEntry_t*     routeEntry)
{
    AcpiInterruptResource_t* routingEntry;
    TRACE("__CreateFixedInterruptSource(routeEntry=0x%" PRIxIN ")", routeEntry);

    if (!routeEntry->InterruptSource) {
        routeEntry->InterruptSource = kmalloc(sizeof(AcpiInterruptSource_t));
        if (!routeEntry->InterruptSource) {
            return AE_NO_MEMORY;
        }

        // initialize the interrupt source, set HANDLE=NULL
        ELEMENT_INIT(&routeEntry->InterruptSource->Header, 0, routeEntry->InterruptSource);
        list_construct(&routeEntry->InterruptSource->PossibleInterrupts);
        routeEntry->InterruptSource->Handle = NULL;
        routeEntry->InterruptSource->CurrentInterrupt = NULL;
    }

    // Allocate a new entry and store information
    routingEntry = __CreateRoutingEntry(ACPI_RESOURCE_TYPE_IRQ, ACPI_ACTIVE_LOW,
                                        ACPI_LEVEL_SENSITIVE, 0,
                                        pciRoutingTable->SourceIndex);
    if (!routingEntry) {
        return AE_NO_MEMORY;
    }

    // set as active and add to list
    InterruptIncreasePenalty((int)pciRoutingTable->SourceIndex);
    routeEntry->InterruptSource->CurrentInterrupt = routingEntry;
    list_append(&routeEntry->InterruptSource->PossibleInterrupts, &routingEntry->Header);
    return AE_OK;
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

    interruptSource = (AcpiInterruptSource_t*)kmalloc(sizeof(AcpiInterruptSource_t));
    if (!interruptSource) {
        return AE_NO_MEMORY;
    }

    ELEMENT_INIT(&interruptSource->Header, &pciRoutingTable->Source[0], interruptSource);
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
    AcpiRoutingEntry_t*    routeEntry;
    unsigned int           interruptIndex;
    unsigned int           deviceIndex;
    TRACE("__ParsePRTTable(deviceHandle=0x%" PRIxIN ")", deviceHandle);

    // Convert the addresses
    deviceIndex    = (unsigned int)((pciRoutingTable->Address >> 16) & 0xFFFF);
    interruptIndex = (deviceIndex * 4) + pciRoutingTable->Pin;
    routeEntry     = &routings->InterruptEntries[interruptIndex];

    TRACE("__ParsePRTTable deviceIndex=%u, interruptIndex=%u, pciRoutingTable->sourceIndex=%u",
          deviceIndex, interruptIndex, pciRoutingTable->SourceIndex);
    // Check if the first byte is 0, then there is no irq-resource
    // Then the SourceIndex is the actual IRQ
    if (pciRoutingTable->Source[0] == '\0') {
        acpiStatus = __CreateFixedInterruptSource(pciRoutingTable, routeEntry);
        goto exit;
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

    routeEntry->InterruptSource = interruptSource;
    TRACE("__ParsePRTTable source=%s", &pciRoutingTable->Source[0]);

exit:
    TRACE("__ParsePRTTable returns=%u", acpiStatus);
    return acpiStatus;
}

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

ACPI_STATUS
AcpiDeviceGetIrqRoutings(
        _In_ AcpiDevice_t* device)
{
    AcpiBusRoutings_t* routings;
    ACPI_STATUS    acpiStatus;
    ACPI_BUFFER    acpiBuffer;

    TRACE("AcpiDeviceGetIrqRoutings(device=0x%" PRIxIN ")", device);

    acpiBuffer.Length  = 0x2000;
    acpiBuffer.Pointer = (char*)kmalloc(0x2000);
    if (!acpiBuffer.Pointer) {
        acpiStatus = AE_NO_MEMORY;
        goto exit;
    }

    memset(acpiBuffer.Pointer, 0, 0x2000);

    routings = __CreateAcpiRoutings();
    if (!routings) {
        acpiStatus = AE_NO_MEMORY;
        goto exit;
    }

    // Execute the _PRT method
    acpiStatus = AcpiGetIrqRoutingTable(device->Handle, &acpiBuffer);
    if (ACPI_FAILURE(acpiStatus)) {
        ERROR("AcpiDeviceGetIrqRoutings failed to extract irq routings");
        goto exit;
    }

    acpiStatus = __ParsePRTBuffer(device->Handle, routings, acpiBuffer.Pointer);
    if (ACPI_FAILURE(acpiStatus)) {
        ERROR("AcpiDeviceGetIrqRoutings failed to parse irq routings");
        goto exit;
    }

    device->Routings = routings;
exit:
    if (acpiBuffer.Pointer) {
        kfree(acpiBuffer.Pointer);
    }

    TRACE("AcpiDeviceGetIrqRoutings returns=%u", acpiStatus);
    return acpiStatus;
}

OsStatus_t
AcpiDeviceGetInterrupt(
        _In_  int           bus,
        _In_  int           device,
        _In_  int           pciPin,
        _Out_ int*          interruptOut,
        _Out_ unsigned int* acpiConformOut)
{
    AcpiInterruptSource_t* interruptSource;
    AcpiDevice_t*          acpiDevice;
    int                    index = (device * 4) + (pciPin - 1);
    unsigned int           flags = INTERRUPT_ACPICONFORM_PRESENT;

    TRACE("AcpiDeviceGetInterrupt(bus=%i, device=%i, pciPin=%i)",
          bus, device, pciPin);

    // Start by checking if we can find the
    // routings by checking the given device
    acpiDevice = AcpiDeviceLookupBusRoutings(bus);
    if (!acpiDevice || !acpiDevice->Routings) {
        return OsDoesNotExist;
    }

    TRACE("AcpiDeviceGetInterrupt found bus-device <%s>, accessing index %i",
          &acpiDevice->HId[0], index);
    interruptSource = acpiDevice->Routings->InterruptEntries[index].InterruptSource;
    if (!interruptSource || !interruptSource->CurrentInterrupt) {
        return OsDoesNotExist;
    }

    // convert flags
    if (interruptSource->CurrentInterrupt->Trigger == ACPI_LEVEL_SENSITIVE) { flags |= INTERRUPT_ACPICONFORM_TRIGGERMODE; }
    if (interruptSource->CurrentInterrupt->Polarity == ACPI_ACTIVE_LOW)     { flags |= INTERRUPT_ACPICONFORM_POLARITY; }
    if (interruptSource->CurrentInterrupt->Shareable)                       { flags |= INTERRUPT_ACPICONFORM_SHAREABLE; }
    if (!interruptSource->Handle)                                           { flags |= INTERRUPT_ACPICONFORM_FIXED; }

    // Update IRQ Information
    *acpiConformOut = flags;
    *interruptOut = (int)interruptSource->CurrentInterrupt->Irq;

    TRACE("AcpiDeriveInterrupt found irq=%u, flags=0x%x",
          interruptSource->CurrentInterrupt->Irq, flags);
    return OsSuccess;
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
