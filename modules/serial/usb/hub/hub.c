/**
 * MollenOS
 *
 * Copyright 2021, Philip Meulengracht
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Human Input Device Driver (Generic)
 */

#define __TRACE

#include <ddk/utils.h>
#include "hub.h"
#include <internal/_utils.h>
#include <usb/usb.h>
#include <stdlib.h>

#include <gracht/link/vali.h>
#include <ctt_usbhost_protocol_client.h>

static struct {
    UUId_t driverId;
    int    references;
} g_Subscriptions[16] = { { 0 } };

static void*
memdup(void* mem, size_t size)
{
    void* dup = malloc(size);
    if (!dup) {
        return NULL;
    }
    memcpy(dup, mem, size);
    return dup;
}

static void __SubscribeToController(UUId_t driverId)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(driverId);
    int    foundIndex = -1;

    for (int i = 0; i < 16; i++) {
        if (g_Subscriptions[i].driverId == driverId) {
            return;
        }
        if (foundIndex == -1 && !g_Subscriptions[i].driverId) {
            foundIndex = i;
        }
    }

    ctt_usbhost_subscribe(GetGrachtClient(), &msg.base);
    g_Subscriptions[foundIndex].driverId   = driverId;
    g_Subscriptions[foundIndex].references = 1;
}

static void __UnsubscribeToController(UUId_t driverId)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(driverId);

    for (int i = 0; i < 16; i++) {
        if (g_Subscriptions[i].driverId == driverId) {
            g_Subscriptions[i].references--;
            if (!g_Subscriptions[i].references) {
                g_Subscriptions[i].driverId = 0;
                ctt_usbhost_unsubscribe(GetGrachtClient(), &msg.base);
            }
            return;
        }
    }
}

static inline int __IsSupportedInterface(
        _In_ usb_device_interface_setting_t* interface)
{
    TRACE("__IsSupportedInterface(interface=0x%" PRIxIN ")", interface);

    // Verify class is HUB
    if (interface->base.Class != USB_CLASS_HUB) {
        ERROR("__IsSupportedInterface interface->class 0x%x != 0x%x",
              interface->base.Class, USB_CLASS_HUB);
        return 0;
    }
    return 1;
}

static inline void __GetDeviceProtocol(
        _In_ HubDevice_t*                    hubDevice,
        _In_ usb_device_interface_setting_t* interface)
{
    TRACE("__GetDeviceProtocol(hubDevice=0x%" PRIxIN ", interface=0x%" PRIxIN ")",
          hubDevice, interface);

    TRACE("interface->Protocol=%u, interface->Class=%u, interface->Subclass=%u",
          interface->base.Protocol, interface->base.Class, interface->base.Subclass);
    TRACE("interface->AlternativeSetting=%u, interface->NumInterface=%u, interface->NumEndpoints=%u",
          interface->base.AlternativeSetting, interface->base.NumInterface, interface->base.NumEndpoints);

    hubDevice->InterfaceId = interface->base.NumInterface;
}

static OsStatus_t __GetDeviceConfiguration(
        _In_ HubDevice_t* hubDevice)
{
    usb_device_configuration_t configuration;
    UsbTransferStatus_t        status;
    int                        i, j;
    TRACE("__GetDeviceConfiguration(hubDevice=0x%" PRIxIN ")", hubDevice);

    status = UsbGetActiveConfigDescriptor(&hubDevice->Base.DeviceContext, &configuration);
    if (status != TransferFinished) {
        return OsDeviceError;
    }

    TRACE("configuration.ConfigurationValue=%u, configuration.Attributes=%u, configuration.NumInterfaces=%u",
          configuration.base.ConfigurationValue, configuration.base.Attributes,
          configuration.base.NumInterfaces);

    // TODO support interface settings
    for (i = 0; i < configuration.base.NumInterfaces; i++) {
        usb_device_interface_setting_t* interface = &configuration.interfaces[i].settings[0];
        if (__IsSupportedInterface(interface)) {
            for (j = 0; j < interface->base.NumEndpoints; j++) {
                usb_endpoint_descriptor_t* endpoint = &interface->endpoints[j];
                if (USB_ENDPOINT_TYPE(endpoint) == USB_ENDPOINT_INTERRUPT) {
                    hubDevice->Interrupt = memdup(endpoint, sizeof(usb_endpoint_descriptor_t));
                    TRACE("endpoint->Address=%x, endpoint->Attributes=%x, endpoint->Interval=%x",
                          endpoint->Address, endpoint->Attributes, endpoint->Interval);
                    TRACE("endpoint->MaxPacketSize=%x, endpoint->Refresh=%x, endpoint->SyncAddress=%x",
                          endpoint->MaxPacketSize, endpoint->Refresh, endpoint->SyncAddress);
                }
            }
            __GetDeviceProtocol(hubDevice, interface);
            break;
        }
    }

    UsbFreeConfigDescriptor(&configuration);
    return OsSuccess;
}

