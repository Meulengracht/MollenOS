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
 * Service - Usb Manager
 * - Contains the implementation of the usb-manager which keeps track
 *   of all usb-controllers and their devices
 */

#define __TRACE

#include <ddk/usbdevice.h>
#include <usb/usb.h>
#include <ddk/device.h>
#include <ddk/utils.h>
#include <internal/_ipc.h>
#include "manager.h"
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include "svc_usb_protocol_server.h"

static const struct {
    uint8_t     Class;
    const char* IdentificationString;
} g_deviceIdentifications[22] = {
    { 0x00, "Generic (Usb)" },
    { 0x01, "Audio (Usb)" },
    { 0x02, "Communications and CDC Control Device" },
    { 0x03, "Human Input Device" },
    { 0x05, "Physical (Usb)" },
    { 0x06, "Imaging (Usb)" },
    { 0x07, "Printer (Usb)" },
    { 0x08, "Mass Storage (Usb)" },
    { 0x09, "Hub Controller (Usb)" },
    { 0x0A, "CDC Data Device" },
    { 0x0B, "Smart Card (Usb)" },
    { 0x0D, "Content Security (Usb)" },
    { 0x0E, "Video (Usb)" },
    { 0x0F, "Personal Healthcare (Usb)" },
    { 0x10, "Audio/Video (Usb)" },
    { 0x11, "Billboard (Usb)" },
    { 0x12, "Usb Type-C Bridge (Usb)" },
    { 0xDC, "Diagnostics (Usb)" },
    { 0xE0, "Wireless Controller (Usb)" },
    { 0xEF, "Miscellaneous (Usb)" },
    { 0xFE, "Application Specific (Usb)" },
    { 0xFF, "Vendor Specific (Usb)" }
};

static list_t      g_devices      = LIST_INIT;
static const char* g_vendorString = "Vendor-specific device (Usb)";

static uint16_t
GetMaxPacketSizeControl(
    _In_ uint8_t speed)
{
    if (speed == USB_SPEED_FULL || speed == USB_SPEED_HIGH) {
        return 64;
    }
    else if (speed == USB_SPEED_SUPER || speed == USB_SPEED_SUPER_PLUS) {
        return 512;
    }
    else {
        return 8;
    }
}

/* UsbGetIdentificationString
 * Retrieves the identification string for the usb class. */
const char*
UsbGetIdentificationString(
    _In_ uint8_t ClassCode)
{
    for (int i = 0; i < 22; i++) {
        if (g_deviceIdentifications[i].Class == ClassCode) {
            return g_deviceIdentifications[i].IdentificationString;
        }
    }
    return g_vendorString;
}

OsStatus_t
UsbCoreInitialize(void)
{
    UsbCoreHubsInitialize();
    return UsbInitialize();
}

static void __CleanupDeviceEntry(
        _In_ element_t* element,
        _In_ void*      context)
{
    // not really used
}

OsStatus_t
UsbCoreDestroy(void)
{
    UsbCoreHubsCleanup();
    UsbCoreControllersCleanup();
    list_clear(&g_devices, __CleanupDeviceEntry, NULL);
    return UsbCleanup();
}

OsStatus_t
UsbDeviceLoadDrivers(
    _In_ UsbController_t* controller,
    _In_ UsbPortDevice_t* device)
{
    UsbDevice_t  coreDevice     = { { 0 } };
    const char*  identification = UsbGetIdentificationString(device->Class);

    TRACE("[usb] [load_driver] %u:%u %u:%u",
        device->VendorId, device->ProductId, device->Class, device->Subclass);

    memcpy(&coreDevice.Base.Name[0], identification, strlen(&identification[0]));
    memcpy(&coreDevice.DeviceContext, &device->Base, sizeof(usb_device_context_t));
    coreDevice.Base.ParentId = controller->Device.Id;
    coreDevice.Base.Length   = sizeof(UsbDevice_t);
    coreDevice.Base.VendorId = device->VendorId;
    coreDevice.Base.ProductId = device->ProductId;
    coreDevice.Base.Class    = USB_DEVICE_CLASS;
    coreDevice.Base.Subclass = (device->Class << 16) | 0; // Subclass

    device->DeviceId = RegisterDevice(&coreDevice.Base, DEVICE_REGISTER_FLAG_LOADDRIVER);
    if (device->DeviceId == UUID_INVALID) {
        return OsError;
    }
    return OsSuccess;
}

