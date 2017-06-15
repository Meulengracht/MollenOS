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

/* Includes 
 * - System */
#include "manager.h"

/* Includes
 * - Library */
#include <stddef.h>

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

/* UsbCoreInitialize
 * Initializes the usb-core stack driver. Allocates all neccessary resources
 * for managing usb controllers devices in the system. */
OsStatus_t
UsbCoreInitialize(void)
{
	// Initialize globals to a known state
	GlbUsbControllers = ListCreate(KeyInteger, LIST_SAFE);
	GlbUsbDevices = ListCreate(KeyInteger, LIST_SAFE);

	// No error
	return OsSuccess;
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
	Controller = (UsbController_t*)kmalloc(sizeof(UsbController_t));
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
	/* Vars */
	UsbTransferStatus_t tStatus;
	UsbHcDevice_t *Device;
	size_t ReservedAddr;
	int i, j;

	/* Sanity */
	if (Hc->Ports[Port]->Connected
		&& Hc->Ports[Port]->Device != NULL)
		return;

	/* Create a device */
	Device = (UsbHcDevice_t*)kmalloc(sizeof(UsbHcDevice_t));

	/* Reset structure */
	memset(Device, 0, sizeof(UsbHcDevice_t));

	/* Set Hc & Port */
	Device->HcDriver = Hc;
	Device->Port = (size_t)Port;
	
	/* Initial Address must be 0 */
	Device->Address = 0;

	/* Allocate control endpoint */
	Device->CtrlEndpoint = (UsbHcEndpoint_t*)kmalloc(sizeof(UsbHcEndpoint_t));
	memset(Device->CtrlEndpoint, 0, sizeof(UsbHcEndpoint_t));
	Device->CtrlEndpoint->Type = EndpointControl;
	Device->CtrlEndpoint->Bandwidth = 1;
	Device->CtrlEndpoint->MaxPacketSize = 8;
	Device->CtrlEndpoint->Direction = USB_EP_DIRECTION_BOTH;

	/* Bind it */
	Hc->Ports[Port]->Device = Device;

	/* Setup Port */
	Hc->PortSetup(Hc->Hc, Hc->Ports[Port]);

	/* Sanity */
	if (Hc->Ports[Port]->Connected != 1
		&& Hc->Ports[Port]->Enabled != 1)
		goto DevError;

	/* Determine control size */
	if (Hc->Ports[Port]->Speed == FullSpeed
		|| Hc->Ports[Port]->Speed == HighSpeed) {
		Device->CtrlEndpoint->MaxPacketSize = 64;
	}
	else if (Hc->Ports[Port]->Speed == SuperSpeed) {
		Device->CtrlEndpoint->MaxPacketSize = 512;
	}

	/* Setup Endpoint */
	Hc->EndpointSetup(Hc->Hc, Device->CtrlEndpoint);

	/* Allocate Address */
	ReservedAddr = UsbReserveAddress(Hc);

	/* Sanity */
	if (ReservedAddr == 0
		|| ReservedAddr > 127) {
		LogFatal("USBC", "(UsbReserveAddress %u) Failed to setup port %i", ReservedAddr, Port);
		goto DevError;
	}

	/* Set Device Address */
	tStatus = UsbFunctionSetAddress(Hc, Port, ReservedAddr);
	if (tStatus != TransferFinished)
	{
		/* Try again */
		tStatus = UsbFunctionSetAddress(Hc, Port, ReservedAddr);
		if (tStatus != TransferFinished) {
			LogFatal("USBC", "(Set_Address) Failed to setup port %i: %u", Port, (size_t)tStatus);
			goto DevError;
		}
	}

	/* After SetAddress device is allowed 2 ms recovery */
	StallMs(2);

	/* Get Device Descriptor */
	if (UsbFunctionGetDeviceDescriptor(Hc, Port) != TransferFinished)
	{
		/* Try Again */
		if (UsbFunctionGetDeviceDescriptor(Hc, Port) != TransferFinished)
		{
			LogFatal("USBC", "(Get_Device_Desc) Failed to setup port %i", Port);
			goto DevError;
		}
	}

	/* Get Config Descriptor */
	if (UsbFunctionGetConfigDescriptor(Hc, Port) != TransferFinished)
	{
		/* Try Again */
		if (UsbFunctionGetConfigDescriptor(Hc, Port) != TransferFinished)
		{
			LogFatal("USBC", "(Get_Config_Desc) Failed to setup port %i", Port);
			goto DevError;
		}
	}

	/* Set Configuration */
	if (UsbFunctionSetConfiguration(Hc, Port, Hc->Ports[Port]->Device->Configuration) != TransferFinished)
	{
		/* Try Again */
		if (UsbFunctionSetConfiguration(Hc, Port, Hc->Ports[Port]->Device->Configuration) != TransferFinished)
		{
			LogFatal("USBC", "(Set_Configuration) Failed to setup port %i", Port);
			goto DevError;
		}
	}

	/* Go through interfaces and add them */
	for (i = 0; i < (int)Hc->Ports[Port]->Device->NumInterfaces; i++)
	{
		/* We want to support Hubs, HIDs and MSDs*/
		uint32_t IfIndex = (uint32_t)i;

		/* Is this an HID Interface? :> */
		if (Hc->Ports[Port]->Device->Interfaces[i]->Class == USB_CLASS_HID)
		{
			/* Registrate us with HID Manager */
			UsbHidInit(Hc->Ports[Port]->Device, IfIndex);
		}

		/* Is this an MSD Interface? :> */
		if (Hc->Ports[Port]->Device->Interfaces[i]->Class == USB_CLASS_MSD)
		{
			/* Registrate us with MSD Manager */
			UsbMsdInit(Hc->Ports[Port]->Device, IfIndex);
		}

		/* Is this an HUB Interface? :> */
		if (Hc->Ports[Port]->Device->Interfaces[i]->Class == USB_CLASS_HUB)
		{
			/* Protocol specifies usb interface (high or low speed) */

			/* Registrate us with Hub Manager */
		}
	}

	/* Done */
	LogInformation("USBC", "Setup of port %i done!", Port);
	return;

DevError:
	LogInformation("USBC", "Setup of port %i failed!", Port);

	/* Destruct */
	Hc->EndpointDestroy(Hc->Hc, Device->CtrlEndpoint);

	/* Free Control Endpoint */
	kfree(Device->CtrlEndpoint);

	/* Free Address */
	if (Device->Address != 0)
		UsbReleaseAddress(Hc, Device->Address);

	/* Free Interfaces */
	for (i = 0; i < (int)Device->NumInterfaces; i++)
	{
		/* Sanity */
		if (Device->Interfaces[i] == NULL)
			continue;

		/* Free Endpoints */
		for (j = 0; j < USB_MAX_VERSIONS; j++) {
			if (Device->Interfaces[i]->Versions[j] != NULL
				&& Device->Interfaces[i]->Versions[j]->Endpoints != NULL)
				kfree(Device->Interfaces[i]->Versions[j]->Endpoints);
			if (Device->Interfaces[i]->Versions[j] != NULL)
				kfree(Device->Interfaces[i]->Versions[j]);
		}

		/* Free the string if any */
		if (Device->Interfaces[i]->Name != NULL)
			kfree(Device->Interfaces[i]->Name);

		/* Free the Interface */
		kfree(Device->Interfaces[i]);
	}

	/* Free Descriptor Buffer */
	if (Device->Descriptors != NULL)
		kfree(Device->Descriptors);

	/* Free Languages / Strings */
	if (Device->NumLanguages != 0)
		kfree(Device->Languages);

	if (Device->Name != NULL)
		MStringDestroy(Device->Name);
	if (Device->Manufactor != NULL)
		MStringDestroy(Device->Manufactor);
	if (Device->SerialNumber != NULL)
		MStringDestroy(Device->SerialNumber);

	/* Free base */
	kfree(Device);

	/* Update Port */
	Hc->Ports[Port]->Connected = 0;
	Hc->Ports[Port]->Enabled = 0;
	Hc->Ports[Port]->Speed = LowSpeed;
	Hc->Ports[Port]->Device = NULL;
}

