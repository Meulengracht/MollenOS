/* MollenOS
*
* Copyright 2011 - 2016, Philip Meulengracht
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
* MollenOS USB Core Driver
*/

/* Includes */
#include <UsbCore.h>
#include <Module.h>

/* Kernel */
#include <Timers.h>
#include <Semaphore.h>
#include <Heap.h>
#include <List.h>
#include <Log.h>

/* Drivers */
#include <UsbHid.h>
#include <UsbMsd.h>

/* CLib */
#include <string.h>

/* Globals */
list_t *GlbUsbControllers = NULL;
list_t *GlbUsbDevices = NULL;
list_t *GlbUsbEvents = NULL;
Semaphore_t *GlbEventLock = NULL;
volatile int GlbUsbInitialized = 0;
volatile int GlbUsbControllerId = 0;

/* Prototypes */
void UsbWatchdog(void*);
void UsbEventHandler(void*);
void UsbDeviceSetup(UsbHc_t *Hc, int Port);
void UsbDeviceDestroy(UsbHc_t *Hc, int Port);
UsbHcPort_t *UsbPortCreate(int Port);

/* Entry point of a module */
MODULES_API void ModuleInit(void *Data)
{
	/* Save */
	_CRT_UNUSED(Data);

	/* Init */
	GlbUsbInitialized = 1;
	GlbUsbDevices = list_create(LIST_SAFE);
	GlbUsbControllers = list_create(LIST_SAFE);
	GlbUsbEvents = list_create(LIST_SAFE);
	GlbUsbControllerId = 0;

	/* Initialize Event Semaphore */
	GlbEventLock = SemaphoreCreate(0);

	/* Start Event Thread */
	ThreadingCreateThread("Usb Event Thread", UsbEventHandler, NULL, 0);

	/* Install Usb Watchdog */
	TimersCreateTimer(UsbWatchdog, NULL, TimerPeriodic, USB_WATCHDOG_INTERVAL);
}

/* Registrate an OHCI/UHCI/EHCI/XHCI controller */
UsbHc_t *UsbInitController(void *Data, UsbControllerType_t Type, size_t Ports)
{
	UsbHc_t *Controller;

	/* Allocate Resources */
	Controller = (UsbHc_t*)kmalloc(sizeof(UsbHc_t));
	memset(Controller, 0, sizeof(UsbHc_t));

	/* Fill data */
	Controller->Hc = Data;
	Controller->Type = Type;
	Controller->NumPorts = Ports;

	/* Reserve address 0 */
	Controller->AddressMap[0] |= 0x1;

	/* Done! */
	return Controller;
}

/* Unregistrate an OHCI/UHCI/EHCI/XHCI controller */
int UsbRegisterController(UsbHc_t *Controller)
{
	/* Vars */
	int Id;

	/* Get id */
	Id = GlbUsbControllerId;
	GlbUsbControllerId++;

	/* Add to list */
	list_append(GlbUsbControllers, list_create_node(Id, Controller));

	/* Done */
	return Id;
}

/* Create Event */
void UsbEventCreate(UsbHc_t *Hc, int Port, UsbEventType_t Type)
{
	UsbEvent_t *Event;

	/* Allocate */
	Event = (UsbEvent_t*)kmalloc(sizeof(UsbEvent_t));
	Event->Controller = Hc;
	Event->Port = Port;
	Event->Type = Type;

	/* Append */
	list_append(GlbUsbEvents, list_create_node((int)Type, Event));

	/* Signal */
	SemaphoreV(GlbEventLock);
}

/* Reserve an Address */
size_t UsbReserveAddress(UsbHc_t *Hc)
{
	/* Find first free bit */
	size_t Itr = 0, Jtr = 0;

	/* Check map */
	for (; Itr < 4; Itr++) {
		for (Jtr = 0; Jtr < 32; Jtr++) {
			if (!(Hc->AddressMap[Itr] & (1 << Jtr))) {
				Hc->AddressMap[Itr] |= (1 << Jtr);
				return (Itr * 4) + Jtr;
			}
		}
	}

	/* Wtf? No address?! */
	return 255;
}

/* Release an Address */
void UsbReleaseAddress(UsbHc_t *Hc, size_t Address)
{
	/* Sanity */
	if (Address == 0
		|| Address > 127)
		return;

	/* Which map-part? */
	size_t mSegment = (Address / 32);
	size_t mOffset = (Address % 32);

	/* Unset */
	Hc->AddressMap[mSegment] &= ~(1 << mOffset);
}

