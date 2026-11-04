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
 * Service - Usb Manager
 * - Contains the implementation of the usb-manager which keeps track
 *   of all usb-controllers and their devices
 */

#define __TRACE

#include <ddk/devices/usb.h>
#include <usb/usb.h>
#include <ddk/devices/device.h>
#include <ddk/utils.h>
#include "manager.h"
#include <stdlib.h>
#include <threads.h>

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

static const char* g_vendorString = "Vendor-specific device (Usb)";
static bool        g_coreInitialized;

static uint16_t
GetMaxPacketSizeControl(
    _In_ enum USBSpeed speed)
{
    switch (speed) {
        case USBSPEED_FULL:
        case USBSPEED_HIGH:
            return 64;
        case USBSPEED_SUPER:
        case USBSPEED_SUPER_PLUS:
            return 512;
        default:
            return 8;
    }
}

/**
 * @brief Get the description for a USB class, or the vendor-specific description
 * if the class is unknown.
 */
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

oserr_t
UsbCoreInitialize(void)
{
    oserr_t status;

    if (g_coreInitialized) {
        return OS_EBUSY;
    }

    UsbCoreHubsInitialize();
    
    status = UsbInitialize();
    if (status != OS_EOK) {
        UsbCoreHubsCleanup();
        return status;
    }
    
    status = UsbTopologyStart();
    if (status != OS_EOK) {
        UsbCoreHubsCleanup();
        UsbCleanup();
    } else {
        g_coreInitialized = true;
    }
    return status;
}

oserr_t
UsbCoreDestroy(void)
{
    oserr_t status;

    if (!g_coreInitialized) {
        return OS_EOK;
    }

    status = UsbTopologyStop();
    if (status != OS_EOK) {
        return status;
    }
    
    UsbCoreControllersCleanup();
    UsbCoreHubsCleanup();
    UsbCleanup();

    g_coreInitialized = false;
    return OS_EOK;
}

oserr_t
__LoadDeviceDriver(
    _In_ UsbController_t* controller,
    _In_ UsbPortDevice_t* device)
{
    UsbDevice_t coreDevice;

    TRACE("[usb] [load_driver] %u:%u %u:%u",
        device->VendorId, device->ProductId, device->Class, device->Subclass);

    memset(&coreDevice, 0, sizeof(UsbDevice_t));
    memcpy(&coreDevice.DeviceContext, &device->Base, sizeof(usb_device_context_t));

    coreDevice.Base.Id       = UUID_INVALID;
    coreDevice.Base.Length   = sizeof(UsbDevice_t);
    coreDevice.Base.ParentId = controller->Device->Id;
    coreDevice.Base.VendorId = device->VendorId;
    coreDevice.Base.ProductId = device->ProductId;
    coreDevice.Base.Class    = USB_DEVICE_CLASS;
    coreDevice.Base.Subclass = (device->Class << 16) | 0; // Subclass

    coreDevice.Base.Identification.Description  = (char*)UsbGetIdentificationString(device->Class);
    coreDevice.Base.Identification.Manufacturer = device->Manufacturer == NULL ? NULL : mstr_u8(device->Manufacturer);
    coreDevice.Base.Identification.Product      = device->Product == NULL ? NULL : mstr_u8(device->Product);
    coreDevice.Base.Identification.Serial       = device->Serial == NULL ? NULL : mstr_u8(device->Serial);

    device->DeviceId = RegisterDevice(&coreDevice.Base, DEVICE_REGISTER_FLAG_LOADDRIVER);

    // Free the UTF-8 strings created for device registration.
    free(coreDevice.Base.Identification.Manufacturer);
    free(coreDevice.Base.Identification.Product);
    free(coreDevice.Base.Identification.Serial);
    return device->DeviceId == UUID_INVALID ? OS_EUNKNOWN : OS_EOK;
}

