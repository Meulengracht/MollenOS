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

static void __GetDeviceConfiguration(
        _In_ HubDevice_t* hubDevice)
{
    usb_device_configuration_t configuration;
    UsbTransferStatus_t        status;
    int                        i, j;
    TRACE("__GetDeviceConfiguration(hubDevice=0x%" PRIxIN ")", hubDevice);

    status = UsbGetActiveConfigDescriptor(&hubDevice->Base.DeviceContext, &configuration);
    if (status != TransferFinished) {
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
}

HubDevice_t*
HubDeviceCreate(
        _In_ UsbDevice_t* usbDevice)
{
    HubDevice_t* hubDevice;

    TRACE("HubDeviceCreate(usbDevice=0x%" PRIxIN ")", usbDevice);

    hubDevice = (HubDevice_t*)malloc(sizeof(HubDevice_t));
    if (!hubDevice) {
        return NULL;
    }

    memset(hubDevice, 0, sizeof(HubDevice_t));
    memcpy(&hubDevice->Base, usbDevice, sizeof(UsbDevice_t));

    ELEMENT_INIT(&hubDevice->Header, (uintptr_t)usbDevice->Base.Id, hubDevice);
    hubDevice->TransferId = UUID_INVALID;

    __GetDeviceConfiguration(hubDevice);

    // Make sure we at-least found an interrupt endpoint
    if (!hubDevice->Interrupt) {
        ERROR("HubDeviceCreate HUB endpoint (in, interrupt) did not exist");
        goto error_exit;
    }

    // Reset interrupt ep
    if (UsbEndpointReset(&hubDevice->Base.DeviceContext,
                         USB_ENDPOINT_ADDRESS(hubDevice->Interrupt->Address)) != OsSuccess) {
        ERROR("HubDeviceCreate failed to reset endpoint (interrupt)");
        goto error_exit;
    }

    // Allocate a ringbuffer for use
    if (dma_pool_allocate(UsbRetrievePool(), 0x400, (void**)&hubDevice->Buffer) != OsSuccess) {
        ERROR("HubDeviceCreate failed to allocate reusable buffer (interrupt-buffer)");
        goto error_exit;
    }

    // Subscripe to the usb controller for events
    __SubscribeToController(usbDevice->DeviceContext.driver_id);

    // Install interrupt pipe
    //UsbTransferInitialize(&hubDevice->Transfer, &hubDevice->Base.DeviceContext,
    //                      hubDevice->Interrupt, USB_TRANSFER_INTERRUPT, 0);
    //UsbTransferPeriodic(&hubDevice->Transfer, dma_pool_handle(UsbRetrievePool()),
    //                    dma_pool_offset(UsbRetrievePool(), hubDevice->Buffer), 0x400,
    //                    hubDevice->ReportLength, USB_TRANSACTION_IN, (const void*)hubDevice);
//
    //status = UsbTransferQueuePeriodic(&hubDevice->Base.DeviceContext, &hubDevice->Transfer, &hubDevice->TransferId);
    //if (status != TransferQueued && status != TransferInProgress) {
    //    ERROR("HubDeviceCreate failed to install interrupt transfer");
    //    goto error_exit;
    //}

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

void
HubInterrupt(
        _In_ HubDevice_t*        hubDevice,
        _In_ UsbTransferStatus_t transferStatus,
        _In_ size_t              dataIndex)
{

    // Store previous index
    hubDevice->PreviousDataIndex = dataIndex;
}
