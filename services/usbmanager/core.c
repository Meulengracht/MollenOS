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
    uint8_t      Class;
    const char  *IdentificationString;
} DeviceIdentifications[22] = {
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

OsStatus_t UsbDeviceDestroy(UsbController_t* Controller, UsbPort_t* Port);
OsStatus_t UsbCoreControllerUnregister(UUId_t DeviceId);

static list_t      g_controllers  = LIST_INIT;
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
        if (DeviceIdentifications[i].Class == ClassCode) {
            return DeviceIdentifications[i].IdentificationString;
        }
    }
    return g_vendorString;
}

/* UsbReserveAddress 
 * Iterate all 128 addresses in an controller and find one not allocated */
OsStatus_t
UsbReserveAddress(
    _In_  UsbController_t* Controller,
    _Out_ int*             Address)
{
    // We find the first free bit in map
    int Itr = 0, Jtr = 0;

    // Iterate all 128 bits in map and find one not set
    for (; Itr < 4; Itr++) {
        for (Jtr = 0; Jtr < 32; Jtr++) {
            if (!(Controller->AddressMap[Itr] & (1 << Jtr))) {
                Controller->AddressMap[Itr] |= (1 << Jtr);
                *Address = (Itr * 4) + Jtr;
                return OsSuccess;
            }
        }
    }
    return OsError;
}

/* UsbReleaseAddress 
 * Frees an given address in the controller for an usb-device */
OsStatus_t
UsbReleaseAddress(
    _In_ UsbController_t*       Controller,
    _In_ int                    Address)
{
    // Variables
    int mSegment    = (Address / 32);
    int mOffset     = (Address % 32);

    // Sanitize bounds of address
    if (Address <= 0 || Address > 127) {
        return OsError;
    }
    Controller->AddressMap[mSegment] &= ~(1 << mOffset);
    return OsSuccess;
}

OsStatus_t
UsbCoreInitialize(void)
{
    return UsbInitialize();
}

static void __CleanupControllerEntry(
        _In_ element_t* element,
        _In_ void*      context)
{
    UsbController_t* controller = (UsbController_t*)element->value;
    UsbCoreControllerUnregister(controller->Device.Id);
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
    list_clear(&g_controllers, __CleanupControllerEntry, NULL);
    list_clear(&g_devices, __CleanupDeviceEntry, NULL);
    return UsbCleanup();
}

OsStatus_t
UsbCoreControllerRegister(
    _In_ UUId_t              DriverId,
    _In_ Device_t*           Device,
    _In_ UsbControllerType_t Type,
    _In_ int                 RootPorts)
{
    UsbController_t* controller;

    // Allocate a new instance and reset all members
    controller = (UsbController_t*)malloc(sizeof(UsbController_t));
    if (!controller) {
        return OsOutOfMemory;
    }
    memset(controller, 0, sizeof(UsbController_t));

    // Store initial data
    memcpy(&controller->Device, Device, sizeof(Device_t));
    controller->DriverId  = DriverId;
    controller->Type      = Type;
    controller->PortCount = RootPorts;
    ELEMENT_INIT(&controller->Header, 0, controller);

    // Reserve address 0, it's setup address
    controller->AddressMap[0] |= 0x1;

    list_append(&g_controllers, &controller->Header);
    return OsSuccess;
}

void svc_usb_register_callback(struct gracht_recv_message* message, struct svc_usb_register_args* args)
{
    UsbCoreControllerRegister(args->driver_id, args->device, (UsbControllerType_t)args->type, args->port_count);
}

OsStatus_t
UsbCoreControllerUnregister(
    _In_ UUId_t DeviceId)
{
    UsbController_t* controller;
    int              i;

    // Lookup controller and verify existance
    controller = UsbCoreGetController(DeviceId);
    if (controller == NULL) {
        return OsError;
    }

    // Iterate ports that has connected devices and cleanup
    // @todo recursion
    for (i = 0; i < USB_MAX_PORTS; i++) {
        if (controller->RootHub.Ports[i] != NULL) {
            if (controller->RootHub.Ports[i]->Device != NULL) {
                UsbDeviceDestroy(controller, controller->RootHub.Ports[i]);
            }
            free(controller->RootHub.Ports[i]);
        }
    }
    free(controller);
    return OsSuccess;
}

