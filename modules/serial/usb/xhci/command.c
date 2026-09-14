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
#define XHCI_SLOT_CONTEXT_ENTRIES(n) (((n) & 0x1F) << 27)
#define XHCI_SLOT_CONTEXT_SPEED(n)   (((n) & 0xF) << 20)
#define XHCI_SLOT_CONTEXT_PORT(n)    (((n) & 0xFF) << 16)
#define XHCI_EP_CONTEXT_CERR(n)      (((n) & 0x3) << 1)
#define XHCI_EP_CONTEXT_TYPE_CONTROL (4 << 3)
#define XHCI_EP_CONTEXT_MPS(n)       (((n) & 0xFFFF) << 16)
#define XHCI_EP_CONTEXT_AVG_TRB(n)   ((n) & 0xFFFF)
#define XHCI_EP_CONTEXT_DCS          (1 << 0)

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

static oserr_t
__SubmitCommand(
    _In_ XhciController_t*  controller,
    _In_ XhciDevice_t*      device,
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
    dma_mb();
    WRITE_VOLATILE(controller->Doorbells[0], 0);
    return OS_EOK;
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

    inputControl[1] = XHCI_INPUT_ADD_SLOT | XHCI_INPUT_ADD_EP0;
    slotContext[0] = XHCI_SLOT_CONTEXT_ENTRIES(1) | XHCI_SLOT_CONTEXT_SPEED(__SpeedId(transfer->Speed));
    slotContext[1] = XHCI_SLOT_CONTEXT_PORT(transfer->Address.PortAddress + 1);

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
    return __SubmitCommand(controller, device, XHCI_COMMAND_ADDRESS_DEVICE, &trb);
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
    device->DefaultEndpoint = endpoint;
    device->BootstrapTransfer = transfer;
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
    oserr = __SubmitCommand(controller, device, XHCI_COMMAND_ENABLE_SLOT, &trb);
    if (oserr != OS_EOK) {
        device->State = XHCI_DEVICE_FAILED;
        device->BootstrapTransfer = NULL;
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
        device->State = XHCI_DEVICE_FAILED;
        if (device->AddressTransfer != NULL) {
            device->AddressTransfer->ResultCode = USBTRANSFERCODE_INVALID;
            device->AddressTransfer->State = USBTRANSFER_STATE_CLEANUP;
            device->AddressTransfer = NULL;
        } else if (device->BootstrapTransfer != NULL) {
            device->BootstrapTransfer->ResultCode = USBTRANSFERCODE_INVALID;
            device->BootstrapTransfer->State = USBTRANSFER_STATE_CLEANUP;
            device->BootstrapTransfer = NULL;
        }
    } else if (command->Type == XHCI_COMMAND_ENABLE_SLOT) {
        device->SlotId = XHCI_TRB_SLOT_ID(eventTrb->Control);
        if (device->SlotId == 0 || device->SlotId > controller->SlotCount) {
            device->State = XHCI_DEVICE_FAILED;
            device->BootstrapTransfer->ResultCode = USBTRANSFERCODE_INVALID;
            device->BootstrapTransfer->State = USBTRANSFER_STATE_CLEANUP;
            device->BootstrapTransfer = NULL;
            goto commandHandled;
        }
        device->DefaultEndpoint->SlotId = device->SlotId;
        controller->DCBaa[device->SlotId] = device->DeviceContextDMATable.Entries[0].Address;
        __BuildAddressContext(controller, device, device->BootstrapTransfer);
        device->State = XHCI_DEVICE_DEFAULT_PENDING;
        if (__SubmitAddressDevice(controller, device, true) != OS_EOK) {
            device->State = XHCI_DEVICE_FAILED;
            device->BootstrapTransfer->ResultCode = USBTRANSFERCODE_INVALID;
            device->BootstrapTransfer->State = USBTRANSFER_STATE_CLEANUP;
            device->BootstrapTransfer = NULL;
        }
    } else if (command->Type == XHCI_COMMAND_ADDRESS_DEVICE) {
        if (device->State == XHCI_DEVICE_DEFAULT_PENDING) {
            device->State = XHCI_DEVICE_DEFAULT;
            device->BootstrapTransfer = NULL;
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
