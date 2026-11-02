/**
 * Copyright 2026, Philip Meulengracht
 */

#include <stdlib.h>
#include <string.h>
#include "xhci.h"

static bool
__AddressEqual(
    _In_ USBAddress_t* left,
    _In_ USBAddress_t* right)
{
    return left->HubAddress == right->HubAddress &&
            left->PortAddress == right->PortAddress &&
            left->EndpointAddress == right->EndpointAddress;
}

static uint8_t
__EndpointToDeviceContextIndex(
    _In_ UsbManagerTransfer_t* transfer)
{
    uint8_t endpointAddress = transfer->Address.EndpointAddress & 0xF;

    if (transfer->Type == USBTRANSFER_TYPE_CONTROL && endpointAddress == 0) {
        return 1;
    }
    return (uint8_t)(endpointAddress * 2 + (transfer->Direction == USBTRANSFER_DIRECTION_IN ? 1 : 0));
}

XhciEndpoint_t*
XhciEndpointGet(
    _In_ XhciController_t* controller,
    _In_ USBAddress_t*     address)
{
    foreach(node, &controller->XhciEndpoints) {
        XhciEndpoint_t* endpoint = node->value;
        if (__AddressEqual(&endpoint->Address, address)) {
            return endpoint;
        }
    }
    return NULL;
}

XhciEndpoint_t*
XhciEndpointGetOrCreate(
    _In_ XhciController_t*     controller,
    _In_ UsbManagerTransfer_t* transfer)
{
    XhciEndpoint_t* endpoint = XhciEndpointGet(controller, &transfer->Address);
    oserr_t         oserr;

    if (endpoint != NULL) {
        if (endpoint->State == XHCI_ENDPOINT_FAILED) {
            return NULL;
        }

        endpoint->Address = transfer->Address;
        return endpoint;
    }

    endpoint = malloc(sizeof(XhciEndpoint_t));
    if (endpoint == NULL) {
        return NULL;
    }

    memset(endpoint, 0, sizeof(XhciEndpoint_t));
    endpoint->Address            = transfer->Address;
    endpoint->SlotId             = transfer->Address.DeviceAddress;
    endpoint->DeviceContextIndex = __EndpointToDeviceContextIndex(transfer);
    endpoint->MaxPacketSize      = transfer->MaxPacketSize;
    /* The default control endpoint (DCI 1) is configured implicitly as part
     * of the Address Device command, everything else needs an explicit
     * Configure Endpoint command before it can be used. */
    endpoint->State = (endpoint->DeviceContextIndex == 1) ?
            XHCI_ENDPOINT_RUNNING : XHCI_ENDPOINT_UNCONFIGURED;
    list_construct(&endpoint->Pending);
    ELEMENT_INIT(&endpoint->Header, (uintptr_t)transfer->Address.EndpointAddress, endpoint);

    // Keep partial allocations attached for controller teardown if release fails.
    list_append(&controller->XhciEndpoints, &endpoint->Header);
    
    oserr = XhciRingInitialize(controller->Base.DmaContext, &endpoint->TransferRing, XHCI_RING_TRB_COUNT);
    if (oserr != OS_EOK) {
        endpoint->State = XHCI_ENDPOINT_FAILED;
        return NULL;
    }
    return endpoint;
}

static oserr_t
__DestroyEndpoint(
    _In_ XhciController_t* controller,
    _In_ XhciEndpoint_t*   endpoint)
{
    oserr_t status;

    if (endpoint->Pending.head != NULL) {
        return OS_EBUSY;
    }

    status = XhciRingDestroy(&endpoint->TransferRing);
    if (status != OS_EOK) {
        return status;
    }

    if (endpoint->Device != NULL &&
        endpoint->Device->Endpoints[endpoint->DeviceContextIndex] == endpoint) {
        endpoint->Device->Endpoints[endpoint->DeviceContextIndex] = NULL;
    }

    list_remove(&controller->XhciEndpoints, &endpoint->Header);
    free(endpoint);
    return OS_EOK;
}

oserr_t
XhciEndpointDestroyAll(
    _In_ XhciController_t* controller)
{
    oserr_t status;

    while (controller->XhciEndpoints.head != NULL) {
        status = __DestroyEndpoint(controller, controller->XhciEndpoints.head->value);
        if (status != OS_EOK) {
            return status;
        }
    }
    return OS_EOK;
}

oserr_t
XhciEndpointDestroyDevice(
    _In_ XhciController_t* controller,
    _In_ XhciDevice_t*     device)
{
    element_t* node = controller->XhciEndpoints.head;
    oserr_t    status;

    while (node != NULL) {
        element_t*      next = node->next;
        XhciEndpoint_t* endpoint = node->value;
        
        if (endpoint->Device == device) {
            status = __DestroyEndpoint(controller, endpoint);
            if (status != OS_EOK) {
                return status;
            }
        }
        node = next;
    }
    return OS_EOK;
}

bool
XhciEndpointMetadataMatches(
    _In_ XhciEndpoint_t*       endpoint,
    _In_ UsbManagerTransfer_t* transfer)
{
    if (endpoint->State == XHCI_ENDPOINT_UNCONFIGURED) {
        return false;
    }
    return endpoint->ConfiguredMaxPacketSize == transfer->MaxPacketSize &&
            endpoint->ConfiguredInterval == transfer->TData.Periodic.Interval;
}

oserr_t
XhciEndpointEnqueueTransfer(
    _In_ XhciEndpoint_t*           endpoint,
    _In_ XhciTransferDescriptor_t* descriptor)
{
    if (endpoint == NULL || descriptor == NULL) {
        return OS_EINVALPARAMS;
    }

    ELEMENT_INIT(&descriptor->QueueHeader, descriptor->Transfer, descriptor);
    if (list_append(&endpoint->Pending, &descriptor->QueueHeader) != 0) {
        return OS_EUNKNOWN;
    }
    descriptor->Endpoint = endpoint;
    descriptor->Flags   |= XHCI_TD_FLAG_QUEUED;
    return OS_EOK;
}

void
XhciEndpointDequeueTransfer(
    _In_ XhciEndpoint_t*           endpoint,
    _In_ XhciTransferDescriptor_t* descriptor)
{
    if (endpoint == NULL || descriptor == NULL) {
        return;
    }

    if (descriptor->QueueHeader.next != NULL ||
        descriptor->QueueHeader.previous != NULL ||
        endpoint->Pending.head == &descriptor->QueueHeader)
    {
        list_remove(&endpoint->Pending, &descriptor->QueueHeader);
    }
    if (endpoint->CurrentTd == descriptor) {
        endpoint->CurrentTd = NULL;
    }
}