void svc_usb_unregister_callback(struct gracht_recv_message* message, struct svc_usb_unregister_args* args)
{
    UsbCoreControllerUnregister(args->device_id);
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
    coreDevice.Base.DeviceId = device->ProductId;
    coreDevice.Base.Class    = USB_DEVICE_CLASS;
    coreDevice.Base.Subclass = (device->Class << 16) | 0; // Subclass

    device->DeviceId = RegisterDevice(&coreDevice.Base, DEVICE_REGISTER_FLAG_LOADDRIVER);
    if (device->DeviceId == UUID_INVALID) {
        return OsError;
    }
    return OsSuccess;
}

OsStatus_t
UsbDeviceSetup(
    _In_ UsbController_t* Controller,
    _In_ UsbHub_t*        Hub, 
    _In_ UsbPort_t*       Port)
{
    UsbHcPortDescriptor_t   portDescriptor;
    usb_device_descriptor_t deviceDescriptor;
    UsbTransferStatus_t     tStatus;
    UsbPortDevice_t*        device;
    int                     reservedAddress = 0;

    // Debug
    TRACE("[usb] [%u:%u] setting up", Hub->Address, Port->Address);

    // Make sure that there isn't already one device
    // setup on the port
    if (Port->Connected && Port->Device != NULL) {
        return OsError;
    }

    // Allocate a new instance of the usb device and reset it
    device = (UsbPortDevice_t*)malloc(sizeof(UsbPortDevice_t));
    if (!device) {
        return OsOutOfMemory;
    }
    memset(device, 0, sizeof(UsbPortDevice_t));

    Port->Device = device;

    // Initialize the port by resetting it
    if (UsbHubResetPort(Controller->DriverId, Controller->Device.Id,
            Port->Address, &portDescriptor) != OsSuccess) {
        ERROR("[usb] [%u:%u] UsbHubResetPort %u failed",
            Hub->Address, Port->Address, Controller->Device);
        goto DevError;
    }

    // Sanitize device is still present after reset
    if (portDescriptor.Connected != 1 && portDescriptor.Enabled != 1) {
        goto DevError;
    }

    // Update port
    Port->Connected = 1;
    Port->Enabled   = 1;
    Port->Speed     = portDescriptor.Speed;
    
    // Initialize the members we have, leave device address to 0
    device->Base.device_id      = Controller->Device.Id;
    device->Base.driver_id      = Controller->DriverId;
    device->Base.hub_address    = Hub->Address;
    device->Base.port_address   = Port->Address;
    device->Base.device_address = 0;
    device->Base.speed          = Port->Speed;
    device->Base.device_mps     = GetMaxPacketSizeControl(Port->Speed);
    device->Base.configuration_length = 0;
    
	// Wait 100 for device to stabilize after port-reset 
	// I found this wait to be EXTREMELY crucical, otherwise devices would stall. 
	// because I accessed them to quickly after the reset
	thrd_sleepex(100);

    // Allocate a device-address
    if (UsbReserveAddress(Controller, &reservedAddress) != OsSuccess) {
        ERROR("(UsbReserveAddress %u) Failed to setup port %u:%u",
            Controller->Device, Hub->Address, Port->Address);
        goto DevError;
    }

    // Set device address for the new device
    TRACE("[usb] [%u:%u] new device address => %i",
        Hub->Address, Port->Address, reservedAddress);
    tStatus = UsbSetAddress(&device->Base, reservedAddress);
    if (tStatus != TransferFinished) {
        tStatus = UsbSetAddress(&device->Base, reservedAddress);
        if (tStatus != TransferFinished) {
            ERROR("[usb] [%u:%u] UsbSetAddress failed - %u", 
                Hub->Address, Port->Address, (size_t)tStatus);
            goto DevError;
        }
    }
    
    // After SetAddress device is allowed at-least 10 ms recovery
    device->Base.device_address = reservedAddress;
    thrd_sleepex(10);

    // Query Device Descriptor
    tStatus = UsbGetDeviceDescriptor(&device->Base, &deviceDescriptor);
    if (tStatus != TransferFinished) {
        tStatus = UsbGetDeviceDescriptor(&device->Base, &deviceDescriptor);
        if (tStatus != TransferFinished) {
            ERROR("[usb] [%u:%u] UsbGetDeviceDescriptor failed", 
                Hub->Address, Port->Address);
            goto DevError;
        }
    }

    // Debug Information
    TRACE("[usb] [%u:%u] descriptor length 0x%x, vendor id 0x%x, product id 0x%x", 
        Hub->Address, Port->Address, deviceDescriptor.Length,
        deviceDescriptor.VendorId, deviceDescriptor.ProductId);
    TRACE("[usb] [%u:%u] configurations 0x%x, mps 0x%x",
        Hub->Address, Port->Address, deviceDescriptor.ConfigurationCount,
        deviceDescriptor.MaxPacketSize);

    // Update information from the device descriptor
    device->Base.device_mps = deviceDescriptor.MaxPacketSize;
    device->VendorId        = deviceDescriptor.VendorId;
    device->ProductId       = deviceDescriptor.ProductId;
    device->Class           = deviceDescriptor.Class;
    device->Subclass        = deviceDescriptor.Subclass;
    
    // If the Class is 0, then we retrieve the class/subclass from the
    // first interface
    if (deviceDescriptor.Class == USB_CLASS_INTERFACE) {
        usb_device_configuration_t configuration;
        
        tStatus = UsbGetActiveConfigDescriptor(&device->Base, &configuration);
        if (tStatus == TransferFinished) {
            device->Class    = configuration.interfaces[0].settings[0].base.Class;
            device->Subclass = configuration.interfaces[0].settings[0].base.Subclass;
            UsbFreeConfigDescriptor(&configuration);
        }
        else {
            ERROR("[usb] [%u:%u] UsbGetActiveConfigDescriptor failed", 
                Hub->Address, Port->Address);
            goto DevError;
        }
    }

    // Finish setup by selecting the default configuration (0)
    tStatus = UsbSetConfiguration(&device->Base, 0);
    if (tStatus != TransferFinished) {
        tStatus = UsbSetConfiguration(&device->Base, 0);
        if (tStatus != TransferFinished) {
            ERROR("[usb] [%u:%u] UsbSetConfiguration failed", 
                Hub->Address, Port->Address);
            goto DevError;
        }
    }
    
    TRACE("[usb] [%u:%u] setup success", Hub->Address, Port->Address);
    return UsbDeviceLoadDrivers(Controller, device);

    // All errors are handled here
DevError:
    TRACE("[usb] [%u:%u] setup failed", Hub->Address, Port->Address);
    UsbDeviceDestroy(Controller, Port);
    return OsError;
}