static UsbTransferStatus_t __GetDeviceDescriptor(
        _In_ UsbPortDevice_t* device)
{
    usb_device_descriptor_t deviceDescriptor;
    UsbTransferStatus_t     transferStatus;

    transferStatus = UsbGetDeviceDescriptor(&device->Base, &deviceDescriptor);
    if (transferStatus != TransferFinished) {
        transferStatus = UsbGetDeviceDescriptor(&device->Base, &deviceDescriptor);
        if (transferStatus != TransferFinished) {
            return transferStatus;
        }
    }

    // Debug Information
    TRACE("__GetDeviceDescriptor descriptor length 0x%x, vendor id 0x%x, product id 0x%x",
          deviceDescriptor.Length, deviceDescriptor.VendorId, deviceDescriptor.ProductId);
    TRACE("__GetDeviceDescriptor configurations 0x%x, mps 0x%x",
          deviceDescriptor.ConfigurationCount, deviceDescriptor.MaxPacketSize);


    // Update information from the device descriptor
    device->Base.device_mps = deviceDescriptor.MaxPacketSize;
    device->VendorId        = deviceDescriptor.VendorId;
    device->ProductId       = deviceDescriptor.ProductId;
    device->Class           = deviceDescriptor.Class;
    device->Subclass        = deviceDescriptor.Subclass;
    return transferStatus;
}

static UsbTransferStatus_t __GetConfiguration(
        _In_ UsbPortDevice_t* device)
{
    usb_device_configuration_t configuration;
    UsbTransferStatus_t        transferStatus;

    transferStatus = UsbGetActiveConfigDescriptor(&device->Base, &configuration);
    if (transferStatus == TransferFinished) {
        TRACE("__GetConfiguration current configuration=%u", configuration.base.ConfigurationValue);
        device->DefaultConfiguration = configuration.base.ConfigurationValue;

        // If the Class is 0, then we retrieve the class/subclass from the first interface
        if (device->Class == USB_CLASS_INTERFACE) {
            device->Class    = configuration.interfaces[0].settings[0].base.Class;
            device->Subclass = configuration.interfaces[0].settings[0].base.Subclass;
        }
        UsbFreeConfigDescriptor(&configuration);
    }
    return transferStatus;
}

static UsbTransferStatus_t __SetDefaultConfiguration(
        _In_ UsbPortDevice_t* device)
{
    UsbTransferStatus_t transferStatus;

    transferStatus = UsbSetConfiguration(&device->Base, device->DefaultConfiguration);
    if (transferStatus != TransferFinished) {
        transferStatus = UsbSetConfiguration(&device->Base, device->DefaultConfiguration);
    }
    return transferStatus;
}

