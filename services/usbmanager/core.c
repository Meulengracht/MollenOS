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
 * MollenOS Service - Usb Manager
 * - Contains the implementation of the usb-manager which keeps track
 *   of all usb-controllers and their devices
 */
#define __TRACE

/* Includes 
 * - System */
#include <os/driver/usb.h>
#include <os/thread.h>
#include <os/utils.h>
#include "manager.h"

/* Includes
 * - Library */
#include <stdlib.h>
#include <stddef.h>

/* UsbDeviceDestroy 
 * Unregisters an usb device on a given port, and cleans up all resources
 * that has been allocated, and notifies the driver of unload. */
OsStatus_t
UsbDeviceDestroy(
    _In_ UsbController_t *Controller,
    _In_ UsbPort_t *Port);

/* UsbGetController 
 * Looks up the controller that matches the device-identifier */
UsbController_t*
UsbGetController(
    _In_ UUId_t Device);

/* Globals 
 * To keep track of all data since system startup */
static List_t *GlbUsbControllers = NULL;
static List_t *GlbUsbDevices = NULL;

/* UsbReserveAddress 
 * Iterate all 128 addresses in an controller and find one not allocated */
OsStatus_t
UsbReserveAddress(
    _In_ UsbController_t *Controller,
    _Out_ int *Address)
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

    // Ok this is not plausible and should never happen
    return OsError;
}

/* UsbReleaseAddress 
 * Frees an given address in the controller for an usb-device */
OsStatus_t
UsbReleaseAddress(
    _In_ UsbController_t *Controller, 
    _In_ int Address)
{
    // Sanitize bounds of address
    if (Address <= 0 || Address > 127) {
        return OsError;
    }

    // Calculate offset
    int mSegment = (Address / 32);
    int mOffset = (Address % 32);

    // Release it
    Controller->AddressMap[mSegment] &= ~(1 << mOffset);
    return OsSuccess;
}

/*
 * */
OsStatus_t
UsbQueryLanguages(
    _In_ UsbController_t *Controller,
    _In_ UsbDevice_t *Device)
{
    // Variables
    UsbStringDescriptor_t Descriptor;
    int i;

    // Get count
    Device->Base.LanguageCount = (Descriptor.Length - 2) / 2;

    // Fill the list
    if (Device->Base.LanguageCount > 0) {
        for (i = 0; i < Device->Base.LanguageCount; i++) {
            Device->Base.Languages[i] = Descriptor.Data[i];
        }
    }

    return OsSuccess;
}

/* UsbQueryConfigurationDescriptors
 * Queries the full configuration descriptor setup including all endpoints and interfaces.
 * This relies on the GetInitialConfigDescriptor. Also allocates all resources neccessary. */
