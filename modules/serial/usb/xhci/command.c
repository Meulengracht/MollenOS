/**
 * Copyright 2026, Philip Meulengracht
 */

#include <ddk/barrier.h>
#include <ddk/io.h>
#include <os/handle.h>
#include <os/shm.h>
#include <stdlib.h>
#include <string.h>
#include "xhci.h"

#define XHCI_INPUT_CONTEXT_ENTRIES  33
#define XHCI_DEVICE_CONTEXT_ENTRIES 32

#define XHCI_CONTEXT_SLOT           1
#define XHCI_CONTEXT_EP0            2

#define XHCI_INPUT_ADD_SLOT         (1 << 0)
#define XHCI_INPUT_ADD_EP0          (1 << 1)
#define XHCI_INPUT_DROP_DCI(dci)     (1u << (dci))
#define XHCI_INPUT_ADD_DCI(dci)      (1u << (dci))
#define XHCI_SLOT_CONTEXT_ENTRIES(n) (((n) & 0x1F) << 27)
#define XHCI_SLOT_CONTEXT_SPEED(n)   (((n) & 0xF) << 20)
#define XHCI_SLOT_CONTEXT_PORT(n)    (((n) & 0xFF) << 16)
#define XHCI_SLOT_CONTEXT_ENTRIES(n) (((n) & 0x1F) << 27)
#define XHCI_SLOT_CONTEXT_SPEED(n)   (((n) & 0xF) << 20)
#define XHCI_EP_CONTEXT_CERR(n)      (((n) & 0x3) << 1)
#define XHCI_EP_CONTEXT_TYPE(n)      (((n) & 0x7) << 3)
#define XHCI_EP_CONTEXT_TYPE_CONTROL XHCI_EP_CONTEXT_TYPE(4)
#define XHCI_EP_CONTEXT_MPS(n)       (((n) & 0xFFFF) << 16)
#define XHCI_EP_CONTEXT_AVG_TRB(n)   ((n) & 0xFFFF)
#define XHCI_EP_CONTEXT_DCS          (1 << 0)
#define XHCI_EP_CONTEXT_INTERVAL(n)  (((n) & 0xFF) << 16)

#define XHCI_EP_TYPE_ISOC_OUT 1
#define XHCI_EP_TYPE_BULK_OUT 2
#define XHCI_EP_TYPE_INT_OUT  3
#define XHCI_EP_TYPE_CONTROL  4
#define XHCI_EP_TYPE_ISOC_IN  5
#define XHCI_EP_TYPE_BULK_IN  6
#define XHCI_EP_TYPE_INT_IN   7

static uint8_t
__SpeedId(
    _In_ enum USBSpeed speed)
{
    switch (speed) {
        case USBSPEED_LOW:        return 2;
        case USBSPEED_HIGH:       return 3;
        case USBSPEED_SUPER:      return 4;
        case USBSPEED_SUPER_PLUS: return 5;
        case USBSPEED_FULL:
        default:                   return 1;
    }
}