static OsStatus_t __GetSuperSpeedHubDescriptor(
        _In_ HubDevice_t* hubDevice)
{
    UsbTransferStatus_t     transferStatus;
    UsbHubSuperDescriptor_t descriptor;
    TRACE("__GetSuperSpeedHubDescriptor(hubDevice=0x%" PRIxIN ")", hubDevice);

    transferStatus = UsbExecutePacket(&hubDevice->Base.DeviceContext,
                                      USBPACKET_DIRECTION_IN | USBPACKET_DIRECTION_CLASS,
                              USBPACKET_TYPE_GET_DESC, 0, DESCRIPTOR_TYPE_HUB_SUPERSPEED,
                              0, 8, &descriptor);
    if (transferStatus != TransferFinished) {
        return OsDeviceError;
    }

    // Calculate descriptor length
    hubDevice->DescriptorLength = 10 + DIVUP(descriptor.NumberOfPorts, 8);
    hubDevice->PortCount = descriptor.NumberOfPorts;
    hubDevice->PowerOnDelay = MIN(50, descriptor.PowerOnDelay * 2);
    hubDevice->HubCharacteristics = descriptor.HubCharacteristics;
    return OsSuccess;
}

static OsStatus_t __GetHubDescriptor(
        _In_ HubDevice_t* hubDevice)
{
    UsbTransferStatus_t transferStatus;
    UsbHubDescriptor_t  descriptor;
    TRACE("__GetHubDescriptor(hubDevice=0x%" PRIxIN ")", hubDevice);

    if (hubDevice->Base.DeviceContext.speed >= USB_SPEED_SUPER) {
        return __GetSuperSpeedHubDescriptor(hubDevice);
    }

    transferStatus = UsbExecutePacket(&hubDevice->Base.DeviceContext,
                                      USBPACKET_DIRECTION_IN | USBPACKET_DIRECTION_CLASS,
                                      USBPACKET_TYPE_GET_DESC, 0, DESCRIPTOR_TYPE_HUB,
                                      0, 8, &descriptor);
    if (transferStatus != TransferFinished) {
        return OsDeviceError;
    }

    // Calculate descriptor length
    hubDevice->DescriptorLength = 8 + (descriptor.NumberOfPorts / 8);
    hubDevice->PortCount = descriptor.NumberOfPorts;
    hubDevice->PowerOnDelay = MIN(50, descriptor.PowerOnDelay * 2);
    hubDevice->HubCharacteristics = descriptor.HubCharacteristics;
    return OsSuccess;
}

static HubDevice_t* __CreateHubDevice(
        _In_ UsbDevice_t* usbDevice)
{
    HubDevice_t* hubDevice;
    TRACE("__CreateHubDevice(usbDevice=0x%" PRIxIN ")", usbDevice);

    hubDevice = (HubDevice_t*) malloc(sizeof(HubDevice_t));
    if (!hubDevice) {
        ERROR("__CreateHubDevice hubDevice is null");
        return NULL;
    }

    memset(hubDevice, 0, sizeof(HubDevice_t));
    memcpy(&hubDevice->Base, usbDevice, sizeof(UsbDevice_t));

    ELEMENT_INIT(&hubDevice->Header, (uintptr_t)usbDevice->Base.Id, hubDevice);
    hubDevice->TransferId = UUID_INVALID;
    return hubDevice;
}