OsStatus_t
UsbQueryConfigurationDescriptors(
    _In_ UsbController_t *Controller,
    _In_ UsbDevice_t *Device)
{
    // Variables
    UsbConfigDescriptor_t Descriptor;
    uint8_t *BufferPointer = NULL;
    void *FullDescriptor = NULL;
    int CurrentIfVersion = 0;
    size_t EpIterator = 0;
    int BytesLeft = 0;

    // Query for the config-descriptor
    if (UsbGetConfigDescriptor(Controller->Driver, Controller->Device,
        &Device->Base, &Device->ControlEndpoint, 
        &Descriptor, sizeof(UsbConfigDescriptor_t)) != TransferFinished) {
        return OsError;
    }

    // Update information
    Device->Base.Configuration = Descriptor.ConfigurationValue;
    Device->Base.ConfigMaxLength = Descriptor.TotalLength;
    Device->Base.InterfaceCount = (int)Descriptor.NumInterfaces;
    Device->Base.MaxPowerConsumption = (uint16_t)(Descriptor.MaxPowerConsumption * 2);

    // Query for the full descriptor
    FullDescriptor = malloc(Descriptor.TotalLength);
    if (UsbGetConfigDescriptor(Controller->Driver, Controller->Device,
        &Device->Base, &Device->ControlEndpoint, 
        FullDescriptor, Descriptor.TotalLength) != TransferFinished) {
        free(FullDescriptor);
        return OsError;
    }

    // Iteration variables
    BufferPointer = (uint8_t*)FullDescriptor;
    BytesLeft = (int)Descriptor.TotalLength;
    EpIterator = 1;
    CurrentIfVersion = 0;
    
    // Update initials
    Device->Base.InterfaceCount = 0;

    // Iterate all descriptors and parse the interfaces and 
    // the endpoints
    while (BytesLeft > 0) {
        
        // Extract identifiers for descriptor
        uint8_t Length = *BufferPointer;
        uint8_t Type = *(BufferPointer + 1);

        // Determine descriptor type, if we reach an interface
        // we must setup a new interface index
        if (Length == sizeof(UsbInterfaceDescriptor_t)
            && Type == USB_DESCRIPTOR_INTERFACE) {
            
            // Variables
            UsbInterfaceDescriptor_t *Interface = 
                (UsbInterfaceDescriptor_t*)BufferPointer;
            UsbInterfaceVersion_t *UsbIfVersionMeta = NULL;
            UsbHcInterfaceVersion_t *UsbIfVersion = NULL;
            UsbInterface_t *UsbInterface = NULL;

            // Short-hand the interface pointer
            UsbInterface = &Device->Interfaces[Interface->NumInterface];

            // Has it been setup yet?
            if (!UsbInterface->Exists) {

                // Copy data over
                UsbInterface->Base.Id = Interface->NumInterface;
                UsbInterface->Base.Class = Interface->Class;
                UsbInterface->Base.Subclass = Interface->Subclass;
                UsbInterface->Base.Protocol = Interface->Protocol;
                UsbInterface->Base.StringIndex = Interface->StrIndexInterface;

                // Update count
                Device->Base.InterfaceCount++;
                UsbInterface->Exists = 1;
            }

            // Shorthand both interface-versions
            UsbIfVersionMeta = &UsbInterface->Versions[Interface->AlternativeSetting];
            UsbIfVersion = &UsbInterface->Base.Versions[Interface->AlternativeSetting];

            // Parse the version, all interfaces needs atleast 1 version
            if (!UsbIfVersionMeta->Exists) {

                // Print some debug information
                TRACE("Interface %u.%u - Endpoints %u (Class %u, Subclass %u, Protocol %u)",
                    Interface->NumInterface, Interface->AlternativeSetting, Interface->NumEndpoints, Interface->Class,
                    Interface->Subclass, Interface->Protocol);

                // Store number of endpoints and generate an id
                UsbIfVersionMeta->Base.Id = Interface->AlternativeSetting;
                UsbIfVersionMeta->Base.EndpointCount = Interface->NumEndpoints;

                // Setup some state-machine variables
                CurrentIfVersion = Interface->AlternativeSetting;
                EpIterator = 0;
            }

            // Copy information from meta to base
            memcpy(UsbIfVersion, &UsbIfVersionMeta->Base, 
                sizeof(UsbHcInterfaceVersion_t));
        }
        else if ((Length == 7 || Length == 9)
            && Type == USB_DESCRIPTOR_ENDPOINT) {
            
            // Variables
            UsbHcEndpointDescriptor_t *HcEndpoint = NULL;
            UsbEndpointDescriptor_t *Endpoint = NULL;
            UsbEndpointType_t EndpointType;
            size_t EndpointAddress = 0;

            // Protect against null interface-endpoints
            if (Device->Base.InterfaceCount == 0) {
                goto NextEntry;
            }

            // Instantiate pointer
            Endpoint = (UsbEndpointDescriptor_t*)BufferPointer;
            HcEndpoint = &Device->Interfaces[
                Device->Base.InterfaceCount - 1].
                    Versions[CurrentIfVersion].Endpoints[EpIterator];

            // Extract some information
            EndpointAddress = (Endpoint->Address & 0xF);
            EndpointType = (UsbEndpointType_t)(Endpoint->Attributes & 0x3);

            // Trace some information
            TRACE("Endpoint %u (%s) - Attributes 0x%x (MaxPacketSize 0x%x)",
                EndpointAddress, ((Endpoint->Address & 0x80) != 0 ? "IN" : "OUT"), 
                Endpoint->Attributes, Endpoint->MaxPacketSize);

            // Update the hc-endpoint
            HcEndpoint->Address = EndpointAddress;
            HcEndpoint->MaxPacketSize = (Endpoint->MaxPacketSize & 0x7FF);
            HcEndpoint->Bandwidth = ((Endpoint->MaxPacketSize >> 11) & 0x3) + 1;
            HcEndpoint->Interval = Endpoint->Interval;
            HcEndpoint->Type = EndpointType;

            // Determine the direction of the EP
            if (Endpoint->Address & 0x80) {
                HcEndpoint->Direction = USB_ENDPOINT_IN;
            }	
            else {
                HcEndpoint->Direction = USB_ENDPOINT_OUT;
            }
            
            // Sanitize the endpoint count, we've experienced they
            // don't always match... which is awkward.
            if (Device->Interfaces[Device->Base.InterfaceCount - 1].
                    Versions[CurrentIfVersion].Base.EndpointCount < (EpIterator + 1)) {

                // Yeah we got to correct it. Bad implementation
                Device->Interfaces[
                    Device->Base.InterfaceCount - 1].
                        Versions[CurrentIfVersion].Base.EndpointCount++;
            }

            // Increase the EP index
            EpIterator++;
        }

        // Go to next descriptor entry
    NextEntry:
        BufferPointer += Length;
        BytesLeft -= Length;
    }

    // Cleanup buffer
    free(FullDescriptor);

    // Done
    return OsSuccess;
}