static oserr_t
__AllocateContext(
    _In_  size_t        size,
    _Out_ OSHandle_t*   handle,
    _Out_ SHMSGTable_t* table,
    _Out_ uint8_t**     bufferOut)
{
    oserr_t oserr;

    oserr = SHMCreate(
            &(SHM_t) {
                    .Flags = SHM_DEVICE | SHM_PRIVATE | SHM_CLEAN,
                    .Conformity = OSMEMORYCONFORMITY_LOW,
                    .Size = size,
                    .Access = SHM_ACCESS_READ | SHM_ACCESS_WRITE
            },
            handle
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = SHMGetSGTable(handle, table, -1);
    if (oserr != OS_EOK || table->Count != 1) {
        OSHandleDestroy(handle);
        memset(handle, 0, sizeof(OSHandle_t));
        free(table->Entries);
        memset(table, 0, sizeof(SHMSGTable_t));
        return oserr == OS_EOK ? OS_EUNKNOWN : oserr;
    }

    *bufferOut = SHMBuffer(handle);
    return OS_EOK;
}

static void
__FreeContext(
    _In_ OSHandle_t*   handle,
    _In_ SHMSGTable_t* table)
{
    OSHandleDestroy(handle);
    free(table->Entries);
    memset(table, 0, sizeof(SHMSGTable_t));
}

static XhciDevice_t*
__DeviceGet(
    _In_ XhciController_t* controller,
    _In_ USBAddress_t*     address)
{
    foreach(node, &controller->Devices) {
        XhciDevice_t* device = node->value;
        if (device->HubAddress == address->HubAddress &&
            device->PortAddress == address->PortAddress) {
            return device;
        }
    }
    return NULL;
}

static XhciDevice_t*
__DeviceGetByUsbAddress(
    _In_ XhciController_t* controller,
    _In_ uint8_t           address)
{
    foreach(node, &controller->Devices) {
        XhciDevice_t* device = node->value;
        if (device->UsbAddress == address) {
            return device;
        }
    }
    return NULL;
}

static oserr_t
__SubmitCommand(
    _In_ XhciController_t*  controller,
    _In_ XhciDevice_t*      device,
    _In_ XhciEndpoint_t*    endpoint,
    _In_ enum XhciCommandType type,
    _In_ XhciTrb_t*         trb)
{
    uint16_t commandIndex;
    oserr_t  oserr;

    oserr = XhciRingEnqueue(&controller->CommandRing, trb, &commandIndex);
    if (oserr != OS_EOK) {
        return oserr;
    }

    controller->Commands[commandIndex].Type = type;
    controller->Commands[commandIndex].Device = device;
    controller->Commands[commandIndex].Endpoint = endpoint;
    dma_mb();
    WRITE_VOLATILE(controller->Doorbells[0], 0);
    return OS_EOK;
}

/* Endpoint reset/cancellation recovery sequence.
 *
 * Both "clear halt" (XhciEndpointReset) and "cancel a transfer"
 * (XhciEndpointCancelTransfer) converge on the same Set TR Dequeue Pointer
 * completion handler: once the endpoint is confirmed Halted/Stopped, we
 * reset our software ring bookkeeping and rebuild it from the endpoint's
 * pending transfer list, dropping cancelled transfers and requeuing the
 * rest. Splicing hardware-owned TRBs in place is avoided entirely.
 */

static void
__ResetRingOwnership(
    _In_ XhciEndpoint_t* endpoint)
{
    memset(endpoint->TRBOwners, 0, sizeof(endpoint->TRBOwners));
    endpoint->TransferRing.EnqueueIndex = endpoint->TransferRing.DequeueIndex;
    endpoint->TransferRing.CycleState = endpoint->TransferRing.DequeueCycleState;
    endpoint->TransferRing.Used = 0;
    endpoint->CurrentTd = NULL;
}

static void
__FailAllPending(
    _In_ XhciEndpoint_t* endpoint)
{
    element_t* node = endpoint->Pending.head;

    while (node != NULL) {
        element_t*                node_next = node->next;
        XhciTransferDescriptor_t* descriptor = node->value;
        UsbManagerTransfer_t*     transfer = descriptor->Transfer;

        XhciEndpointDequeueTransfer(endpoint, descriptor);
        transfer->ResultCode = (descriptor->Flags & XHCI_TD_FLAG_CANCELLED) ?
                USBTRANSFERCODE_CANCELLED : USBTRANSFERCODE_INVALID;
        transfer->State = USBTRANSFER_STATE_CLEANUP;
        node = node_next;
    }
}

static oserr_t
__SubmitResetEndpoint(
    _In_ XhciController_t* controller,
    _In_ XhciEndpoint_t*   endpoint)
{
    XhciTrb_t trb = { 0 };
    oserr_t   oserr;

    trb.Control = XHCI_TRB_CONTROL_TYPE(XHCI_TRB_TYPE_RESET_ENDPOINT) |
            XHCI_TRB_CONTROL_EP_ID(endpoint->DeviceContextIndex) |
            XHCI_TRB_CONTROL_SLOT_ID(endpoint->SlotId);
    oserr = __SubmitCommand(controller, endpoint->Device, endpoint, XHCI_COMMAND_RESET_ENDPOINT, &trb);
    if (oserr != OS_EOK) {
        endpoint->State = XHCI_ENDPOINT_FAILED;
        return oserr;
    }
    endpoint->State = XHCI_ENDPOINT_RESET_PENDING;
    return OS_EOK;
}

static oserr_t
__SubmitStopEndpoint(
    _In_ XhciController_t* controller,
    _In_ XhciEndpoint_t*   endpoint)
{
    XhciTrb_t trb = { 0 };
    oserr_t   oserr;

    trb.Control = XHCI_TRB_CONTROL_TYPE(XHCI_TRB_TYPE_STOP_ENDPOINT) |
            XHCI_TRB_CONTROL_EP_ID(endpoint->DeviceContextIndex) |
            XHCI_TRB_CONTROL_SLOT_ID(endpoint->SlotId);
    oserr = __SubmitCommand(controller, endpoint->Device, endpoint, XHCI_COMMAND_STOP_ENDPOINT, &trb);
    if (oserr != OS_EOK) {
        endpoint->State = XHCI_ENDPOINT_FAILED;
        return oserr;
    }
    endpoint->State = XHCI_ENDPOINT_STOP_PENDING;
    return OS_EOK;
}

static oserr_t
__SubmitSetTRDequeuePointer(
    _In_ XhciController_t* controller,
    _In_ XhciEndpoint_t*   endpoint)
{
    XhciTrb_t trb = { 0 };
    reg64_t   dequeuePointer;
    oserr_t   oserr;

    dequeuePointer = endpoint->TransferRing.PhysicalBase +
            ((reg64_t)endpoint->TransferRing.DequeueIndex * sizeof(XhciTrb_t));
    if (endpoint->TransferRing.DequeueCycleState) {
        dequeuePointer |= XHCI_TRB_PARAMETER_DCS;
    }

    trb.Parameter = dequeuePointer;
    trb.Control = XHCI_TRB_CONTROL_TYPE(XHCI_TRB_TYPE_SET_TR_DEQUEUE) |
            XHCI_TRB_CONTROL_EP_ID(endpoint->DeviceContextIndex) |
            XHCI_TRB_CONTROL_SLOT_ID(endpoint->SlotId);
    oserr = __SubmitCommand(controller, endpoint->Device, endpoint, XHCI_COMMAND_SET_TR_DEQUEUE, &trb);
    if (oserr != OS_EOK) {
        endpoint->State = XHCI_ENDPOINT_FAILED;
        return oserr;
    }
    endpoint->State = XHCI_ENDPOINT_DEQUEUE_PENDING;
    return OS_EOK;
}

/* Rebuilds the ring's software ownership state and requeues every transfer
 * still pending on the endpoint, skipping (and finalizing) any that were
 * flagged cancelled. Called once Set TR Dequeue Pointer has completed. */
static void
__RebuildAndRequeuePending(
    _In_ XhciController_t* controller,
    _In_ XhciEndpoint_t*   endpoint)
{
    element_t* node = endpoint->Pending.head;

    __ResetRingOwnership(endpoint);

    while (node != NULL) {
        element_t*                node_next = node->next;
        XhciTransferDescriptor_t* descriptor = node->value;
        UsbManagerTransfer_t*     transfer = descriptor->Transfer;

        if (descriptor->Flags & XHCI_TD_FLAG_CANCELLED) {
            XhciEndpointDequeueTransfer(endpoint, descriptor);
            transfer->ResultCode = USBTRANSFERCODE_CANCELLED;
            transfer->State = USBTRANSFER_STATE_CLEANUP;
        } else if (XhciTransferSubmit(controller, endpoint, transfer) != OS_EOK) {
            /* The ring was just reset, so failure here means a genuine
             * resource problem rather than transient congestion. */
            XhciEndpointDequeueTransfer(endpoint, descriptor);
            transfer->ResultCode = USBTRANSFERCODE_INVALID;
            transfer->State = USBTRANSFER_STATE_CLEANUP;
        }
        node = node_next;
    }

    endpoint->State = XHCI_ENDPOINT_RUNNING;
}

oserr_t
XhciEndpointReset(
    _In_ XhciController_t* controller,
    _In_ XhciEndpoint_t*   endpoint)
{
    if (endpoint->Device == NULL) {
        return OS_EINVALPARAMS;
    }
    if (endpoint->State == XHCI_ENDPOINT_RESET_PENDING ||
        endpoint->State == XHCI_ENDPOINT_STOP_PENDING ||
        endpoint->State == XHCI_ENDPOINT_DEQUEUE_PENDING) {
        /* A recovery sequence is already in-flight for this endpoint. */
        return OS_EOK;
    }

    if (endpoint->State == XHCI_ENDPOINT_HALTED) {
        return __SubmitResetEndpoint(controller, endpoint);
    }

    /* Not halted on the hardware side; just realign the ring's dequeue
     * pointer to the current software position and requeue anything
     * pending, in case toggle/dequeue tracking has drifted. */
    return __SubmitSetTRDequeuePointer(controller, endpoint);
}

oserr_t
XhciEndpointCancelTransfer(
    _In_ XhciController_t*     controller,
    _In_ XhciEndpoint_t*       endpoint,
    _In_ UsbManagerTransfer_t* transfer)
{
    XhciTransferMarkCancelled(controller, transfer);

    if (endpoint->State == XHCI_ENDPOINT_STOP_PENDING ||
        endpoint->State == XHCI_ENDPOINT_DEQUEUE_PENDING) {
        /* Endpoint is already stopping/rebuilding for a previous
         * cancellation; the CANCELLED flag will be picked up once that
         * sequence finishes. */
        return OS_EOK;
    }

    return __SubmitStopEndpoint(controller, endpoint);
}

oserr_t
HCIEndpointReset(
    _In_ UsbManagerController_t* controllerBase,
    _In_ USBAddress_t*           address)
{
    XhciController_t* controller = (XhciController_t*)controllerBase;
    XhciEndpoint_t*   endpoint = XhciEndpointGet(controller, address);

    if (endpoint == NULL) {
        /* No endpoint state has been allocated yet, nothing to reset. */
        return OS_EOK;
    }
    return XhciEndpointReset(controller, endpoint);
}

static uint8_t
__EndpointTypeValue(
    _In_ enum USBTransferType      type,
    _In_ enum USBTransferDirection direction)
{
    bool in = direction == USBTRANSFER_DIRECTION_IN;
    switch (type) {
        case USBTRANSFER_TYPE_ISOC:      return in ? XHCI_EP_TYPE_ISOC_IN : XHCI_EP_TYPE_ISOC_OUT;
        case USBTRANSFER_TYPE_BULK:      return in ? XHCI_EP_TYPE_BULK_IN : XHCI_EP_TYPE_BULK_OUT;
        case USBTRANSFER_TYPE_INTERRUPT: return in ? XHCI_EP_TYPE_INT_IN  : XHCI_EP_TYPE_INT_OUT;
        case USBTRANSFER_TYPE_CONTROL:
        default:                         return XHCI_EP_TYPE_CONTROL;
    }
}

static uint8_t
__EndpointIntervalField(
    _In_ enum USBSpeed        speed,
    _In_ enum USBTransferType type,
    _In_ uint8_t              bInterval)
{
    uint32_t frames;
    uint8_t  exponent;

    if (type == USBTRANSFER_TYPE_CONTROL || type == USBTRANSFER_TYPE_BULK || bInterval == 0) {
        return 0;
    }
    if (speed == USBSPEED_HIGH || speed == USBSPEED_SUPER || speed == USBSPEED_SUPER_PLUS) {
        /* bInterval already encodes 2^(bInterval-1) * 125us units. */
        return (uint8_t)(bInterval - 1);
    }
    /* Low/full-speed intervals are expressed in 1ms (8 * 125us) frames; round
     * up to the nearest power-of-two multiple of 125us. */
    frames = (uint32_t)bInterval * 8;
    exponent = 0;
    while (((uint32_t)1 << exponent) < frames) {
        exponent++;
    }
    return exponent;
}

static void
__BuildEndpointContext(
    _In_  XhciEndpoint_t*       endpoint,
    _In_  UsbManagerTransfer_t* transfer,
    _Out_ reg32_t*              epContext)
{
    uint8_t typeValue = __EndpointTypeValue(transfer->Type, transfer->Direction);

    epContext[0] = XHCI_EP_CONTEXT_INTERVAL(
            __EndpointIntervalField(transfer->Speed, transfer->Type, transfer->TData.Periodic.Interval));
    epContext[1] = XHCI_EP_CONTEXT_CERR(3) | XHCI_EP_CONTEXT_TYPE(typeValue) |
            XHCI_EP_CONTEXT_MPS(transfer->MaxPacketSize);
    *((reg64_t*)&epContext[2]) = endpoint->TransferRing.PhysicalBase +
            (endpoint->TransferRing.DequeueIndex * sizeof(XhciTrb_t));
    if (endpoint->TransferRing.DequeueCycleState) {
        *((reg64_t*)&epContext[2]) |= XHCI_EP_CONTEXT_DCS;
    }
    epContext[4] = XHCI_EP_CONTEXT_AVG_TRB(transfer->MaxPacketSize ? transfer->MaxPacketSize : 8);
}

static void
__BuildConfigureEndpointContext(
    _In_ XhciController_t*     controller,
    _In_ XhciDevice_t*         device,
    _In_ XhciEndpoint_t*       endpoint,
    _In_ UsbManagerTransfer_t* transfer,
    _In_ bool                  reconfigure)
{
    reg32_t* inputControl;
    reg32_t* slotContext;
    reg32_t* endpointContext;
    uint8_t  dci = endpoint->DeviceContextIndex;

    memset(device->InputContext, 0, XHCI_INPUT_CONTEXT_ENTRIES * controller->ContextSize);
    inputControl = (reg32_t*)device->InputContext;
    slotContext = (reg32_t*)(device->InputContext + (XHCI_CONTEXT_SLOT * controller->ContextSize));
    endpointContext = (reg32_t*)(device->InputContext + ((dci + 1) * controller->ContextSize));

    if (dci > device->ContextEntries) {
        device->ContextEntries = dci;
    }

    inputControl[1] = XHCI_INPUT_ADD_SLOT | XHCI_INPUT_ADD_DCI(dci);
    if (reconfigure) {
        inputControl[0] = XHCI_INPUT_DROP_DCI(dci);
    }

    slotContext[0] = XHCI_SLOT_CONTEXT_ENTRIES(device->ContextEntries) |
            XHCI_SLOT_CONTEXT_SPEED(__SpeedId(transfer->Speed)) | device->RouteString;
    slotContext[1] = XHCI_SLOT_CONTEXT_PORT(device->RootPort);
    if (device->HubPortCount != 0) {
        slotContext[0] |= XHCI_SLOT_CONTEXT_HUB;
        slotContext[1] |= XHCI_SLOT_CONTEXT_NUM_PORTS(device->HubPortCount);
        slotContext[2] = XHCI_SLOT_CONTEXT_TT_THINK((device->HubCharacteristics >> 5) & 0x3);
        if (device->HubCharacteristics & 0x4) {
            slotContext[2] |= XHCI_SLOT_CONTEXT_MTT;
        }
    }

    __BuildEndpointContext(endpoint, transfer, endpointContext);
    dma_mb();

    endpoint->ConfiguredMaxPacketSize = transfer->MaxPacketSize;
    endpoint->ConfiguredInterval = transfer->TData.Periodic.Interval;
}

static void
__BuildAddressContext(
    _In_ XhciController_t*     controller,
    _In_ XhciDevice_t*         device,
    _In_ UsbManagerTransfer_t* transfer)
{
    reg32_t* inputControl;
    reg32_t* slotContext;
    reg32_t* endpointContext;

    memset(device->InputContext, 0, XHCI_INPUT_CONTEXT_ENTRIES * controller->ContextSize);
    inputControl = (reg32_t*)device->InputContext;
    slotContext = (reg32_t*)(device->InputContext + (XHCI_CONTEXT_SLOT * controller->ContextSize));
    endpointContext = (reg32_t*)(device->InputContext + (XHCI_CONTEXT_EP0 * controller->ContextSize));

    device->ContextEntries = 1;
    inputControl[1] = XHCI_INPUT_ADD_SLOT | XHCI_INPUT_ADD_EP0;
    slotContext[0] = XHCI_SLOT_CONTEXT_ENTRIES(device->ContextEntries) |
            XHCI_SLOT_CONTEXT_SPEED(__SpeedId(transfer->Speed)) | device->RouteString;
    slotContext[1] = XHCI_SLOT_CONTEXT_PORT(device->RootPort);
    if (device->Parent != NULL &&
        (transfer->Speed == USBSPEED_LOW || transfer->Speed == USBSPEED_FULL)) {
        XhciDevice_t* ttHub = device->Parent;
        while (ttHub->Parent != NULL && ttHub->Speed != USBSPEED_HIGH) {
            ttHub = ttHub->Parent;
        }
        if (ttHub->Speed == USBSPEED_HIGH) {
            slotContext[2] = XHCI_SLOT_CONTEXT_TT_HUB(ttHub->SlotId) |
                    XHCI_SLOT_CONTEXT_TT_PORT(device->PortAddress);
        }
    }

    endpointContext[1] = XHCI_EP_CONTEXT_CERR(3) | XHCI_EP_CONTEXT_TYPE_CONTROL |
            XHCI_EP_CONTEXT_MPS(transfer->MaxPacketSize);
    *((reg64_t*)&endpointContext[2]) = device->DefaultEndpoint->TransferRing.PhysicalBase +
            (device->DefaultEndpoint->TransferRing.DequeueIndex * sizeof(XhciTrb_t));
    if (device->DefaultEndpoint->TransferRing.DequeueCycleState) {
        *((reg64_t*)&endpointContext[2]) |= XHCI_EP_CONTEXT_DCS;
    }
    endpointContext[4] = XHCI_EP_CONTEXT_AVG_TRB(8);
    dma_mb();
}

static oserr_t
__SubmitEvaluateContext(
    _In_ XhciController_t* controller,
    _In_ XhciDevice_t*     device)
{
    XhciTrb_t trb = { 0 };
    trb.Parameter = device->InputContextDMATable.Entries[0].Address;
    trb.Control = XHCI_TRB_CONTROL_TYPE(XHCI_TRB_TYPE_EVALUATE_CONTEXT) |
            XHCI_TRB_CONTROL_SLOT_ID(device->SlotId);
    return __SubmitCommand(controller, device, NULL, XHCI_COMMAND_EVALUATE_CONTEXT, &trb);
}

oserr_t
HCIConfigureHub(
    _In_ UsbManagerController_t* controllerBase,
    _In_ uint8_t                 hubAddress,
    _In_ uint8_t                 portCount,
    _In_ uint16_t                characteristics)
{
    XhciController_t* controller = (XhciController_t*)controllerBase;
    XhciDevice_t* hub = __DeviceGetByUsbAddress(controller, hubAddress);
    reg32_t* slotContext;

    if (hub == NULL || hub->SlotId == 0) {
        return OS_ENOENT;
    }
    hub->HubPortCount = portCount;
    hub->HubCharacteristics = characteristics;
    memset(hub->InputContext, 0, XHCI_INPUT_CONTEXT_ENTRIES * controller->ContextSize);
    ((reg32_t*)hub->InputContext)[1] = XHCI_INPUT_ADD_SLOT;
    slotContext = (reg32_t*)(hub->InputContext + XHCI_CONTEXT_SLOT * controller->ContextSize);
        slotContext[0] = XHCI_SLOT_CONTEXT_ENTRIES(hub->ContextEntries) |
            XHCI_SLOT_CONTEXT_HUB;
    slotContext[1] = XHCI_SLOT_CONTEXT_PORT(hub->RootPort) |
            XHCI_SLOT_CONTEXT_NUM_PORTS(portCount);
    slotContext[2] = XHCI_SLOT_CONTEXT_TT_THINK((characteristics >> 5) & 0x3);
    /* The descriptor's compound-device bit is the only hub-level capability
     * available through this contract; preserve it as the MTT indication. */
    if (characteristics & 0x4) {
        slotContext[2] |= XHCI_SLOT_CONTEXT_MTT;
    }
    dma_mb();
    return __SubmitEvaluateContext(controller, hub);
}

static oserr_t
__SubmitAddressDevice(
    _In_ XhciController_t* controller,
    _In_ XhciDevice_t*     device,
    _In_ bool              blockSetAddress)
{
    XhciTrb_t trb = { 0 };

    trb.Parameter = device->InputContextDMATable.Entries[0].Address;
    trb.Control = XHCI_TRB_CONTROL_TYPE(XHCI_TRB_TYPE_ADDRESS_DEVICE) |
            ((reg32_t)device->SlotId << 24);
    if (blockSetAddress) {
        trb.Control |= XHCI_TRB_ADDRESS_BSR;
    }
    return __SubmitCommand(controller, device, NULL, XHCI_COMMAND_ADDRESS_DEVICE, &trb);
}

oserr_t
XhciDeviceConfigureEndpoint(
    _In_ XhciController_t*     controller,
    _In_ XhciDevice_t*         device,
    _In_ XhciEndpoint_t*       endpoint,
    _In_ UsbManagerTransfer_t* transfer)
{
    XhciTrb_t trb = { 0 };
    bool      reconfigure = endpoint->State != XHCI_ENDPOINT_UNCONFIGURED;
    oserr_t   oserr;

    __BuildConfigureEndpointContext(controller, device, endpoint, transfer, reconfigure);

    trb.Parameter = device->InputContextDMATable.Entries[0].Address;
    trb.Control = XHCI_TRB_CONTROL_TYPE(XHCI_TRB_TYPE_CONFIGURE_ENDPOINT) |
            ((reg32_t)device->SlotId << 24);

    oserr = __SubmitCommand(controller, device, endpoint, XHCI_COMMAND_CONFIGURE_ENDPOINT, &trb);
    if (oserr != OS_EOK) {
        endpoint->State = XHCI_ENDPOINT_FAILED;
        return oserr;
    }
    endpoint->State = XHCI_ENDPOINT_CONFIGURE_PENDING;
    return OS_EINCOMPLETE;
}

oserr_t
XhciDeviceEnsure(
    _In_  XhciController_t*     controller,
    _In_  UsbManagerTransfer_t* transfer,
    _In_  XhciEndpoint_t*       endpoint,
    _Out_ XhciDevice_t**        deviceOut)
{
    XhciDevice_t* device = __DeviceGet(controller, &transfer->Address);
    XhciTrb_t     trb = { 0 };
    oserr_t       oserr;

    if (device != NULL) {
        endpoint->Device = device;
        endpoint->SlotId = device->SlotId;
        *deviceOut = device;
        if (device->State == XHCI_DEVICE_FAILED) {
            return OS_EUNKNOWN;
        }
        return (device->State == XHCI_DEVICE_DEFAULT || device->State == XHCI_DEVICE_ADDRESSED) ?
                OS_EOK : OS_EINCOMPLETE;
    }

    device = calloc(1, sizeof(XhciDevice_t));
    if (device == NULL) {
        return OS_EOOM;
    }

    device->HubAddress = transfer->Address.HubAddress;
    device->PortAddress = transfer->Address.PortAddress;
    device->Speed = transfer->Speed;
    device->Parent = __DeviceGetByUsbAddress(controller, device->HubAddress);
    if (device->Parent != NULL) {
        device->RootPort = device->Parent->RootPort;
        device->Depth = device->Parent->Depth + 1;
        if (device->Depth >= 5) {
            free(device);
            return OS_EINVALPARAMS;
        }
        device->RouteString = device->Parent->RouteString |
                ((uint32_t)device->PortAddress << (device->Depth * 4));
    } else {
        if (device->HubAddress != 0 || device->PortAddress > 14) {
            free(device);
            return OS_EINVALPARAMS;
        }
        device->RootPort = device->PortAddress + 1;
        device->RouteString = device->RootPort;
    }
    device->DefaultEndpoint = endpoint;
    device->InitTransfer = transfer;
    device->State = XHCI_DEVICE_ENABLE_PENDING;
    endpoint->Device = device;
    ELEMENT_INIT(&device->Header, transfer->Address.PortAddress, device);

    oserr = __AllocateContext(
            XHCI_INPUT_CONTEXT_ENTRIES * controller->ContextSize,
            &device->InputContextDMA,
            &device->InputContextDMATable,
            &device->InputContext
    );
    if (oserr == OS_EOK) {
        oserr = __AllocateContext(
                XHCI_DEVICE_CONTEXT_ENTRIES * controller->ContextSize,
                &device->DeviceContextDMA,
                &device->DeviceContextDMATable,
                &device->DeviceContext
        );
    }
    if (oserr != OS_EOK) {
        __FreeContext(&device->InputContextDMA, &device->InputContextDMATable);
        free(device);
        return oserr;
    }

    if (list_append(&controller->Devices, &device->Header) != 0) {
        __FreeContext(&device->DeviceContextDMA, &device->DeviceContextDMATable);
        __FreeContext(&device->InputContextDMA, &device->InputContextDMATable);
        free(device);
        return OS_EUNKNOWN;
    }

    trb.Control = XHCI_TRB_CONTROL_TYPE(XHCI_TRB_TYPE_ENABLE_SLOT);
    oserr = __SubmitCommand(controller, device, NULL, XHCI_COMMAND_ENABLE_SLOT, &trb);
    if (oserr != OS_EOK) {
        device->State = XHCI_DEVICE_FAILED;
        device->InitTransfer = NULL;
        return oserr;
    }

    *deviceOut = device;
    return OS_EINCOMPLETE;
}

oserr_t
XhciDeviceSetAddress(
    _In_ XhciController_t*     controller,
    _In_ XhciDevice_t*         device,
    _In_ UsbManagerTransfer_t* transfer,
    _In_ uint8_t               address)
{
    oserr_t oserr;

    if (device->State == XHCI_DEVICE_ADDRESS_PENDING) {
        return OS_EINCOMPLETE;
    }
    if (device->State != XHCI_DEVICE_DEFAULT) {
        return OS_EINVALPARAMS;
    }

    device->AddressTransfer = transfer;
    device->UsbAddress = address;
    device->State = XHCI_DEVICE_ADDRESS_PENDING;
    __BuildAddressContext(controller, device, transfer);
    oserr = __SubmitAddressDevice(controller, device, false);
    if (oserr != OS_EOK) {
        device->State = XHCI_DEVICE_FAILED;
        device->AddressTransfer = NULL;
    }
    return oserr == OS_EOK ? OS_EINCOMPLETE : oserr;
}

void
XhciCommandHandleCompletion(
    _In_ XhciController_t* controller,
    _In_ XhciTrb_t*        eventTrb)
{
    uintptr_t      commandAddress = (uintptr_t)eventTrb->Parameter;
    uintptr_t      commandOffset;
    uint16_t       commandIndex;
    XhciCommand_t* command;
    XhciDevice_t*  device;

    if (commandAddress < controller->CommandRing.PhysicalBase) {
        return;
    }
    commandOffset = commandAddress - controller->CommandRing.PhysicalBase;
    if ((commandOffset % sizeof(XhciTrb_t)) != 0) {
        return;
    }
    commandIndex = (uint16_t)(commandOffset / sizeof(XhciTrb_t));
    if (commandIndex >= XHCI_COMMAND_RING_ENTRIES - 1) {
        return;
    }

    command = &controller->Commands[commandIndex];
    device = command->Device;
    if (command->Type == XHCI_COMMAND_NONE || device == NULL) {
        return;
    }

    if (XHCI_TRB_COMPLETION_CODE(eventTrb->Status) != XHCI_TRB_COMPLETION_SUCCESS) {
        ERROR("XHCI-Failure: command %u failed with completion code %u",
                command->Type, XHCI_TRB_COMPLETION_CODE(eventTrb->Status));
        if (command->Type == XHCI_COMMAND_CONFIGURE_ENDPOINT) {
            /* Endpoint-scoped failure; other endpoints/EP0 on the device are unaffected. */
            if (command->Endpoint != NULL) {
                command->Endpoint->State = XHCI_ENDPOINT_FAILED;
            }
        } else if (command->Type == XHCI_COMMAND_RESET_ENDPOINT ||
                   command->Type == XHCI_COMMAND_STOP_ENDPOINT ||
                   command->Type == XHCI_COMMAND_SET_TR_DEQUEUE) {
            /* Endpoint-scoped failure; fail every transfer still pending on
             * this endpoint rather than leaving them stuck against a ring
             * whose ownership state we can no longer trust. */
            if (command->Endpoint != NULL) {
                command->Endpoint->State = XHCI_ENDPOINT_FAILED;
                __FailAllPending(command->Endpoint);
            }
        } else {
            device->State = XHCI_DEVICE_FAILED;
            if (device->AddressTransfer != NULL) {
                device->AddressTransfer->ResultCode = USBTRANSFERCODE_INVALID;
                device->AddressTransfer->State = USBTRANSFER_STATE_CLEANUP;
                device->AddressTransfer = NULL;
            } else if (device->InitTransfer != NULL) {
                device->InitTransfer->ResultCode = USBTRANSFERCODE_INVALID;
                device->InitTransfer->State = USBTRANSFER_STATE_CLEANUP;
                device->InitTransfer = NULL;
            }
        }
    } else if (command->Type == XHCI_COMMAND_CONFIGURE_ENDPOINT) {
        if (command->Endpoint != NULL) {
            command->Endpoint->State = XHCI_ENDPOINT_RUNNING;
        }
    } else if (command->Type == XHCI_COMMAND_EVALUATE_CONTEXT) {
        /* The hub context update is complete. */
    } else if (command->Type == XHCI_COMMAND_RESET_ENDPOINT || command->Type == XHCI_COMMAND_STOP_ENDPOINT) {
        /* Endpoint is now Stopped; realign the ring dequeue pointer before
         * rebuilding software state and requeuing pending transfers. */
        if (command->Endpoint != NULL && __SubmitSetTRDequeuePointer(controller, command->Endpoint) != OS_EOK) {
            __FailAllPending(command->Endpoint);
        }
    } else if (command->Type == XHCI_COMMAND_SET_TR_DEQUEUE) {
        if (command->Endpoint != NULL) {
            __RebuildAndRequeuePending(controller, command->Endpoint);
        }
    } else if (command->Type == XHCI_COMMAND_ENABLE_SLOT) {
        device->SlotId = XHCI_TRB_SLOT_ID(eventTrb->Control);
        if (device->SlotId == 0 || device->SlotId > controller->SlotCount) {
            device->State = XHCI_DEVICE_FAILED;
            device->InitTransfer->ResultCode = USBTRANSFERCODE_INVALID;
            device->InitTransfer->State = USBTRANSFER_STATE_CLEANUP;
            device->InitTransfer = NULL;
            goto commandHandled;
        }
        device->DefaultEndpoint->SlotId = device->SlotId;
        controller->DCBaa[device->SlotId] = device->DeviceContextDMATable.Entries[0].Address;
        __BuildAddressContext(controller, device, device->InitTransfer);
        device->State = XHCI_DEVICE_DEFAULT_PENDING;
        if (__SubmitAddressDevice(controller, device, true) != OS_EOK) {
            device->State = XHCI_DEVICE_FAILED;
            device->InitTransfer->ResultCode = USBTRANSFERCODE_INVALID;
            device->InitTransfer->State = USBTRANSFER_STATE_CLEANUP;
            device->InitTransfer = NULL;
        }
    } else if (command->Type == XHCI_COMMAND_ADDRESS_DEVICE) {
        if (device->State == XHCI_DEVICE_DEFAULT_PENDING) {
            device->State = XHCI_DEVICE_DEFAULT;
            device->InitTransfer = NULL;
        } else if (device->State == XHCI_DEVICE_ADDRESS_PENDING) {
            device->State = XHCI_DEVICE_ADDRESSED;
            XhciTransferCompleteSoftware(controller, device->AddressTransfer);
            device->AddressTransfer = NULL;
        }
    }

commandHandled:
    memset(command, 0, sizeof(XhciCommand_t));
    XhciRingRelease(&controller->CommandRing, 1);
}

static void
__DestroyDevice(
    _In_ element_t* item,
    _In_ void*      context)
{
    XhciDevice_t* device = item->value;
    _CRT_UNUSED(context);

    __FreeContext(&device->DeviceContextDMA, &device->DeviceContextDMATable);
    __FreeContext(&device->InputContextDMA, &device->InputContextDMATable);
    free(device);
}

void
XhciDeviceDestroyAll(
    _In_ XhciController_t* controller)
{
    list_clear(&controller->Devices, __DestroyDevice, NULL);
}