static enum USBTransferCode __GetDeviceDescriptor(
        _In_ UsbPortDevice_t* device)
{
    usb_device_descriptor_t deviceDescriptor;
    enum USBTransferCode     transferStatus;

    transferStatus = UsbGetDeviceDescriptor(&device->Base, &deviceDescriptor);
    if (transferStatus != USBTRANSFERCODE_SUCCESS) {
        transferStatus = UsbGetDeviceDescriptor(&device->Base, &deviceDescriptor);
        if (transferStatus != USBTRANSFERCODE_SUCCESS) {
            return transferStatus;
        }
    }

    // Debug Information
    TRACE("__GetDeviceDescriptor descriptor length 0x%x, vendor id 0x%x, product id 0x%x",
          deviceDescriptor.Length, deviceDescriptor.VendorId, deviceDescriptor.ProductId);
    TRACE("__GetDeviceDescriptor configurations 0x%x, mps 0x%x",
          deviceDescriptor.ConfigurationCount, deviceDescriptor.MaxPacketSize);

    TRACE("USB string indexes: manufacturer=%u product=%u serial=%u",
        deviceDescriptor.StringIndexManufactor, deviceDescriptor.StringIndexProduct,
        deviceDescriptor.StringIndexSerialNumber);

    // Update information from the device descriptor
    device->Base.device_mps   = deviceDescriptor.MaxPacketSize;
    device->ManufactorerIndex = deviceDescriptor.StringIndexManufactor;
    device->ProductIndex      = deviceDescriptor.StringIndexProduct;
    device->SerialIndex       = deviceDescriptor.StringIndexSerialNumber;
    device->VendorId          = deviceDescriptor.VendorId;
    device->ProductId         = deviceDescriptor.ProductId;
    device->Class             = deviceDescriptor.Class;
    device->Subclass          = deviceDescriptor.Subclass;
    return transferStatus;
}

// TODO: Read the supported language list before requesting strings.
static enum USBTransferCode
__GetDeviceString(
    _In_  UsbPortDevice_t* device,
    _In_  uint8_t          stringIndex,
    _Out_ mstring_t**      stringOut)
{
    enum USBTransferCode transferStatus;

    // A zero index means this device has no string for the field. Descriptor
    // zero is the language table and must not be requested as an identity string.
    *stringOut = NULL;
    if (stringIndex == 0) {
        return USBTRANSFERCODE_SUCCESS;
    }

    TRACE("USB string request: address=%u index=%u language=%04x attempt=1",
        device->Base.device_address, stringIndex, USB_LANGUAGE_ENGLISH);
    
    transferStatus = UsbGetStringDescriptor(&device->Base, USB_LANGUAGE_ENGLISH, stringIndex, stringOut);
    if (transferStatus != USBTRANSFERCODE_SUCCESS) {
        TRACE("USB string request: address=%u index=%u language=%04x attempt=2 previous=%u",
            device->Base.device_address, stringIndex, USB_LANGUAGE_ENGLISH, transferStatus);
        transferStatus = UsbGetStringDescriptor(&device->Base, USB_LANGUAGE_ENGLISH, stringIndex, stringOut);
    }
    TRACE("USB string result: address=%u index=%u status=%u",
        device->Base.device_address, stringIndex, transferStatus);
    return transferStatus;
}

static enum USBTransferCode __GetDeviceIdentification(
        _In_ UsbPortDevice_t* device)
{
    enum USBTransferCode transferStatus;

    transferStatus = __GetDeviceString(device, device->ManufactorerIndex, &device->Manufacturer);
    if (transferStatus != USBTRANSFERCODE_SUCCESS) {
        return transferStatus;
    }

    transferStatus = __GetDeviceString(device, device->ProductIndex, &device->Product);
    if (transferStatus != USBTRANSFERCODE_SUCCESS) {
        return transferStatus;
    }
    return __GetDeviceString(device, device->SerialIndex, &device->Serial);
}

static enum USBTransferCode __GetConfiguration(
        _In_ UsbPortDevice_t* device)
{
    usb_device_configuration_t configuration;
    enum USBTransferCode        transferStatus;

    transferStatus = UsbGetActiveConfigDescriptor(&device->Base, &configuration);
    if (transferStatus == USBTRANSFERCODE_SUCCESS) {
        TRACE("__GetConfiguration current configuration=%u", configuration.base.ConfigurationValue);
        device->DefaultConfiguration = configuration.base.ConfigurationValue;

        // If the device class is 0, use the class and subclass of the first interface.
        if (device->Class == USB_CLASS_INTERFACE) {
            device->Class    = configuration.interfaces[0].settings[0].base.Class;
            device->Subclass = configuration.interfaces[0].settings[0].base.Subclass;
        }
        UsbFreeConfigDescriptor(&configuration);
    }
    return transferStatus;
}