/* UsbCoreInitialize
 * Initializes the usb-core stack driver. Allocates all neccessary resources
 * for managing usb controllers devices in the system. */
OsStatus_t
UsbCoreInitialize(void)
{
    // Initialize globals to a known state
    GlbUsbControllers = ListCreate(KeyInteger, LIST_SAFE);
    GlbUsbDevices = ListCreate(KeyInteger, LIST_SAFE);

    // Allocate buffers
    return UsbInitialize();
}

/* UsbCoreDestroy
 * Cleans up and frees any resouces allocated by the usb-core stack */
OsStatus_t
UsbCoreDestroy(void)
{
    // Iterate all registered controllers
    // and clean them up
    foreach(cNode, GlbUsbControllers) {
        // Instantiate a pointer of correct type
        UsbController_t *Controller =
            (UsbController_t*)cNode->Data;
        UsbControllerUnregister(Controller->Driver, Controller->Device);
    }

    // Destroy lists
    ListDestroy(GlbUsbControllers);
    ListDestroy(GlbUsbDevices);

    // Cleanup resources
    return UsbCleanup();
}

/* UsbControllerRegister
 * Registers a new controller with the given type and setup */
OsStatus_t
UsbControllerRegister(
    _In_ UUId_t Driver,
    _In_ UUId_t Device,
    _In_ UsbControllerType_t Type,
    _In_ size_t Ports)
{
    // Variables
    UsbController_t *Controller = NULL;
    DataKey_t Key;

    // Allocate a new instance and reset all members
    Controller = (UsbController_t*)malloc(sizeof(UsbController_t));
    memset(Controller, 0, sizeof(UsbController_t));

    // Store initial data
    Controller->Driver = Driver;
    Controller->Device = Device;
    Controller->Type = Type;
    Controller->PortCount = Ports;

    // Reserve address 0, it's setup address
    Controller->AddressMap[0] |= 0x1;

    // Set key 0
    Key.Value = 0;

    // Add to list of controllers
    return ListAppend(GlbUsbControllers, 
        ListCreateNode(Key, Key, Controller));
}

/* UsbControllerUnregister
 * Unregisters the given usb-controller from the manager and
 * unregisters any devices registered by the controller */
