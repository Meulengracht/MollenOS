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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Human Input Device Driver (Generic)
 */

#define __TRACE

#include <usb/usb.h>
#include <ddk/utils.h>
#include <stdlib.h>
#include "hid.h"

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
    UsbTransferStatus_t        status;
    int                        i, j;
    TRACE("__GetDeviceConfiguration(hidDevice=0x%" PRIxIN ")", hidDevice);
    
    status = UsbGetActiveConfigDescriptor(&hidDevice->Base.DeviceContext, &configuration);
    if (status != TransferFinished) {
        return;
    }
    
    // TODO support interface settings
    for (i = 0; i < configuration.base.NumInterfaces; i++) {
        usb_device_interface_setting_t* interface = &configuration.interfaces[i].settings[0];
        if (__IsSupportedInterface(interface)) {
            for (j = 0; j < interface->base.NumEndpoints; j++) {
                usb_endpoint_descriptor_t* endpoint = &interface->endpoints[j];
                if (USB_ENDPOINT_TYPE(endpoint) == USB_ENDPOINT_INTERRUPT) {
                    hidDevice->Interrupt = memdup(endpoint, sizeof(usb_endpoint_descriptor_t));
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

    TRACE("HidDeviceCreate(usbDevice=0x%" PRIxIN ")", usbDevice);

    hidDevice = (HidDevice_t*)malloc(sizeof(HidDevice_t));
    if (!hidDevice) {
        return NULL;
    }

    memset(hidDevice, 0, sizeof(HidDevice_t));
    memcpy(&hidDevice->Base, usbDevice, sizeof(UsbDevice_t));

    ELEMENT_INIT(&hidDevice->Header, (uintptr_t)usbDevice->Base.Id, hidDevice);
    hidDevice->TransferId = UUID_INVALID;

    __GetDeviceConfiguration(hidDevice);
    
    // Make sure we at-least found an interrupt endpoint
    if (!hidDevice->Interrupt) {
        ERROR("HidDeviceCreate HID endpoint (in, interrupt) did not exist");
        goto error_exit;
    }

    // Setup device
    if (HidSetupGeneric(hidDevice) != OsSuccess) {
        ERROR("HidDeviceCreate failed to setup the generic hid device");
        goto error_exit;
    }

    // Reset interrupt ep
    if (UsbEndpointReset(&hidDevice->Base.DeviceContext,
            USB_ENDPOINT_ADDRESS(hidDevice->Interrupt->Address)) != OsSuccess) {
        ERROR("HidDeviceCreate failed to reset endpoint (interrupt)");
        goto error_exit;
    }

    // Allocate a ringbuffer for use
    if (dma_pool_allocate(UsbRetrievePool(), 0x400, (void**)&hidDevice->Buffer) != OsSuccess) {
        ERROR("HidDeviceCreate failed to allocate reusable buffer (interrupt-buffer)");
        goto error_exit;
    }

    // Install interrupt pipe
    UsbTransferInitialize(&hidDevice->Transfer, &hidDevice->Base.DeviceContext,
                          hidDevice->Interrupt, USB_TRANSFER_INTERRUPT, 0);
    UsbTransferPeriodic(&hidDevice->Transfer, dma_pool_handle(UsbRetrievePool()),
                        dma_pool_offset(UsbRetrievePool(), hidDevice->Buffer), 0x400,
                        hidDevice->ReportLength, USB_TRANSACTION_IN, (const void*)hidDevice);
    if (UsbTransferQueuePeriodic(&hidDevice->Base.DeviceContext,
                                 &hidDevice->Transfer, &hidDevice->TransferId) != TransferQueued) {
        ERROR("HidDeviceCreate failed to install interrupt transfer");
        goto error_exit;
    }

    return hidDevice;

error_exit:
    if (hidDevice != NULL) {
        HidDeviceDestroy(hidDevice);
    }
    return NULL;
}

OsStatus_t
HidDeviceDestroy(
    _In_ HidDevice_t *Device)
{
    // Destroy the interrupt channel
    if (Device->TransferId != UUID_INVALID) {
        UsbTransferDequeuePeriodic(&Device->Base.DeviceContext, Device->TransferId);
    }

    // Cleanup collections
    HidCollectionCleanup(Device);
    
    // Cleanup the buffer
    if (Device->Buffer != NULL) {
        dma_pool_free(UsbRetrievePool(), Device->Buffer);
    }

    // Cleanup structure
    free(Device);
    return OsSuccess;
}

InterruptStatus_t
HidInterrupt(
    _In_ HidDevice_t *Device, 
    _In_ UsbTransferStatus_t Status,
    _In_ size_t DataIndex)
{
    // Sanitize
    if (Device->Collection == NULL || Status == TransferNAK) {
        return InterruptHandled;
    }

    // Perform the report parse
    if (!HidParseReport(Device, Device->Collection, DataIndex)) {
        return InterruptHandled;
    }

    // Store previous index
    Device->PreviousDataIndex = DataIndex;
    return InterruptHandled;
}
