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
 * Human Input Device Driver (Generic)
 */

#define __TRACE

#include <ddk/utils.h>
#include "hid.h"
#include <internal/_utils.h>
#include <usb/usb.h>
#include <stdlib.h>

#include <gracht/link/vali.h>
#include <ctt_usbhost_service_client.h>

static struct {
    uuid_t driverId;
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

static void __SubscribeToController(uuid_t driverId)
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

static void __UnsubscribeToController(uuid_t driverId)
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

    // Verify class is HID
    if (interface->base.Class != USB_CLASS_HID) {
        ERROR("__IsSupportedInterface interface->class 0x%x != 0x%x",
              interface->base.Class, USB_CLASS_HID);
        return 0;
    }
    
    // Verify the subclass
    if (interface->base.Subclass != HID_SUBCLASS_NONE &&
        interface->base.Subclass != HID_SUBCLASS_BOOT) {
        ERROR("__IsSupportedInterface unsupported HID subclass 0x%x",
              interface->base.Subclass);
        return 0;
    }
    
    if (interface->base.Protocol == HID_PROTOCOL_NONE ||
        interface->base.Protocol == HID_PROTOCOL_KEYBOARD ||
        interface->base.Protocol == HID_PROTOCOL_MOUSE) {
        return 1;        
    }

    ERROR("__IsSupportedInterface this HID uses an unimplemented protocol and needs external drivers");
    ERROR("__IsSupportedInterface unsupported HID Protocol 0x%x", interface->base.Protocol);
    return 0;
}

static inline void __GetDeviceProtocol(
    _In_ HidDevice_t*                    hidDevice,
    _In_ usb_device_interface_setting_t* interface)
{
    TRACE("__GetDeviceProtocol(hidDevice=0x%" PRIxIN ", interface=0x%" PRIxIN ")",
          hidDevice, interface);

    TRACE("interface->Protocol=%u, interface->Class=%u, interface->Subclass=%u",
          interface->base.Protocol, interface->base.Class, interface->base.Subclass);
    TRACE("interface->AlternativeSetting=%u, interface->NumInterface=%u, interface->NumEndpoints=%u",
          interface->base.AlternativeSetting, interface->base.NumInterface, interface->base.NumEndpoints);

    hidDevice->InterfaceId     = interface->base.NumInterface;
    hidDevice->CurrentProtocol = HID_DEVICE_PROTOCOL_REPORT;
    if (interface->base.Subclass == HID_SUBCLASS_BOOT) {
        hidDevice->CurrentProtocol = HID_DEVICE_PROTOCOL_BOOT;
    }
}

static void __GetDeviceConfiguration(
    _In_ HidDevice_t* hidDevice)
{
    usb_device_configuration_t configuration;
    enum USBTransferCode        status;
    int                        i, j;
    TRACE("__GetDeviceConfiguration(hidDevice=0x%" PRIxIN ")", hidDevice);
    
    status = UsbGetActiveConfigDescriptor(&hidDevice->Base->DeviceContext, &configuration);
    if (status != USBTRANSFERCODE_SUCCESS) {
        return;
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
                    hidDevice->Interrupt = memdup(endpoint, sizeof(usb_endpoint_descriptor_t));
                    TRACE("endpoint->Address=%x, endpoint->Attributes=%x, endpoint->Interval=%x",
                          endpoint->Address, endpoint->Attributes, endpoint->Interval);
                    TRACE("endpoint->MaxPacketSize=%x, endpoint->Refresh=%x, endpoint->SyncAddress=%x",
                          endpoint->MaxPacketSize, endpoint->Refresh, endpoint->SyncAddress);
                }
            }
            __GetDeviceProtocol(hidDevice, interface);
            break;
        }
    }
    
    UsbFreeConfigDescriptor(&configuration);
}