static void __EnumeratePorts(
        _In_ HubDevice_t* hubDevice)
{
    uint8_t i;
    TRACE("__EnumeratePorts(hubDevice=0x%" PRIxIN ")", hubDevice);

    // Power on ports
    for (i = 1; i <= hubDevice->PortCount; i++) {
        OsStatus_t osStatus = HubPowerOnPort(hubDevice, i);
        if (osStatus != OsSuccess) {
            ERROR("__EnumeratePorts failed to power on port %u", i);
        }

        if (__IsHubPortsPoweredGlobal(hubDevice)) {
            break;
        }
    }

    // Wait for the specified power on delay
    thrd_sleepex(hubDevice->PowerOnDelay);

    // Now we iterate through each port again, get the connection status and
    // then send a notification to the usb service that its ready for enumeration
    for (i = 1; i <= hubDevice->PortCount; i++) {
        PortStatus_t portStatus;
        OsStatus_t   osStatus;

        osStatus = HubGetPortStatus(hubDevice, i, &portStatus);
        if (osStatus != OsSuccess) {
            ERROR("__EnumeratePorts failed to get status for port %u", i);
            continue;
        }

        TRACE("__EnumeratePorts port=%u, status=0x%x, change=0x%x",
              i, portStatus.Status, portStatus.Change);
        if (portStatus.Status & HUB_PORT_STATUS_CONNECTED) {
            if (portStatus.Change & HUB_PORT_CHANGE_CONNECTED) {
                HubPortClearChange(hubDevice, i, HUB_FEATURE_C_PORT_CONNECTION);
            }
            UsbEventPort(
                    hubDevice->Base.Base.DeviceId,
                    hubDevice->Base.DeviceContext.device_address,
                    i);
        }
    }
}

HubDevice_t*
HubDeviceCreate(
        _In_ UsbDevice_t* usbDevice)
{
    HubDevice_t*        hubDevice;
    uint8_t             interruptEpAddress;
    UsbTransferStatus_t transferStatus;
    OsStatus_t          osStatus;

    TRACE("HubDeviceCreate(usbDevice=0x%" PRIxIN ")", usbDevice);

    hubDevice = __CreateHubDevice(usbDevice);
    if (!hubDevice) {
        return NULL;
    }

    osStatus = __GetDeviceConfiguration(hubDevice);
    if (osStatus != OsSuccess) {
        goto error_exit;
    }

    osStatus = __GetHubDescriptor(hubDevice);
    if (osStatus != OsSuccess) {
        goto error_exit;
    }

    // Make sure we at-least found an interrupt endpoint
    if (!hubDevice->Interrupt) {
        ERROR("HubDeviceCreate HUB endpoint (in, interrupt) did not exist");
        goto error_exit;
    }

    // Reset interrupt ep
    interruptEpAddress = USB_ENDPOINT_ADDRESS(hubDevice->Interrupt->Address);
    if (UsbEndpointReset(&hubDevice->Base.DeviceContext, interruptEpAddress) != OsSuccess) {
        ERROR("HubDeviceCreate failed to reset endpoint (interrupt)");
        goto error_exit;
    }

    // Allocate a ringbuffer for use
    if (dma_pool_allocate(UsbRetrievePool(), 0x100, (void**)&hubDevice->Buffer) != OsSuccess) {
        ERROR("HubDeviceCreate failed to allocate reusable buffer (interrupt-buffer)");
        goto error_exit;
    }

    // Subscripe to the usb controller for events
    __SubscribeToController(usbDevice->DeviceContext.driver_id);

    // Enumerate the ports
    __EnumeratePorts(hubDevice);

    // Install interrupt pipe
    UsbTransferInitialize(&hubDevice->Transfer, &hubDevice->Base.DeviceContext,
                          hubDevice->Interrupt, USB_TRANSFER_INTERRUPT, 0);
    UsbTransferPeriodic(&hubDevice->Transfer, dma_pool_handle(UsbRetrievePool()),
                        dma_pool_offset(UsbRetrievePool(), hubDevice->Buffer), 0x100,
                        DIVUP(hubDevice->PortCount, 8), USB_TRANSACTION_IN, (const void*)hubDevice);

    transferStatus = UsbTransferQueuePeriodic(&hubDevice->Base.DeviceContext, &hubDevice->Transfer, &hubDevice->TransferId);
    if (transferStatus != TransferQueued && transferStatus != TransferInProgress) {
        ERROR("HubDeviceCreate failed to install interrupt transfer");
        goto error_exit;
    }

    return hubDevice;

error_exit:
    if (hubDevice != NULL) {
        HubDeviceDestroy(hubDevice);
    }
    return NULL;
}