/* UsbDeviceDestroy 
 * Unregisters an usb device on a given port, and cleans up all resources
 * that has been allocated, and notifies the driver of unload. */
OsStatus_t
UsbDeviceDestroy(
    _In_ UsbController_t* Controller,
    _In_ UsbPort_t*       Port)
{
    UsbPortDevice_t* device;

    // Debug
    TRACE("UsbDeviceDestroy()");

    // Sanitize parameters
    if (Port == NULL || Port->Device == NULL) {
        return OsError;
    }

    // Instantiate the device pointer
    device = Port->Device;

    // Release allocated address
    if (device->Base.device_address != 0) {
        UsbReleaseAddress(Controller, device->Base.device_address);
    }

    free(device);
    Port->Device = NULL;
    return OsSuccess;
}

UsbPort_t*
UsbPortCreate(
    _In_ uint8_t Address)
{
    UsbPort_t* Port;

    // Allocate a new instance and reset all members to 0
    Port = (UsbPort_t*)malloc(sizeof(UsbPort_t));
    if (!Port) {
        return NULL;
    }
    
    memset(Port, 0, sizeof(UsbPort_t));
    Port->Address = Address;
    return Port;
}

UsbController_t*
UsbCoreGetController(
    _In_ UUId_t DeviceId)
{
    // Iterate all registered controllers
    foreach(element, &g_controllers) {
        // Cast data pointer to known type
        UsbController_t* controller = (UsbController_t*)element->value;
        if (controller->Device.Id == DeviceId) {
            return controller;
        }
    }
    return NULL;
}