/* UsbDeviceDestroy 
 * */
OsStatus_t
UsbDeviceDestroy(
	_In_ UsbController_t *Controller,
	_In_ UsbPort_t *Port)
{
	/* Shortcut */
	UsbHcDevice_t *Device = NULL;
	int i, j;

	/* Sanity */
	if (Hc->Ports[Port] == NULL
		|| Hc->Ports[Port]->Device == NULL)
		return;

	/* Cast */
	Device = Hc->Ports[Port]->Device;

	/* Notify Driver(s) */
	for (i = 0; i < (int)Device->NumInterfaces; i++) {
		if (Device->Interfaces[i]->Destroy != NULL)
			Device->Interfaces[i]->Destroy((void*)Device, i);
	}

	/* Destruct */
	Hc->EndpointDestroy(Hc->Hc, Device->CtrlEndpoint);

	/* Free Control Endpoint */
	kfree(Device->CtrlEndpoint);

	/* Free Address */
	if (Device->Address != 0)
		UsbReleaseAddress(Hc, Device->Address);

	/* Free Interfaces */
	for (i = 0; i < (int)Device->NumInterfaces; i++)
	{
		/* Sanity */
		if (Device->Interfaces[i] == NULL)
			continue;

		/* Free Endpoints */
		for (j = 0; j < USB_MAX_VERSIONS; j++) {
			if (Device->Interfaces[i]->Versions[j] != NULL 
				&& Device->Interfaces[i]->Versions[j]->Endpoints != NULL)
				kfree(Device->Interfaces[i]->Versions[j]->Endpoints);
			if (Device->Interfaces[i]->Versions[j] != NULL)
				kfree(Device->Interfaces[i]->Versions[j]);
		}

		/* Free the string if any */
		if (Device->Interfaces[i]->Name != NULL)
			kfree(Device->Interfaces[i]->Name);

		/* Free the Interface */
		kfree(Device->Interfaces[i]);
	}

	/* Free Descriptor Buffer */
	if (Device->Descriptors != NULL)
		kfree(Device->Descriptors);

	/* Free Languages / Strings */
	if (Device->NumLanguages != 0)
		kfree(Device->Languages);

	if (Device->Name != NULL)
		MStringDestroy(Device->Name);
	if (Device->Manufactor != NULL)
		MStringDestroy(Device->Manufactor);
	if (Device->SerialNumber != NULL)
		MStringDestroy(Device->SerialNumber);

	/* Free base */
	kfree(Device);

	/* Update Port */
	Hc->Ports[Port]->Connected = 0;
	Hc->Ports[Port]->Enabled = 0;
	Hc->Ports[Port]->Speed = LowSpeed;
	Hc->Ports[Port]->Device = NULL;
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
	Port = kmalloc(sizeof(UsbPort_t));
	memset(Port, 0, sizeof(UsbPort_t));

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