HidDevice_t*
HidDeviceCreate(
    _In_ UsbDevice_t* usbDevice)
{
    HidDevice_t* hidDevice;
    oserr_t      oserr;

    TRACE("HidDeviceCreate(usbDevice=0x%" PRIxIN ")", usbDevice);

    hidDevice = (HidDevice_t*)malloc(sizeof(HidDevice_t));
    if (!hidDevice) {
        return NULL;
    }

    memset(hidDevice, 0, sizeof(HidDevice_t));
    ELEMENT_INIT(&hidDevice->Header, (uintptr_t)usbDevice->Base.Id, hidDevice);
    hidDevice->Base       = usbDevice;
    hidDevice->TransferId = UUID_INVALID;

    __GetDeviceConfiguration(hidDevice);
    
    // Make sure we at-least found an interrupt endpoint
    if (!hidDevice->Interrupt) {
        ERROR("HidDeviceCreate HID endpoint (in, interrupt) did not exist");
        goto error_exit;
    }

    // Setup device
    if (HidSetupGeneric(hidDevice) != OS_EOK) {
        ERROR("HidDeviceCreate failed to setup the generic hid device");
        goto error_exit;
    }

    // Reset interrupt ep
    if (UsbEndpointReset(&hidDevice->Base->DeviceContext,
            USB_ENDPOINT_ADDRESS(hidDevice->Interrupt->Address)) != OS_EOK) {
        ERROR("HidDeviceCreate failed to reset endpoint (interrupt)");
        goto error_exit;
    }

    // Allocate a ringbuffer for use
    if (dma_pool_allocate(UsbRetrievePool(), 0x400, (void**)&hidDevice->Buffer) != OS_EOK) {
        ERROR("HidDeviceCreate failed to allocate reusable buffer (interrupt-buffer)");
        goto error_exit;
    }

    // Subscripe to the usb controller for events
    __SubscribeToController(usbDevice->DeviceContext.controller_driver_id);

    // Install interrupt pipe
    UsbTransferInitialize(
            &hidDevice->Transfer,
            &hidDevice->Base->DeviceContext,
            hidDevice->Interrupt,
            USBTRANSFER_TYPE_INTERRUPT,
            USBTRANSFER_DIRECTION_IN,
            0,
            dma_pool_handle(UsbRetrievePool()),
            dma_pool_offset(UsbRetrievePool(), hidDevice->Buffer),
            0x400
    );

    oserr = UsbTransferQueuePeriodic(&hidDevice->Base->DeviceContext, &hidDevice->Transfer, &hidDevice->TransferId);
    if (oserr != OS_EOK) {
        ERROR("HidDeviceCreate failed to install interrupt transfer: %u", oserr);
        goto error_exit;
    }

    return hidDevice;

error_exit:
    if (hidDevice != NULL) {
        HidDeviceDestroy(hidDevice);
    }
    return NULL;
}

void
HidDeviceDestroy(
    _In_ HidDevice_t* hidDevice)
{
    // Destroy the interrupt channel
    if (hidDevice->TransferId != UUID_INVALID) {
        UsbTransferDequeuePeriodic(&hidDevice->Base->DeviceContext, hidDevice->TransferId);
    }

    __UnsubscribeToController(hidDevice->Base->DeviceContext.controller_driver_id);

    if (hidDevice->Buffer != NULL) {
        dma_pool_free(UsbRetrievePool(), hidDevice->Buffer);
    }
    HidCollectionCleanup(hidDevice);
    free(hidDevice);
}

void
HidInterrupt(
    _In_ HidDevice_t*        hidDevice,
    _In_ enum USBTransferCode transferStatus,
    _In_ size_t              dataIndex)
{
    if (!hidDevice->Collection || transferStatus == USBTRANSFERCODE_NAK) {
        return;
    }

    // Perform the report parse
    if (!HidHandleReportEvent(hidDevice, hidDevice->Collection, dataIndex)) {
        return;
    }

    // Store previous index
    hidDevice->PreviousDataIndex = dataIndex;
}