OsStatus_t
UsbCoreEventPort(
    _In_ UUId_t  DeviceId,
    _In_ uint8_t HubAddress,
    _In_ uint8_t PortAddress)
{
    UsbController_t*      Controller = NULL;
    UsbHcPortDescriptor_t Descriptor;
    OsStatus_t            Result = OsSuccess;
    UsbHub_t*             Hub    = NULL;
    UsbPort_t*            Port   = NULL;

    // Debug
    TRACE("[UsbCoreEventPort] DeviceId %u, Hub %u, Port %u", DeviceId, HubAddress, PortAddress);

    // Lookup controller first to only handle events
    // from registered controllers
    Controller = UsbCoreGetController(DeviceId);
    if (Controller == NULL) {
        ERROR("No such controller");
        return OsError;
    }

    // Query port status so we know the status of the port
    // Also compare to the current state to see if the change was valid
    if (UsbHubQueryPort(Controller->DriverId, DeviceId, PortAddress, &Descriptor) != OsSuccess) {
        ERROR("Query port failed");
        return OsError;
    }

    // Make sure port exists
    if (HubAddress != 0) {
        // @todo
    }
    else {
        Hub = &Controller->RootHub;
    }
    if (Hub->Ports[PortAddress] == NULL) {
        Hub->Ports[PortAddress] = UsbPortCreate(PortAddress);
    }

    Port = Hub->Ports[PortAddress];

    // Now handle connection events
    if (Descriptor.Connected == 1 && Port->Connected == 0) {
        // Connected event
        // This function updates port-status after reset
        Result = UsbDeviceSetup(Controller, Hub, Port);
    }
    else if (Descriptor.Connected == 0 && Port->Connected == 1) {
        // Disconnected event, remember that the descriptor pointer
        // becomes unavailable the moment we call the destroy device
        Result          = UsbDeviceDestroy(Controller, Port);
        Port->Speed     = Descriptor.Speed;              // TODO: invalid
        Port->Enabled   = Descriptor.Enabled;            // TODO: invalid
        Port->Connected = Descriptor.Connected;          // TODO: invalid
    }
    else {
        // Ignore
        Port->Speed     = Descriptor.Speed;
        Port->Enabled   = Descriptor.Enabled;
        Port->Connected = Descriptor.Connected;
    }
    return Result;
}

void svc_usb_port_event_callback(struct gracht_recv_message* message, struct svc_usb_port_event_args* args)
{
    UsbCoreEventPort(args->device_id, args->hub_address, args->port_address);
}

int
UsbCoreGetControllerCount(void)
{
    return list_count(&g_controllers);
}

UsbController_t*
UsbCoreGetControllerIndex(
    _In_ int Index)
{
    element_t* item;
    int        i = 0;

    _foreach(item, &g_controllers) {
        if (i == Index) {
            return (UsbController_t*)item->value;
        } i++;
    }
    return NULL;
}

void svc_usb_get_controller_count_callback(struct gracht_recv_message* message)
{
    svc_usb_get_controller_count_response(message, UsbCoreGetControllerCount());
}

void svc_usb_get_controller_callback(struct gracht_recv_message* message, struct svc_usb_get_controller_args* args)
{
    UsbHcController_t hcController = { { { 0 } }, 0 };
    UsbController_t*  controller;
    
    controller = UsbCoreGetControllerIndex(args->index);
    if (controller != NULL) {
        memcpy(&hcController.Device, &controller->Device, sizeof(Device_t));
        hcController.Type = controller->Type;
    }
    svc_usb_get_controller_response(message, &hcController);
}
