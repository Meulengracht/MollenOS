/**
 * Copyright 2026, Philip Meulengracht
 */

#include <ddk/barrier.h>
#include <ddk/io.h>
#include <ddk/utils.h>
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

static oserr_t
__DestroyDevice(
    _In_ XhciController_t* controller,
    _In_ XhciDevice_t*     device);

static void
__DetachMaybeDisable(
    _In_ XhciController_t* controller,
    _In_ XhciDevice_t*     device);

static void
__FinishEndpointRecovery(
    _In_ XhciController_t* controller,
    _In_ XhciEndpoint_t*   endpoint)
{
    XhciDevice_t* device = endpoint->Device;

    UsbManagerWakeTransfers(&controller->Base);
    if (device != NULL && device->State == XHCI_DEVICE_DETACHING) {
        if (device->DetachPending != 0xFF && device->DetachPending > 0) {
            device->DetachPending--;
        }
        __DetachMaybeDisable(controller, device);
    }
}

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

    if (controller->State != XHCI_CONTROLLER_RUNNING ||
        device->State == XHCI_DEVICE_RELEASE_PENDING) {
        return OS_EBUSY;
    }
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
    XhciRing_t* ring = &endpoint->TransferRing;
    uint16_t    i;
    uint32_t    invalidCycle;

    // The stopped ring may contain cancelled TRBs beyond the rebuilt tail.
    // Invalidate every old entry relative to the hardware's dequeue cycle,
    // including the portion it would reach after wrapping through the link.
    for (i = 0; i < ring->TrbCount; i++) {
        invalidCycle = i < ring->DequeueIndex ? ring->DequeueCycleState : !ring->DequeueCycleState;
        ring->Trbs[i].Parameter = 0;
        ring->Trbs[i].Status = 0;
        ring->Trbs[i].Control = invalidCycle ? XHCI_TRB_CONTROL_CYCLE : 0;
        if (i == ring->TrbCount - 1) {
            ring->Trbs[i].Parameter = ring->DeviceBase;
            ring->Trbs[i].Control |= XHCI_TRB_CONTROL_TYPE(XHCI_TRB_TYPE_LINK) |
                XHCI_TRB_CONTROL_TOGGLE_CYCLE;
        }
    }
    memset(endpoint->TRBOwners, 0, sizeof(endpoint->TRBOwners));
    endpoint->TransferRing.EnqueueIndex = endpoint->TransferRing.DequeueIndex;
    endpoint->TransferRing.CycleState = endpoint->TransferRing.DequeueCycleState;
    endpoint->TransferRing.Used = 0;
    endpoint->CurrentTd = NULL;
}

static oserr_t
__CompletePendingDma(
    _In_ XhciController_t* controller,
    _In_ XhciEndpoint_t*   endpoint)
{
    XhciTransferDescriptor_t* descriptor;
    UsbManagerTransfer_t*     transfer;
    oserr_t                   status;

    // Call only after Stop/Reset Endpoint or Disable Slot has completed.
    // Recovery owns these transfers until every mapping is CPU-owned; neither
    // stale events nor the common completion queue may rearm a partial rebuild.
    __ResetRingOwnership(endpoint);
    foreach(node, &endpoint->Pending) {
        descriptor = node->value;
        transfer = descriptor->Transfer;
        transfer->Dma.HardwareActive = false;
        UsbManagerForgetTransferWork(transfer);
    }
    foreach(node, &endpoint->Pending) {
        descriptor = node->value;
        status = USBTransferDmaComplete(descriptor->Transfer, 0);
        if (status != OS_EOK) {
            controller->RecoveryPending = true;
            return status;
        }
    }
    return OS_EOK;
}

static void
__RetireTransfer(
    _In_ XhciController_t*         controller,
    _In_ XhciTransferDescriptor_t* descriptor)
{
    UsbManagerTransfer_t* transfer = descriptor->Transfer;

    XhciEndpointDequeueTransfer(descriptor->Endpoint, descriptor);
    descriptor->Endpoint = NULL;
    transfer->ResultCode = (descriptor->Flags & XHCI_TD_FLAG_CANCELLED) ?
        USBTRANSFERCODE_CANCELLED : USBTRANSFERCODE_INVALID;
    transfer->State = USBTRANSFER_STATE_CLEANUP;
    UsbManagerQueueTransferWork(&controller->Base, transfer);
}