OsStatus_t
UsbControllerUnregister(
    _In_ UUId_t Driver,
    _In_ UUId_t Device)
{
    // Variables
    UsbController_t *Controller = NULL;
    int i;

    // Lookup controller and verify existance
    Controller = UsbGetController(Device);
    if (Controller == NULL) {
        return OsError;
    }

    // Iterate ports that has connected devices and cleanup
    for (i = 0; i < USB_MAX_PORTS; i++) {
        if (Controller->Ports[i] != NULL) {
            if (Controller->Ports[i]->Device != NULL) {
                UsbDeviceDestroy(Controller, Controller->Ports[i]);
            }
            free(Controller->Ports[i]);
        }
    }

    // Don't remove from list
    free(Controller);

    // Done
    return OsSuccess;
}

/* UsbDeviceSetup 
 * Initializes and enumerates a device if present on the given port */
OsStatus_t
UsbDeviceSetup(
    _In_ UsbController_t *Controller, 
    _In_ UsbPort_t *Port)
{
    // Variables
    UsbDeviceDescriptor_t DeviceDescriptor;
    UsbHcPortDescriptor_t PortDescriptor;
    UsbTransferStatus_t tStatus;
    UsbDevice_t *Device = NULL;
    int ReservedAddress = 0;
    int i;

    // Debug
    TRACE("UsbDeviceSetup()");

    // Make sure that there isn't already one device
    // setup on the port
    if (Port->Connected && Port->Device != NULL) {
        return OsError;
    }

    // Allocate a new instance of the usb device and reset it
    Device = (UsbDevice_t*)malloc(sizeof(UsbDevice_t));
    memset(Device, 0, sizeof(UsbDevice_t));

    // Initialize the control-endpoint
    Device->ControlEndpoint.Type = EndpointControl;
    Device->ControlEndpoint.Bandwidth = 1;
    Device->ControlEndpoint.MaxPacketSize = 8;
    Device->ControlEndpoint.Direction = USB_ENDPOINT_BOTH;

    // Bind it to port
    Port->Device = Device;

    // Initialize the port by resetting it
    if (UsbHostResetPort(Controller->Driver, Controller->Device,
            Port->Index, &PortDescriptor) != OsSuccess) {
        ERROR("(UsbHostResetPort %u) Failed to reset port %i",
            Controller->Device, Port->Index);
        goto DevError;
    }

    // Sanitize device is still present after reset
    if (PortDescriptor.Connected != 1 
        && PortDescriptor.Enabled != 1) {
        goto DevError;
    }

    // Update port
    Port->Connected = 1;
    Port->Enabled = 1;
    Port->Speed = PortDescriptor.Speed;

    // Determine the MPS of the control endpoint
    if (Port->Speed == FullSpeed
        || Port->Speed == HighSpeed) {
        Device->ControlEndpoint.MaxPacketSize = 64;
    }
    else if (Port->Speed == SuperSpeed) {
        Device->ControlEndpoint.MaxPacketSize = 512;
    }
    
    // Initialize the underlying device class
    Device->Base.Address = 0;
    Device->Base.Speed = Port->Speed;

    // Allocate a device-address
    TRACE("Allocating usb-address");
    if (UsbReserveAddress(Controller, &ReservedAddress) != OsSuccess) {
        ERROR("(UsbReserveAddress %u) Failed to setup port %i",
            Controller->Device, Port->Index);
        goto DevError;
    }

    // Set device address for the new device
    TRACE("Updating usb-address");
    tStatus = UsbSetAddress(Controller->Driver, Controller->Device,
        &Device->Base, &Device->ControlEndpoint, ReservedAddress);
    if (tStatus != TransferFinished) {
        tStatus = UsbSetAddress(Controller->Driver, Controller->Device,
            &Device->Base, &Device->ControlEndpoint, ReservedAddress);
        if (tStatus != TransferFinished) {
            ERROR("(Set_Address) Failed to setup port %i: %u", 
                Port->Index, (size_t)tStatus);
            goto DevError;
        }
    }

    // Update it's address
    Device->Base.Address = ReservedAddress;

    // After SetAddress device is allowed 2 ms recovery
    ThreadSleep(2);

    // Query Device Descriptor
    if (UsbGetDeviceDescriptor(Controller->Driver, Controller->Device,
        &Device->Base, &Device->ControlEndpoint, &DeviceDescriptor) != TransferFinished) {
        if (UsbGetDeviceDescriptor(Controller->Driver, Controller->Device,
            &Device->Base, &Device->ControlEndpoint, &DeviceDescriptor) != TransferFinished) {
            ERROR("(Get_Device_Desc) Failed to setup port %i", 
                Port->Index);
            goto DevError;
        }
    }

    // Debug Information
    TRACE("USB Length 0x%x - Device Vendor Id & Product Id: 0x%x - 0x%x", 
    DeviceDescriptor.Length, DeviceDescriptor.VendorId, DeviceDescriptor.ProductId);
    TRACE("Device Configurations 0x%x, Max Packet Size: 0x%x",
        DeviceDescriptor.ConfigurationCount, DeviceDescriptor.MaxPacketSize);

    // Update information
    Device->Base.Class = DeviceDescriptor.Class;
    Device->Base.Subclass = DeviceDescriptor.Subclass;
    Device->Base.Protocol = DeviceDescriptor.Protocol;
    Device->Base.VendorId = DeviceDescriptor.VendorId;
    Device->Base.ProductId = DeviceDescriptor.ProductId;
    Device->Base.StringIndexManufactor = DeviceDescriptor.StringIndexManufactor;
    Device->Base.StringIndexProduct = DeviceDescriptor.StringIndexProduct;
    Device->Base.StringIndexSerialNumber = DeviceDescriptor.StringIndexSerialNumber;
    Device->Base.ConfigurationCount = DeviceDescriptor.ConfigurationCount;
    Device->Base.MaxPacketSize = DeviceDescriptor.MaxPacketSize;

    // Update MPS
    Device->ControlEndpoint.MaxPacketSize = DeviceDescriptor.MaxPacketSize;

    // Query Config Descriptor
    if (UsbQueryConfigurationDescriptors(Controller, Device) != OsSuccess) {
        if (UsbQueryConfigurationDescriptors(Controller, Device) != OsSuccess) {
            ERROR("(Get_Config_Desc) Failed to setup port %i", 
                Port->Index);
            goto DevError;
        }
    }

    // Update Configuration
    if (UsbSetConfiguration(Controller->Driver, Controller->Device,
        &Device->Base, &Device->ControlEndpoint, Device->Base.Configuration) != TransferFinished) {
        if (UsbSetConfiguration(Controller->Driver, Controller->Device,
            &Device->Base, &Device->ControlEndpoint, Device->Base.Configuration) != TransferFinished) {
            ERROR("(Set_Configuration) Failed to setup port %i", 
                Port->Index);
            goto DevError;
        }
    }

    // Iterate discovered interfaces
    for (i = 0; i < Device->Base.InterfaceCount; i++) {
        if (Device->Interfaces[i].Base.Class == USB_CLASS_HID) {
            //UsbHidInit(Device, i);
        }
        if (Device->Interfaces[i].Base.Class == USB_CLASS_MSD) {
            //UsbMsdInit(Device, i);
        }
        if (Device->Interfaces[i].Base.Class == USB_CLASS_HUB) {
            // Protocol specifies usb interface (high or low speed)
        }
    }

    // Setup succeeded
    TRACE("Setup of port %i done!", Port->Index);
    return OsSuccess;

    // All errors are handled here
DevError:
    TRACE("Setup of port %i failed!", Port->Index);

    // Release allocated address
    if (Device->Base.Address != 0) {
        UsbReleaseAddress(Controller, Device->Base.Address);
    }

    // Free the buffer that contains the descriptors
    if (Device->Descriptors != NULL) {
        free(Device->Descriptors);
    }

    // Free base
    free(Device);

    // Reset device pointer
    Port->Device = NULL;
    return OsError;
}