OsStatus_t
UsbCoreDevicesCreate(
    _In_ UsbController_t* usbController,
    _In_ UsbHub_t*        usbHub,
    _In_ UsbPort_t*       usbPort)
{
    UsbHcPortDescriptor_t portDescriptor;
    UsbTransferStatus_t   tStatus;
    UsbPortDevice_t*      device;
    int                   reservedAddress = 0;

    TRACE("[usb] [%u:%u] setting up", usbHub->PortAddress, usbPort->Address);

    // Make sure that there isn't already one device
    // setup on the port
    if (usbPort->Connected && usbPort->Device != NULL) {
        return OsError;
    }

    // Allocate a new instance of the usb device and reset it
    device = (UsbPortDevice_t*)malloc(sizeof(UsbPortDevice_t));
    if (!device) {
        return OsOutOfMemory;
    }
    memset(device, 0, sizeof(UsbPortDevice_t));

    usbPort->Device = device;

    // Initialize the port by resetting it
    if (UsbHubResetPort(usbHub->DriverId, usbHub->DeviceId, usbPort->Address, &portDescriptor) != OsSuccess) {
        ERROR("[usb] [%u:%u] UsbHubResetPort %u failed",
              usbHub->PortAddress, usbPort->Address, usbHub->DeviceId);
        goto device_error;
    }

    // Sanitize device is still present after reset
    if (portDescriptor.Connected != 1 && portDescriptor.Enabled != 1) {
        goto device_error;
    }

    // Update port
    usbPort->Connected                = 1;
    usbPort->Enabled                  = 1;
    usbPort->Speed                    = portDescriptor.Speed;
    
    // Initialize the members we have, leave device address to 0
    device->Base.controller_device_id = usbController->Device.Id;
    device->Base.controller_driver_id = usbController->DriverId;
    device->Base.hub_device_id        = usbHub->DeviceId;
    device->Base.hub_driver_id        = usbHub->DriverId;
    device->Base.hub_address          = usbHub->DeviceAddress;
    device->Base.port_address         = usbPort->Address;
    device->Base.device_address       = 0;
    device->Base.speed                = usbPort->Speed;
    device->Base.device_mps           = GetMaxPacketSizeControl(usbPort->Speed);
    device->Base.configuration_length = 0;
    
	// Wait 100 for device to stabilize after port-reset 
	// I found this wait to be EXTREMELY crucical, otherwise devices would stall. 
	// because I accessed them to quickly after the reset
	thrd_sleepex(100);

    // Allocate a device-address
    if (UsbCoreControllerReserveAddress(usbController, &reservedAddress) != OsSuccess) {
        ERROR("(UsbReserveAddress %u) Failed to setup port %u:%u",
              usbController->Device, usbHub->PortAddress, usbPort->Address);
        goto device_error;
    }

    // Set device address for the new device
    TRACE("[usb] [%u:%u] new device address => %i",
          usbHub->Address, usbPort->Address, reservedAddress);
    tStatus = UsbSetAddress(&device->Base, reservedAddress);
    if (tStatus != TransferFinished) {
        tStatus = UsbSetAddress(&device->Base, reservedAddress);
        if (tStatus != TransferFinished) {
            ERROR("[usb] [%u:%u] UsbSetAddress failed - %u",
                  usbHub->PortAddress, usbPort->Address, (size_t)tStatus);
            goto device_error;
        }
    }
    
    // After SetAddress device is allowed at-least 10 ms recovery
    device->Base.device_address = reservedAddress;
    thrd_sleepex(10);

    // Query Device Descriptor
    tStatus = __GetDeviceDescriptor(device);
    if (tStatus != TransferFinished) {
        ERROR("[usb] [%u:%u] __GetDeviceDescriptor failed", usbHub->PortAddress, usbPort->Address);
        goto device_error;
    }

    tStatus = __GetConfiguration(device);
    if (tStatus != TransferFinished) {
        ERROR("[usb] [%u:%u] __GetConfiguration failed", usbHub->PortAddress, usbPort->Address);
        goto device_error;
    }

    tStatus = __SetDefaultConfiguration(device);
    if (tStatus != TransferFinished) {
        ERROR("[usb] [%u:%u] __SetDefaultConfiguration failed", usbHub->PortAddress, usbPort->Address);
        goto device_error;
    }
    
    TRACE("[usb] [%u:%u] setup success", usbHub->PortAddress, usbPort->Address);
    return UsbDeviceLoadDrivers(usbController, device);

    // All errors are handled here
device_error:
    TRACE("[usb] [%u:%u] setup failed", usbHub->PortAddress, usbPort->Address);
    UsbCoreDevicesDestroy(usbController, usbPort);
    return OsError;
}

OsStatus_t
UsbCoreDevicesDestroy(
    _In_ UsbController_t* controller,
    _In_ UsbPort_t*       port)
{
    UsbPortDevice_t* device;

    TRACE("UsbCoreDevicesDestroy()");

    if (port == NULL || port->Device == NULL) {
        return OsInvalidParameters;
    }

    // Instantiate the device pointer
    device = port->Device;

    // Release allocated address
    if (device->Base.device_address != 0) {
        UsbCoreControllerReleaseAddress(controller, device->Base.device_address);
    }

    free(device);
    port->Device = NULL;
    return OsSuccess;
}
