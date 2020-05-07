/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS MCore - Human Input Device Driver (Generic)
 */
//#define __TRACE

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

static inline int
IsSupportedInterface(
    _In_ usb_device_interface_setting_t* interface)
{
    // Verify class is HID
    if (interface->base.Class != USB_CLASS_HID) {
        return 0;
    }
    
    // Verify the subclass
    if (interface->base.Subclass != HID_SUBCLASS_NONE &&
        interface->base.Subclass != HID_SUBCLASS_BOOT) {
        ERROR("Unsupported HID Subclass 0x%x", interface->base.Subclass);
        return 0;
    }
    
    if (interface->base.Protocol == HID_PROTOCOL_NONE ||
        interface->base.Protocol == HID_PROTOCOL_KEYBOARD ||
        interface->base.Protocol == HID_PROTOCOL_MOUSE) {
        return 1;        
    }

    ERROR("This HID uses an unimplemented protocol and needs external drivers");
    ERROR("Unsupported HID Protocol 0x%x", interface->base.Protocol);
    return 0;
}

static inline void
GetDeviceProtocol(
    _In_ HidDevice_t*                    device,
    _In_ usb_device_interface_setting_t* interface)
{
    device->InterfaceId = interface->base.NumInterface;
    device->CurrentProtocol = HID_DEVICE_PROTOCOL_REPORT;
    
    if (interface->base.Subclass == HID_SUBCLASS_BOOT) {
        device->CurrentProtocol = HID_DEVICE_PROTOCOL_BOOT;
    }
}

static void
GetDeviceConfiguration(
    _In_ HidDevice_t* device)
{
    usb_device_configuration_t configuration;
    UsbTransferStatus_t        status;
    int                        i, j;
    
    status = UsbGetActiveConfigDescriptor(&device->Base.DeviceContext, &configuration);
    if (status != TransferFinished) {
        return;
    }
    
    // TODO support interface settings
    for (i = 0; i < configuration.base.NumInterfaces; i++) {
        usb_device_interface_setting_t* interface = &configuration.interfaces[i].settings[0];
        if (IsSupportedInterface(interface)) {
            for (j = 0; j < interface->base.NumEndpoints; j++) {
                usb_endpoint_descriptor_t* endpoint = &interface->endpoints[j];
                if (USB_ENDPOINT_TYPE(endpoint) == USB_ENDPOINT_INTERRUPT) {
                    device->Interrupt = memdup(endpoint, sizeof(usb_endpoint_descriptor_t));
                }
            }
            GetDeviceProtocol(device, interface);
            break;
        }
    }
    
    UsbFreeConfigDescriptor(&configuration);
}

HidDevice_t*
HidDeviceCreate(
    _In_ UsbDevice_t* UsbDevice)
{
    HidDevice_t* Device;

    // Debug
    TRACE("HidDeviceCreate()");

    // Allocate new resources
    Device = (HidDevice_t*)malloc(sizeof(HidDevice_t));
    memset(Device, 0, sizeof(HidDevice_t));
    memcpy(&Device->Base, UsbDevice, sizeof(UsbDevice_t));
    Device->TransferId = UUID_INVALID;
    GetDeviceConfiguration(Device);
    
    // Make sure we at-least found an interrupt endpoint
    if (Device->Interrupt == NULL) {
        ERROR("HID Endpoint (In, Interrupt) did not exist.");
        goto Error;
    }

    // Setup device
    if (HidSetupGeneric(Device) != OsSuccess) {
        ERROR("Failed to setup the generic hid device.");
        goto Error;
    }

    // Reset interrupt ep
    if (UsbEndpointReset(&Device->Base.DeviceContext,
            USB_ENDPOINT_ADDRESS(Device->Interrupt->Address)) != OsSuccess) {
        ERROR("Failed to reset endpoint (interrupt)");
        goto Error;
    }

    // Allocate a ringbuffer for use
    if (dma_pool_allocate(UsbRetrievePool(), 0x400, (void**)&Device->Buffer) != OsSuccess) {
        ERROR("Failed to allocate reusable buffer (interrupt-buffer)");
        goto Error;
    }

    // Install interrupt pipe
    UsbTransferInitialize(&Device->Transfer, &Device->Base.DeviceContext, 
        Device->Interrupt, USB_TRANSFER_INTERRUPT, 0);
    UsbTransferPeriodic(&Device->Transfer, dma_pool_handle(UsbRetrievePool()), 
        dma_pool_offset(UsbRetrievePool(), Device->Buffer), 0x400, 
        Device->ReportLength, USB_TRANSACTION_IN, (const void*)Device);
    if (UsbTransferQueuePeriodic(&Device->Base.DeviceContext, 
        &Device->Transfer, &Device->TransferId) != TransferQueued) {
        ERROR("Failed to install interrupt transfer");
        goto Error;
    }

    // Done
    return Device;

Error:
    // Cleanup
    if (Device != NULL) {
        HidDeviceDestroy(Device);
    }

    // No device
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
