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
//#define __TRACE

/* Includes 
 * - System */
#include <os/thread.h>
#include <os/utils.h>
#include "manager.h"

/* Includes
 * - Library */
#include <stdlib.h>
#include <stddef.h>

/* Globals 
 * To keep track of all data since system startup */
static List_t *GlbUsbControllers = NULL;
static List_t *GlbUsbDevices = NULL;
static BufferObject_t *GlbBuffer = NULL;
static BufferPool_t *GlbBufferPool = NULL;

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

/* UsbCoreGetBufferPool
 * Retrieves the buffer-pool that can be used for internal
 * usb-transfers. Usefull for enumeration. */
BufferPool_t*
UsbCoreGetBufferPool(void)
{
	return GlbBufferPool;
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
	GlbBuffer = CreateBuffer(0x1000);
	return BufferPoolCreate(GlbBuffer, &GlbBufferPool);
}

/* UsbCoreDestroy
 * Cleans up and frees any resouces allocated by the usb-core stack */
OsStatus_t
UsbCoreDestroy(void)
{

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

}

/* UsbDeviceSetup 
 * Initializes and enumerates a device if present on the given port */
OsStatus_t
UsbDeviceSetup(
	_In_ UsbController_t *Controller, 
	_In_ UsbPort_t *Port)
{
	// Variables
	UsbHcPortDescriptor_t Descriptor;
	UsbTransferStatus_t tStatus;
	UsbDevice_t *Device = NULL;
	int ReservedAddress = 0;
	int i;

	// Make sure that there isn't already one device
	// setup on the port
	if (Port->Connected
		&& Port->Device != NULL) {
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
		Port->Index, &Descriptor) != OsSuccess) {
		ERROR("(UsbHostResetPort %u) Failed to reset port %i",
			Controller->Device, Port->Index);
		goto DevError;
	}

	// Sanitize device is still present after reset
	if (Descriptor.Connected != 1 
		&& Descriptor.Enabled != 1) {
		goto DevError;
	}

	// Determine the MPS of the control endpoint
	if (Port->Speed == FullSpeed
		|| Port->Speed == HighSpeed) {
		Device->ControlEndpoint.MaxPacketSize = 64;
	}
	else if (Port->Speed == SuperSpeed) {
		Device->ControlEndpoint.MaxPacketSize = 512;
	}

	// Allocate a device-address
	if (UsbReserveAddress(Controller, &ReservedAddress) != OsSuccess) {
		ERROR("(UsbReserveAddress %u) Failed to setup port %i",
			Controller->Device, Port->Index);
		goto DevError;
	}

	// Set device address for the new device
	tStatus = UsbFunctionSetAddress(Controller, Port, ReservedAddress);
	if (tStatus != TransferFinished) {
		tStatus = UsbFunctionSetAddress(Controller, Port, ReservedAddress);
		if (tStatus != TransferFinished) {
			ERROR("(Set_Address) Failed to setup port %i: %u", 
				Port->Index, (size_t)tStatus);
			goto DevError;
		}
	}

	// After SetAddress device is allowed 2 ms recovery
	ThreadSleep(2);

	// Query Device Descriptor
	if (UsbFunctionGetDeviceDescriptor(Controller, Port) != TransferFinished) {
		if (UsbFunctionGetDeviceDescriptor(Controller, Port) != TransferFinished) {
			ERROR("(Get_Device_Desc) Failed to setup port %i", 
				Port->Index);
			goto DevError;
		}
	}

	// Query Config Descriptor
	if (UsbFunctionGetConfigDescriptor(Controller, Port) != TransferFinished) {
		if (UsbFunctionGetConfigDescriptor(Controller, Port) != TransferFinished) {
			ERROR("(Get_Config_Desc) Failed to setup port %i", 
				Port->Index);
			goto DevError;
		}
	}

	// Update Configuration
	if (UsbFunctionSetConfiguration(Controller, Port, Port->Device->Base.Configuration) != TransferFinished) {
		if (UsbFunctionSetConfiguration(Controller, Port, Port->Device->Base.Configuration) != TransferFinished) {
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
}

/* UsbDeviceDestroy 
 * */
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

	// Lookup controller first to only handle events
	// from registered controllers
	Controller = UsbGetController(Device);
	if (Controller == NULL) {
		return OsError;
	}

	// Query port status so we know the status of the port
	// Also compare to the current state to see if the change was valid
	if (UsbHostQueryPort(Driver, Device, Index, &Descriptor) != OsSuccess) {
		return OsError;
	}

	// Make sure port exists
	if (Controller->Ports[Index] == NULL) {
		Controller->Ports[Index] = UsbPortCreate(Index);
	}

	// Shorthand the port
	Port = Controller->Ports[Index];

	// Now handle connection events
	if (Descriptor.Enabled == 1) {
		if (Descriptor.Connected == 1
		&& Port->Connected == 0) {
			// Connected event
			Result = UsbDeviceSetup(Controller, Port);
		}
		else if (Descriptor.Connected == 0
			&& Port->Connected == 1) {
			// Disconnected event
			Result = UsbDeviceDestroy(Controller, Port);
		}
		else {
			// Ignore
		}
	}
	
	// Update members
	Port->Speed = Descriptor.Speed;
	Port->Enabled = Descriptor.Enabled;
	Port->Connected = Descriptor.Connected;

	// Event handled
	return Result;
}