/* UsbDeviceDestroy 
 * Unregisters an usb device on a given port, and cleans up all resources
 * that has been allocated, and notifies the driver of unload. */
OsStatus_t
UsbDeviceDestroy(
    _In_ UsbController_t *Controller,
    _In_ UsbPort_t *Port)
{
    // Variables
    UsbDevice_t *Device = NULL;
    int i;

    // Sanitize parameters
    if (Port == NULL || Port->Device == NULL) {
        return OsError;
    }

    // Instantiate the device pointer
    Device = Port->Device;

    // Iterate all interfaces and send unregister
    for (i = 0; i < Device->Base.InterfaceCount; i++) {
        // Send unregister
    }

    // Release allocated address
    if (Device->Base.Address != 0) {
        UsbReleaseAddress(Controller, Device->Base.Address);
    }

    // Free the buffer that contains the descriptors
    if (Device->Descriptors != NULL) {
        free(Device->Descriptors);
    }

    // Free base
    free(Device);

    // Reset device pointer
    Port->Device = NULL;
    return OsSuccess;
}

/* UsbPortCreate
 * Creates a port with the given index */
UsbPort_t*
UsbPortCreate(
    _In_ int Index)
{
    // Variables
    UsbPort_t *Port = NULL;

    // Allocate a new instance and reset all members to 0
    Port = (UsbPort_t*)malloc(sizeof(UsbPort_t));
    memset(Port, 0, sizeof(UsbPort_t));

    // Store index
    Port->Index = Index;

    // All set
    return Port;
}