void
HubDeviceDestroy(
        _In_ HubDevice_t* hubDevice)
{
    // unregister all ports
    // @todo

    // Destroy the interrupt channel
    if (hubDevice->TransferId != UUID_INVALID) {
        UsbTransferDequeuePeriodic(&hubDevice->Base.DeviceContext, hubDevice->TransferId);
    }

    __UnsubscribeToController(hubDevice->Base.DeviceContext.driver_id);

    if (hubDevice->Buffer != NULL) {
        dma_pool_free(UsbRetrievePool(), hubDevice->Buffer);
    }
    free(hubDevice);
}

static void __HandleConnectionChange(
        _In_ HubDevice_t*  hubDevice,
        _In_ uint8_t       portIndex)
{
    HubPortClearChange(hubDevice, portIndex, HUB_FEATURE_C_PORT_CONNECTION);
    UsbEventPort(
            hubDevice->Base.Base.DeviceId,
            hubDevice->Base.DeviceContext.device_address,
            portIndex);
}

static void __HandleHubOverCurrentEvent(
        _In_ HubDevice_t* hubDevice)
{
    // unregister all devices
    for (uint8_t i = 1; i <= hubDevice->PortCount; i++) {
        // @todo
    }

    // we have to wait for the overcurrent status bit to clear
    while (1) {
        HubStatus_t hubStatus;
        OsStatus_t  osStatus = HubGetStatus(hubDevice, &hubStatus);
        if (osStatus != OsSuccess) {
            ERROR("__HandleHubOverCurrentEvent failed to get hub status");
            return;
        }

        if (!(hubStatus.Status & HUB_STATUS_OVERCURRENT_ACTIVE)) {
            osStatus = HubClearChange(hubDevice, HUB_CHANGE_OVERCURRENT);
            if (osStatus != OsSuccess) {
                ERROR("__HandleHubOverCurrentEvent failed to clear overcurrent change");
            }
            break;
        }
    }

    __EnumeratePorts(hubDevice);
}

void
HubInterrupt(
        _In_ HubDevice_t* hubDevice,
        _In_ size_t       dataIndex)
{
    PortStatus_t portStatus;
    uint8_t*     changeMap = &((uint8_t*)hubDevice->Buffer)[dataIndex];
    TRACE("HubInterrupt(hubDevice=0x%" PRIxIN ", dataIndex=%" PRIuIN ")",
          hubDevice, dataIndex);

    // If the first bit is set - there was an hub event
    if (changeMap[0] & 0x1) {
        HubStatus_t hubStatus;
        OsStatus_t  osStatus = HubGetStatus(hubDevice, &hubStatus);
        if (osStatus != OsSuccess) {
            ERROR("HubInterrupt failed to get hub status");
        }
        else {
            if (hubStatus.Change & HUB_CHANGE_LOCAL_POWER) {
                // change to local power source
            }
            if (hubStatus.Change & HUB_CHANGE_OVERCURRENT) {
                if (__IsHubOverCurrentGlobal(hubDevice)) {
                    __HandleHubOverCurrentEvent(hubDevice);
                }
            }
        }
    }

    // We also have to iterate all other bits in the change bitmap to make sure
    // we do detect all change events
    for (uint8_t i = 1; i <= hubDevice->PortCount; i++) {
        if (changeMap[i / 8] & (i << (i % 8))) {
            OsStatus_t osStatus = HubGetPortStatus(hubDevice, i, &portStatus);
            if (osStatus != OsSuccess) {
                ERROR("HubInterrupt failed to get port %u status", i);
                continue;
            }

            if (portStatus.Change & HUB_PORT_CHANGE_CONNECTED) {
                __HandleConnectionChange(hubDevice, i);
            }
            if (portStatus.Change & HUB_PORT_CHANGE_OVERCURRENT) {
                if (!__IsHubOverCurrentGlobal(hubDevice)) {
                    // unregister port, wait for status to 0, then reenumerate
                    // @todo
                }
            }
            if (portStatus.Change & HUB_PORT_CHANGE_SUSPEND) {
                // resume complete
            }
            if (portStatus.Change & HUB_PORT_CHANGE_RESET) {
                // reset complete
            }
        }
    }

}