static oserr_t
__FailAllPending(
    _In_ XhciController_t* controller,
    _In_ XhciEndpoint_t*   endpoint)
{
    oserr_t status;

    endpoint->State = XHCI_ENDPOINT_RETIRE_PENDING;
    status = __CompletePendingDma(controller, endpoint);
    if (status != OS_EOK) {
        return status;
    }
    while (endpoint->Pending.head != NULL) {
        __RetireTransfer(controller, endpoint->Pending.head->value);
    }
    endpoint->State = XHCI_ENDPOINT_FAILED;
    __FinishEndpointRecovery(controller, endpoint);
    return OS_EOK;
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

    dequeuePointer = endpoint->TransferRing.DeviceBase +
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

static oserr_t
__SubmitDisableSlot(
    _In_ XhciController_t* controller,
    _In_ XhciDevice_t*     device)
{
    XhciTrb_t trb = { 0 };

    trb.Control = XHCI_TRB_CONTROL_TYPE(XHCI_TRB_TYPE_DISABLE_SLOT) |
            XHCI_TRB_CONTROL_SLOT_ID(device->SlotId);
    return __SubmitCommand(controller, device, NULL, XHCI_COMMAND_DISABLE_SLOT, &trb);
}

static void
__DetachComplete(
    _In_ XhciController_t* controller,
    _In_ XhciDevice_t*     device)
{
    XhciEndpoint_t* endpoint;

    // Disable Slot has completed. Return queued payloads to CPU ownership
    // before freeing endpoint rings.
    foreach(node, &controller->XhciEndpoints) {
        endpoint = node->value;
        if (endpoint->Device == device) {
            if (__FailAllPending(controller, endpoint) != OS_EOK) {
                return;
            }
        }
    }
    if (XhciEndpointDestroyDevice(controller, device) != OS_EOK) {
        controller->RecoveryPending = true;
        return;
    }
    if (__DestroyDevice(controller, device) != OS_EOK) {
        controller->RecoveryPending = true;
    }
}

static void
__DetachMaybeDisable(
    _In_ XhciController_t* controller,
    _In_ XhciDevice_t*     device)
{
    if (device->DetachPending == 0) {
        device->DetachPending = 0xFF;
        if (__SubmitDisableSlot(controller, device) != OS_EOK) {
            device->DetachPending = 0;
            controller->RecoveryPending = true;
        }
    }
}

/* Rebuilds the ring's software ownership state and requeues every transfer
 * still pending on the endpoint, skipping (and finalizing) any that were
 * flagged cancelled. Called once Set TR Dequeue Pointer has completed. */
static void
__RebuildAndRequeuePending(
    _In_ XhciController_t* controller,
    _In_ XhciEndpoint_t*   endpoint)
{
    element_t*                node = endpoint->Pending.head;
    element_t*                next;
    XhciTransferDescriptor_t* descriptor;
    UsbManagerTransfer_t*     transfer;
    oserr_t                   status;

    endpoint->State = XHCI_ENDPOINT_REBUILD_PENDING;
    if (__CompletePendingDma(controller, endpoint) != OS_EOK) {
        return;
    }

    while (node != NULL) {
        next = node->next;
        descriptor = node->value;
        transfer = descriptor->Transfer;

        if (descriptor->Flags & XHCI_TD_FLAG_CANCELLED) {
            __RetireTransfer(controller, descriptor);
        } else {
            if (transfer->State == USBTRANSFER_STATE_STALLED) {
                __Transfer_Reset(transfer);
            }
            status = XhciTransferSubmit(controller, endpoint, transfer);
            if (status != OS_EOK) {
                // Hardware is stopped and this TD was not published.
                __RetireTransfer(controller, descriptor);
            }
        }
        node = next;
    }

    endpoint->State = XHCI_ENDPOINT_RUNNING;
    __FinishEndpointRecovery(controller, endpoint);
}

void
HciTimerCallback(
    _In_ UsbManagerController_t* baseController)
{
    XhciController_t* controller = (XhciController_t*)baseController;
    XhciEndpoint_t*   endpoint;
    XhciDevice_t*     device;
    element_t*       node;
    element_t*       next;

    if (controller->State != XHCI_CONTROLLER_RUNNING) {
        return;
    }
    // The shared monitor provides bounded retries even on a quiet controller.
    // Healthy controllers do no list scans, allocation or DMA work here.
    if (controller->RecoveryPending) {
        controller->RecoveryPending = false;
        foreach(item, &controller->XhciEndpoints) {
            endpoint = item->value;
            if (endpoint->Device != NULL && endpoint->Device->State == XHCI_DEVICE_RELEASE_PENDING) {
                continue;
            }
            if (endpoint->State == XHCI_ENDPOINT_REBUILD_PENDING) {
                __RebuildAndRequeuePending(controller, endpoint);
            } else if (endpoint->State == XHCI_ENDPOINT_RETIRE_PENDING) {
                __FailAllPending(controller, endpoint);
            }
        }
        node = controller->Devices.head;
        while (node != NULL) {
            next = node->next;
            device = node->value;
            if (device->State == XHCI_DEVICE_RELEASE_PENDING) {
                __DetachComplete(controller, device);
            } else if (device->State == XHCI_DEVICE_DETACHING) {
                __DetachMaybeDisable(controller, device);
            }
            node = next;
        }
    }
    if (baseController->ReadyTransfers.head != NULL) {
        UsbManagerProcessTransfers(baseController);
    }
}

oserr_t
XhciEndpointReset(
    _In_ XhciController_t* controller,
    _In_ XhciEndpoint_t*   endpoint)
{
    if (endpoint->Device == NULL) {
        return OS_EINVALPARAMS;
    }
    if (endpoint->Device->State == XHCI_DEVICE_DETACHING ||
        endpoint->Device->State == XHCI_DEVICE_RELEASE_PENDING) {
        return OS_EBUSY;
    }
    if (endpoint->State == XHCI_ENDPOINT_RESET_PENDING ||
        endpoint->State == XHCI_ENDPOINT_STOP_PENDING ||
        endpoint->State == XHCI_ENDPOINT_DEQUEUE_PENDING ||
        endpoint->State == XHCI_ENDPOINT_REBUILD_PENDING ||
        endpoint->State == XHCI_ENDPOINT_RETIRE_PENDING) {
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
        endpoint->State == XHCI_ENDPOINT_DEQUEUE_PENDING ||
        endpoint->State == XHCI_ENDPOINT_REBUILD_PENDING ||
        endpoint->State == XHCI_ENDPOINT_RETIRE_PENDING) {
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
    *((reg64_t*)&epContext[2]) = endpoint->TransferRing.DeviceBase +
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
    XhciDevice_t* ttHub;
    XhciDevice_t* ttChild;

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
        ttHub = device->Parent;
        ttChild = device;
        while (ttHub->Parent != NULL && ttHub->Speed != USBSPEED_HIGH) {
            ttChild = ttHub;
            ttHub = ttHub->Parent;
        }
        if (ttHub->Speed == USBSPEED_HIGH) {
            // The translator belongs to the upstream high-speed hub. Its port is
            // the next branch toward this device, not necessarily the leaf port.
            slotContext[2] = XHCI_SLOT_CONTEXT_TT_HUB(ttHub->SlotId) |
                    XHCI_SLOT_CONTEXT_TT_PORT(ttChild->PortAddress);
        }
    }

    endpointContext[1] = XHCI_EP_CONTEXT_CERR(3) | XHCI_EP_CONTEXT_TYPE_CONTROL |
            XHCI_EP_CONTEXT_MPS(transfer->MaxPacketSize);
    *((reg64_t*)&endpointContext[2]) = device->DefaultEndpoint->TransferRing.DeviceBase +
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
    trb.Parameter = device->InputContextDeviceAddress;
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

    trb.Parameter = device->InputContextDeviceAddress;
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

    trb.Parameter = device->InputContextDeviceAddress;
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
    unsigned int routePort;

    if (device != NULL) {
        endpoint->Device = device;
        endpoint->SlotId = device->SlotId;
        device->Endpoints[endpoint->DeviceContextIndex] = endpoint;
        *deviceOut = device;
        if (device->State == XHCI_DEVICE_DETACHING || device->State == XHCI_DEVICE_RELEASE_PENDING) {
            return OS_EBUSY;
        }
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
    // Address zero identifies the root hub, even while another root device
    // has not yet received its USB address.
    if (device->HubAddress != 0) {
        device->Parent = __DeviceGetByUsbAddress(controller, device->HubAddress);
    }
    if (device->Parent != NULL) {
        if (device->PortAddress == 0) {
            free(device);
            return OS_EINVALPARAMS;
        }
        device->RootPort = device->Parent->RootPort;
        device->Depth = device->Parent->Depth + 1;
        if (device->Depth > 5) {
            free(device);
            return OS_EINVALPARAMS;
        }
        // Each tier consumes one nibble, starting with the least significant.
        // External hubs already report one-based ports; xHCI saturates above 15.
        routePort = device->PortAddress;
        if (routePort > 15) {
            routePort = 15;
        }
        device->RouteString = device->Parent->RouteString |
                ((uint32_t)routePort << ((device->Depth - 1) * 4));
    } else {
        if (device->HubAddress != 0 || device->PortAddress >= controller->Base.PortCount) {
            free(device);
            return OS_EINVALPARAMS;
        }
        device->RootPort = device->PortAddress + 1;
        // The root port has its own slot-context field. Route nibbles only
        // describe downstream hub ports; a directly attached device has none.
        device->RouteString = 0;
    }
    device->DefaultEndpoint = endpoint;
    device->InitTransfer = transfer;
    device->State = XHCI_DEVICE_ENABLE_PENDING;
    endpoint->Device = device;
    device->Endpoints[endpoint->DeviceContextIndex] = endpoint;
    ELEMENT_INIT(&device->Header, (uintptr_t)transfer->Address.PortAddress, device);

    list_append(&controller->Devices, &device->Header);
    oserr = XhciDmaAllocate(controller->Base.DmaContext,
            XHCI_INPUT_CONTEXT_ENTRIES * controller->ContextSize,
            &device->InputContextDMA,
            &device->InputContextDeviceAddress,
            (void**)&device->InputContext
    );
    if (oserr == OS_EOK) {
        oserr = XhciDmaAllocate(controller->Base.DmaContext,
                XHCI_DEVICE_CONTEXT_ENTRIES * controller->ContextSize,
                &device->DeviceContextDMA,
                &device->DeviceContextDeviceAddress,
                (void**)&device->DeviceContext
        );
    }
    if (oserr != OS_EOK) {
        device->State = XHCI_DEVICE_FAILED;
        device->InitTransfer = NULL;
        return oserr;
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

oserr_t
HCIDeviceDetach(
    _In_ UsbManagerController_t* controllerBase,
    _In_ USBAddress_t*           address)
{
    XhciController_t* controller = (XhciController_t*)controllerBase;
    XhciDevice_t* device = __DeviceGet(controller, address);
    USBPortDescriptor_t port;
    bool portUsable;

    if (device == NULL) {
        return OS_ENOENT;
    }
    if (device->State == XHCI_DEVICE_DETACHING || device->State == XHCI_DEVICE_RELEASE_PENDING) {
        controller->RecoveryPending = true;
        return OS_EOK;
    }

    device->State = XHCI_DEVICE_DETACHING;
    portUsable = false;
    if (device->RootPort != 0) {
        HCIPortStatus(&controller->Base, device->RootPort - 1, &port);
        portUsable = port.Connected && port.Enabled;
    }

    foreach(node, &controller->XhciEndpoints) {
        XhciEndpoint_t* endpoint = node->value;
        if (endpoint->Device != device) {
            continue;
        }
        foreach(transferNode, &endpoint->Pending) {
            XhciTransferDescriptor_t* descriptor = transferNode->value;
            descriptor->Flags |= XHCI_TD_FLAG_CANCELLED;
        }
        switch (endpoint->State) {
            case XHCI_ENDPOINT_RESET_PENDING:
            case XHCI_ENDPOINT_STOP_PENDING:
            case XHCI_ENDPOINT_DEQUEUE_PENDING:
            case XHCI_ENDPOINT_REBUILD_PENDING:
            case XHCI_ENDPOINT_RETIRE_PENDING:
                // The existing recovery command still uses this device without owning it.
                device->DetachPending++;
                continue;
            default:
                break;
        }
        if (portUsable && (endpoint->State == XHCI_ENDPOINT_RUNNING ||
            endpoint->State == XHCI_ENDPOINT_HALTED)) {
            if (__SubmitStopEndpoint(controller, endpoint) == OS_EOK) {
                device->DetachPending++;
            }
        }
    }

    __DetachMaybeDisable(controller, device);
    return OS_EOK;
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

    if (commandAddress < controller->CommandRing.DeviceBase) {
        return;
    }
    commandOffset = commandAddress - controller->CommandRing.DeviceBase;
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
        if (command->Type == XHCI_COMMAND_DISABLE_SLOT) {
            device->DetachPending = 0;
            controller->RecoveryPending = true;
            goto commandHandled;
        } else if (command->Type == XHCI_COMMAND_CONFIGURE_ENDPOINT) {
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
                // Keep DMA mappings pinned until a successful disable or controller halt.
                if (device->State == XHCI_DEVICE_DETACHING) {
                    if (device->DetachPending != 0xFF && device->DetachPending > 0) {
                        device->DetachPending--;
                    }
                    __DetachMaybeDisable(controller, device);
                }
            }
        } else {
            device->State = XHCI_DEVICE_FAILED;
            if (device->AddressTransfer != NULL) {
                device->AddressTransfer->ResultCode = USBTRANSFERCODE_INVALID;
                device->AddressTransfer->State = USBTRANSFER_STATE_CLEANUP;
                UsbManagerQueueTransferWork(&controller->Base, device->AddressTransfer);
                device->AddressTransfer = NULL;
            } else if (device->InitTransfer != NULL) {
                device->InitTransfer->ResultCode = USBTRANSFERCODE_INVALID;
                device->InitTransfer->State = USBTRANSFER_STATE_CLEANUP;
                UsbManagerQueueTransferWork(&controller->Base, device->InitTransfer);
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
            __FailAllPending(controller, command->Endpoint);
        }
    } else if (command->Type == XHCI_COMMAND_SET_TR_DEQUEUE) {
        if (command->Endpoint != NULL) {
            __RebuildAndRequeuePending(controller, command->Endpoint);
        }
    } else if (command->Type == XHCI_COMMAND_DISABLE_SLOT) {
        device->State = XHCI_DEVICE_RELEASE_PENDING;
        // Clear hardware identity once, before any retry can overlap slot reuse.
        if (controller->Slots[device->SlotId] == device) {
            controller->DCBaa[device->SlotId] = 0;
            controller->Slots[device->SlotId] = NULL;
        }
        __DetachComplete(controller, device);
    } else if (command->Type == XHCI_COMMAND_ENABLE_SLOT) {
        device->SlotId = XHCI_TRB_SLOT_ID(eventTrb->Control);
        if (device->SlotId == 0 || device->SlotId > controller->SlotCount ||
            controller->Slots[device->SlotId] != NULL) {
            device->State = XHCI_DEVICE_FAILED;
            device->InitTransfer->ResultCode = USBTRANSFERCODE_INVALID;
            device->InitTransfer->State = USBTRANSFER_STATE_CLEANUP;
            UsbManagerQueueTransferWork(&controller->Base, device->InitTransfer);
            device->InitTransfer = NULL;
            goto commandHandled;
        }
        controller->Slots[device->SlotId] = device;
        device->DefaultEndpoint->SlotId = device->SlotId;
        controller->DCBaa[device->SlotId] = device->DeviceContextDeviceAddress;
        __BuildAddressContext(controller, device, device->InitTransfer);
        device->State = XHCI_DEVICE_DEFAULT_PENDING;
        if (__SubmitAddressDevice(controller, device, true) != OS_EOK) {
            device->State = XHCI_DEVICE_FAILED;
            device->InitTransfer->ResultCode = USBTRANSFERCODE_INVALID;
            device->InitTransfer->State = USBTRANSFER_STATE_CLEANUP;
            UsbManagerQueueTransferWork(&controller->Base, device->InitTransfer);
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
    UsbManagerWakeTransfers(&controller->Base);
}

static oserr_t
__DestroyDevice(
    _In_ XhciController_t* controller,
    _In_ XhciDevice_t* device)
{
    oserr_t status;

    status = DMAFree(&device->DeviceContextDMA);
    if (status != OS_EOK) {
        return status;
    }
    status = DMAFree(&device->InputContextDMA);
    if (status != OS_EOK) {
        return status;
    }
    if (controller->Slots[device->SlotId] == device) {
        controller->Slots[device->SlotId] = NULL;
    }
    list_remove(&controller->Devices, &device->Header);
    free(device);
    return OS_EOK;
}

oserr_t
XhciDeviceDestroyAll(
    _In_ XhciController_t* controller)
{
    oserr_t status;

    while (controller->Devices.head != NULL) {
        status = __DestroyDevice(controller, controller->Devices.head->value);
        if (status != OS_EOK) {
            return status;
        }
    }
    return OS_EOK;
}