/* Device Connected */
void UsbDeviceSetup(UsbHc_t *Hc, int Port)
{
	/* Vars */
	UsbHcDevice_t *Device;
	size_t ReservedAddr;
	int i, j;

	/* Make sure we have the port allocated */
	if (Hc->Ports[Port] == NULL)
		Hc->Ports[Port] = UsbPortCreate(Port);

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
	Device->CtrlEndpoint->Address = 0;
	Device->CtrlEndpoint->Type = EndpointControl;
	Device->CtrlEndpoint->Toggle = 0;
	Device->CtrlEndpoint->Bandwidth = 1;
	Device->CtrlEndpoint->MaxPacketSize = 8;
	Device->CtrlEndpoint->Direction = USB_EP_DIRECTION_BOTH;
	Device->CtrlEndpoint->Interval = 0;
	Device->CtrlEndpoint->AttachedData = NULL;

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
	if (UsbFunctionSetAddress(Hc, Port, ReservedAddr) != TransferFinished)
	{
		/* Try again */
		if (UsbFunctionSetAddress(Hc, Port, ReservedAddr) != TransferFinished)
		{
			LogFatal("USBC", "(Set_Address) Failed to setup port %i", Port);
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
	/* Destruct */
	Hc->EndpointDestroy(Hc->Hc, Device->CtrlEndpoint);

	/* Free Control Endpoint */
	kfree(Device->CtrlEndpoint);

	/* Free Address */
	UsbReleaseAddress(Hc, Device->Address);

	/* Free Interfaces */
	for (i = 0; i < (int)Device->NumInterfaces; i++)
	{
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

/* Device Disconnected */
void UsbDeviceDestroy(UsbHc_t *Hc, int Port)
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
	UsbReleaseAddress(Hc, Device->Address);

	/* Free Interfaces */
	for (i = 0; i < (int)Device->NumInterfaces; i++)
	{
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

/* Ports */
UsbHcPort_t *UsbPortCreate(int Port)
{
	UsbHcPort_t *HcPort;

	/* Allocate Resources */
	HcPort = kmalloc(sizeof(UsbHcPort_t));

	/* Get Port Status */
	HcPort->Id = Port;
	HcPort->Connected = 0;
	HcPort->Device = NULL;
	HcPort->Enabled = 0;

	/* Done */
	return HcPort;
}

/* Usb Watchdog */
void UsbWatchdog(void* Data)
{
	/* Unused */
	_CRT_UNUSED(Data);

	/* Iterate controllers */
	foreach(Node, GlbUsbControllers)
	{
		/* Cast */
		UsbHc_t *Controller = (UsbHc_t*)Node->data;

		/* Invoke the controllers watchdog */
		if (Controller->Watchdog != NULL)
			Controller->Watchdog(Controller->Hc);
	}
}

/* USB Events */
void UsbEventHandler(void *args)
{
	UsbEvent_t *Event;
	list_node_t *lNode;

	/* Unused */
	_CRT_UNUSED(args);

	while (1)
	{
		/* Acquire Semaphore */
		SemaphoreP(GlbEventLock, 0);

		/* Pop Event */
		lNode = list_pop_front(GlbUsbEvents);

		/* Sanity */
		if (lNode == NULL)
			continue;

		/* Cast */
		Event = (UsbEvent_t*)lNode->data;

		/* Free the node */
		kfree(lNode);

		/* Again, sanity */
		if (Event == NULL)
			continue;

		/* Handle Event */
		switch (Event->Type)
		{
			case HcdConnectedEvent:
			{
				/* Setup Device */
				LogInformation("USBC", "Setting up Port %i", Event->Port);
				UsbDeviceSetup(Event->Controller, Event->Port);

			} break;

			case HcdDisconnectedEvent:
			{
				/* Destroy Device */
				LogInformation("USBC", "Destroying Port %i", Event->Port);
				UsbDeviceDestroy(Event->Controller, Event->Port);

			} break;

			case HcdFatalEvent:
			{
				/* Reset Controller */
				LogInformation("USBC", "Resetting Controller");
				Event->Controller->Reset(Event->Controller->Hc);

			} break;

			case HcdRootHubEvent:
			{
				/* Check Ports for Activity */
				Event->Controller->RootHubCheck(Event->Controller->Hc);

			} break;

			default:
			{
				LogFatal("USBC", "Unhandled Event: %u on port %i", Event->Type, Event->Port);
			} break;
		}

		/* Cleanup Event */
		kfree(Event);
	}
}

/* Gets */
UsbHc_t *UsbGetHcd(int ControllerId)
{
	return (UsbHc_t*)list_get_data_by_id(GlbUsbControllers, ControllerId, 0);
}

UsbHcPort_t *UsbGetPort(UsbHc_t *Controller, int Port)
{
	/* Sanity */
	if (Controller == NULL || Port >= (int)Controller->NumPorts)
		return NULL;

	return Controller->Ports[Port];
}