static enum USBTransferCode
__SetDefaultConfiguration(
        _In_ UsbPortDevice_t* device)
{
    enum USBTransferCode transferStatus;

    transferStatus = UsbSetConfiguration(&device->Base, device->DefaultConfiguration);
    if (transferStatus != USBTRANSFERCODE_SUCCESS) {
        transferStatus = UsbSetConfiguration(&device->Base, device->DefaultConfiguration);
    }
    return transferStatus;
}

static enum USBTransferCode
__QueryInitialDeviceDescriptor(
        _In_ UsbPortDevice_t* device)
{
    usb_device_descriptor_t deviceDescriptor = { 0 };
    enum USBTransferCode    status;
    const uint8_t*          bytes = (const uint8_t*)&deviceDescriptor;

    // Read the first 8 bytes of the device descriptor to get the control endpoint's
    // maximum packet size. This keeps the initial request within one packet.
    status = UsbExecutePacket(
            &device->Base,
            USBPACKET_DIRECTION_IN,
            USBPACKET_TYPE_GET_DESC,
            0, USB_DESCRIPTOR_DEVICE,
            0,
            8,
            &deviceDescriptor
    );
    if (status == USBTRANSFERCODE_SUCCESS) {
        TRACE("USB EP0 descriptor bytes: %02x %02x %02x %02x %02x %02x %02x %02x",
            bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7]);
        device->Base.device_mps = deviceDescriptor.MaxPacketSize;
    }
    return status;
}

static oserr_t
__ClearEnumerationRecovery(
    _In_ UsbController_t* controller)
{
    struct UsbEnumerationRecovery* pending = &controller->EnumerationRecovery;
    USBPortDescriptor_t            descriptor;
    oserr_t                        status;

    if (!pending->Pending) {
        return OS_EOK;
    }

    // If disabling the port failed, check its status before trying again.
    // A disconnected or disabled port allows address zero to be used again.
    status = UsbHubQueryPort(
        pending->HubDriverId,
        pending->HubDeviceId,
        pending->PortAddress,
        &descriptor
    );
    if (status == OS_EOK && (!descriptor.Connected || !descriptor.Enabled)) {
        pending->Pending = false;
        return OS_EOK;
    }
    
    status = UsbHubDisablePort(pending->HubDriverId, pending->HubDeviceId, pending->PortAddress);
    if (status == OS_EOK) {
        pending->Pending = false;
    } else {
        WARNING("USB: cannot disable hub %u port %u (%u); address-zero enumeration blocked",
            pending->HubDeviceId, pending->PortAddress, status);
    }
    return status;
}