/* UsbGetController 
 * Looks up the controller that matches the device-identifier */
UsbController_t*
UsbGetController(
    _In_ UUId_t Device)
{
    // Iterate all registered controllers
    foreach(cNode, GlbUsbControllers) {
        // Cast data pointer to known type
        UsbController_t *Controller = 
            (UsbController_t*)cNode->Data;
        if (Controller->Device == Device) {
            return Controller;
        }
    }

    // Not found
    return NULL;
}

/* UsbEventPort 
 * Fired by a usbhost controller driver whenever there is a change
 * in port-status. The port-status is then queried automatically by
 * the usbmanager. */
OsStatus_t
UsbEventPort(
    _In_ UUId_t Driver,
    _In_ UUId_t Device,
    _In_ int Index)
{
    // Variables
    UsbController_t *Controller = NULL;
    UsbPort_t *Port = NULL;
    UsbHcPortDescriptor_t Descriptor;
    OsStatus_t Result = OsSuccess;

    // Debug
    TRACE("UsbEventPort(Device %u, Index %i)", Device, Index);

    // Lookup controller first to only handle events
    // from registered controllers
    Controller = UsbGetController(Device);
    if (Controller == NULL) {
        ERROR("No such controller");
        return OsError;
    }

    // Query port status so we know the status of the port
    // Also compare to the current state to see if the change was valid
    if (UsbHostQueryPort(Driver, Device, Index, &Descriptor) != OsSuccess) {
        ERROR("Query port failed");
        return OsError;
    }

    // Make sure port exists
    if (Controller->Ports[Index] == NULL) {
        Controller->Ports[Index] = UsbPortCreate(Index);
    }

    // Shorthand the port
    Port = Controller->Ports[Index];

    // Now handle connection events
    if (Descriptor.Connected == 1 && Port->Connected == 0) {
        // Connected event
        // This function updates port-status after reset
        Result = UsbDeviceSetup(Controller, Port);
    }
    else if (Descriptor.Connected == 0 && Port->Connected == 1) {
        // Disconnected event
        Result = UsbDeviceDestroy(Controller, Port);
        Port->Speed = Descriptor.Speed;
        Port->Enabled = Descriptor.Enabled;
        Port->Connected = Descriptor.Connected;
    }
    else {
        // Ignore
        Port->Speed = Descriptor.Speed;
        Port->Enabled = Descriptor.Enabled;
        Port->Connected = Descriptor.Connected;
    }

    // Event handled
    return Result;
}
