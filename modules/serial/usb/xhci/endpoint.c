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
            left->DeviceAddress == right->DeviceAddress &&
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
    foreach(node, &controller->Endpoints) {
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
    list_construct(&endpoint->Pending);
    ELEMENT_INIT(&endpoint->Header, (uintptr_t)transfer->Address.EndpointAddress, endpoint);

    oserr = XhciRingInitialize(&endpoint->TransferRing, XHCI_RING_TRB_COUNT);
    if (oserr != OS_EOK) {
        free(endpoint);
        return NULL;
    }

    if (list_append(&controller->Endpoints, &endpoint->Header) != 0) {
        XhciRingDestroy(&endpoint->TransferRing);
        free(endpoint);
        return NULL;
    }
    return endpoint;
}

static void
__DestroyEndpoint(
    _In_ element_t* item,
    _In_ void*      context)
{
    XhciEndpoint_t* endpoint = item->value;
    _CRT_UNUSED(context);

    XhciRingDestroy(&endpoint->TransferRing);
    free(endpoint);
}

void
XhciEndpointDestroyAll(
    _In_ XhciController_t* controller)
{
    list_clear(&controller->Endpoints, __DestroyEndpoint, NULL);
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