oserr_t
UsbCoreDevicesCreate(
    _In_ UsbController_t* usbController,
    _In_ UsbHub_t*        usbHub,
    _In_ UsbPort_t*       usbPort)
{
    USBPortDescriptor_t  portDescriptor;
    enum USBTransferCode usbStatus;
    oserr_t              oserr;
    UsbPortDevice_t*     device;
    int                  reservedAddress = 0;

    TRACE("[usb] [%u:%u] setting up", usbHub->PortAddress, usbPort->Address);

    // The port must not already have a connected device.
    if (usbPort->Connected && usbPort->Device != NULL) {
        return OS_EUNKNOWN;
    }

    // Only the topology worker enumerates ports. If an earlier reset failed,
    // disable that port before another device can use address zero.
    oserr = __ClearEnumerationRecovery(usbController);
    if (oserr != OS_EOK) {
        return oserr;
    }

    // Allocate and clear the device state.
    device = (UsbPortDevice_t*)malloc(sizeof(UsbPortDevice_t));
    if (!device) {
        return OS_EOOM;
    }
    memset(device, 0, sizeof(UsbPortDevice_t));

    usbPort->Device = device;

    // Use address zero until SET_ADDRESS succeeds.
    device->Base.controller_device_id = usbController->Device->Id;
    device->Base.controller_driver_id = usbController->DriverId;
    device->Base.hub_device_id        = usbHub->DeviceId;
    device->Base.hub_driver_id        = usbHub->DriverId;
    device->Base.hub_address          = usbHub->DeviceAddress;
    device->Base.port_address         = usbPort->Address;
    device->Base.device_address       = 0;
    device->Base.speed                = usbPort->Speed;
    device->Base.device_mps           = GetMaxPacketSizeControl(usbPort->Speed);
    device->Base.configuration_length = 0;

    // Record the port before reset can yield or fail. Clear Pending after the
    // SET_ADDRESS recovery delay. On failure, disable the port before the next
    // device is enumerated.
    usbController->EnumerationRecovery = (struct UsbEnumerationRecovery) {
        .HubDriverId = usbHub->DriverId,
        .HubDeviceId = usbHub->DeviceId,
        .PortAddress = usbPort->Address,
        .Pending = true
    };

    // Initialize the port by resetting it
    TRACE("[usb] [%u:%u] first reset begin", usbHub->PortAddress, usbPort->Address);
    if (UsbHubResetPort(usbHub->DriverId, usbHub->DeviceId, usbPort->Address, &portDescriptor) != OS_EOK) {
        ERROR("UsbCoreDevicesCreate: [%u:%u] failed to initialize usb-device [1st-reset]",
              usbHub->PortAddress, usbPort->Address);
        goto device_error;
    }
    TRACE("[usb] [%u:%u] first reset complete: connected=%u enabled=%u speed=%u",
        usbHub->PortAddress, usbPort->Address,
        portDescriptor.Connected, portDescriptor.Enabled, portDescriptor.Speed);

    // The device must still be connected and enabled after reset.
    if (portDescriptor.Connected != 1 || portDescriptor.Enabled != 1) {
        goto device_error;
    }

    // Update port
    usbPort->Connected = 1;
    usbPort->Enabled   = 1;
    usbPort->Speed     = portDescriptor.Speed;
    
    device->Base.speed = usbPort->Speed;
    device->Base.device_mps = GetMaxPacketSizeControl(usbPort->Speed);

    // Wait 100 ms after reset. Devices have stalled when accessed sooner.
    thrd_sleep(&(struct timespec) { .tv_nsec = 100 * NSEC_PER_MSEC }, NULL);

    // The first request after an initial reset must be the Device Descriptor
    // request with a length less than or equal to max packet size.
    TRACE("[usb] [%u:%u] initial descriptor begin", usbHub->PortAddress, usbPort->Address);
    usbStatus = __QueryInitialDeviceDescriptor(device);
    TRACE("[usb] [%u:%u] initial descriptor complete: status=%u packet-size=%u",
        usbHub->PortAddress, usbPort->Address, usbStatus, device->Base.device_mps);
    if (usbStatus != USBTRANSFERCODE_SUCCESS) {
        ERROR("UsbCoreDevicesCreate: [%u:%u] failed to initialize usb-device [query-initial-dd]: %u",
              usbHub->PortAddress, usbPort->Address, usbStatus);
        goto device_error;
    }

    // Reset again after reading the initial descriptor bytes, then set the
    // address before requesting the full 18-byte descriptor.
    TRACE("[usb] [%u:%u] second reset begin", usbHub->PortAddress, usbPort->Address);
    oserr = UsbHubResetPort(usbHub->DriverId, usbHub->DeviceId, usbPort->Address, &portDescriptor);
    if (oserr != OS_EOK) {
        ERROR("UsbCoreDevicesCreate: [%u:%u] failed to initialize usb-device [2nd-reset]",
              usbHub->PortAddress, usbPort->Address);
        goto device_error;
    }

    // Allocate a device-address
    if (UsbCoreControllerReserveAddress(usbController, &reservedAddress) != OS_EOK) {
        ERROR("(UsbReserveAddress %u) Failed to setup port %u:%u",
              usbController->Device, usbHub->PortAddress, usbPort->Address);
        goto device_error;
    }

    // Set device address for the new device
    TRACE("[usb] [%u:%u] new device address => %i",
          usbHub->PortAddress, usbPort->Address, reservedAddress);
    usbStatus = UsbSetAddress(&device->Base, reservedAddress);
    if (usbStatus != USBTRANSFERCODE_SUCCESS) {
        usbStatus = UsbSetAddress(&device->Base, reservedAddress);
        if (usbStatus != USBTRANSFERCODE_SUCCESS) {
            ERROR("[usb] [%u:%u] UsbSetAddress failed - %u",
                  usbHub->PortAddress, usbPort->Address, (size_t)usbStatus);
            goto device_error;
        }
    }
    
    // Allow 10 ms for the device to recover after SET_ADDRESS.
    device->Base.device_address = reservedAddress;
    thrd_sleep(&(struct timespec) { .tv_nsec = 10 * NSEC_PER_MSEC }, NULL);
    usbController->EnumerationRecovery.Pending = false;

    usbStatus = __GetDeviceDescriptor(device);
    if (usbStatus != USBTRANSFERCODE_SUCCESS) {
        ERROR("[usb] [%u:%u] __GetDeviceDescriptor failed", usbHub->PortAddress, usbPort->Address);
        goto device_error;
    }

    usbStatus = __GetDeviceIdentification(device);
    if (usbStatus != USBTRANSFERCODE_SUCCESS) {
        ERROR("[usb] [%u:%u] __GetDeviceIdentification failed", usbHub->PortAddress, usbPort->Address);
        goto device_error;
    }

    usbStatus = __GetConfiguration(device);
    if (usbStatus != USBTRANSFERCODE_SUCCESS) {
        ERROR("[usb] [%u:%u] __GetConfiguration failed", usbHub->PortAddress, usbPort->Address);
        goto device_error;
    }

    usbStatus = __SetDefaultConfiguration(device);
    if (usbStatus != USBTRANSFERCODE_SUCCESS) {
        ERROR("[usb] [%u:%u] __SetDefaultConfiguration failed", usbHub->PortAddress, usbPort->Address);
        goto device_error;
    }
    
    TRACE("[usb] [%u:%u] setup success", usbHub->PortAddress, usbPort->Address);
    oserr = __LoadDeviceDriver(usbController, device);
    if (oserr == OS_EOK) {
        return OS_EOK;
    }

    // All errors are handled here
device_error:
    TRACE("[usb] [%u:%u] setup failed", usbHub->PortAddress, usbPort->Address);
    
    // SET_ADDRESS may have reached the device even if the response failed.
    // Disable the port before its address is reused or another port is reset.
    // This is also required if descriptor reads, configuration, or driver
    // registration fail after the address was assigned.
    usbController->EnumerationRecovery.Pending = true;
    
    (void)__ClearEnumerationRecovery(usbController);
    if (reservedAddress != 0 && device->Base.device_address == 0) {
        UsbCoreControllerReleaseAddress(usbController, reservedAddress);
    }

    UsbCoreDevicesDestroy(usbController, usbPort);
    usbPort->Connected = 0;
    usbPort->Enabled = 0;
    return OS_EUNKNOWN;
}

oserr_t
UsbCoreDevicesDestroy(
    _In_ UsbController_t* controller,
    _In_ UsbPort_t*       port)
{
    UsbPortDevice_t* device;

    TRACE("UsbCoreDevicesDestroy()");

    if (port == NULL || port->Device == NULL) {
        return OS_EINVALPARAMS;
    }

    // Get the device attached to this port.
    device = port->Device;

    // Unload the class driver while its USB context is still valid.
    if (device->DeviceId != UUID_INVALID) {
        (void)UnregisterDevice(device->DeviceId);
    }

    // Remove the device state from the controller before releasing its USB
    // address or freeing the device structure.
    UsbDetachDevice(controller->Device->Id, &device->Base);

    // Release allocated address
    if (device->Base.device_address != 0) {
        UsbCoreControllerReleaseAddress(controller, device->Base.device_address);
    }

    mstr_delete(device->Manufacturer);
    mstr_delete(device->Product);
    mstr_delete(device->Serial);
    free(device);
    port->Device = NULL;
    return OS_EOK;
}
