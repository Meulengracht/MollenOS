/* MollenOS
*
* Copyright 2011 - 2014, Philip Meulengracht
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
* MollenOS X86-32 USB Core Driver
*/

/* Includes */
#include <UsbCore.h>
#include <Module.h>

/* CLib */
#include <string.h>

/* Globals */
MCoreModuleDescriptor_t *GlbDescriptor = NULL;
list_t *GlbUsbControllers = NULL;
list_t *GlbUsbDevices = NULL;
list_t *GlbUsbEvents = NULL;
Semaphore_t *GlbEventLock = NULL;
volatile uint32_t GlbUsbInitialized = 0;
volatile uint32_t GlbUsbControllerId = 0;

/* Prototypes */
void UsbEventHandler(void*);
void UsbDeviceSetup(UsbHc_t *Hc, int Port);
void UsbDeviceDestroy(UsbHc_t *Hc, int Port);
UsbHcPort_t *UsbPortCreate(int Port);

/* Entry point of a module */
MODULES_API void ModuleInit(MCoreModuleDescriptor_t *DriverDescriptor, void *DeviceData)
{
	/* Save */
	_CRT_UNUSED(DeviceData);
	GlbDescriptor = DriverDescriptor;

	/* Init */
	GlbUsbInitialized = 0xDEADBEEF;
	GlbUsbDevices = GlbDescriptor->ListCreate(LIST_SAFE);
	GlbUsbControllers = GlbDescriptor->ListCreate(LIST_SAFE);
	GlbUsbEvents = GlbDescriptor->ListCreate(LIST_SAFE);
	GlbUsbControllerId = 0;

	/* Initialize Event Semaphore */
	GlbEventLock = GlbDescriptor->SemaphoreCreate(0);

	/* Start Event Thread */
	GlbDescriptor->CreateThread("UsbEventHandler", UsbEventHandler, NULL, 0);
}

/* Registrate an OHCI/UHCI/EHCI/XHCI controller */
UsbHc_t *UsbInitController(void *Data, UsbControllerType_t Type, uint32_t Ports)
{
	UsbHc_t *Controller;

	/* Allocate Resources */
	Controller = (UsbHc_t*)GlbDescriptor->MemAlloc(sizeof(UsbHc_t));
	memset(Controller, 0, sizeof(UsbHc_t));

	Controller->Hc = Data;
	Controller->Type = Type;
	Controller->NumPorts = Ports;

	return Controller;
}

uint32_t UsbRegisterController(UsbHc_t *Controller)
{
	uint32_t Id;

	/* Get id */
	Id = GlbUsbControllerId;
	GlbUsbControllerId++;

	/* Add to list */
	GlbDescriptor->ListAppend(GlbUsbControllers, GlbDescriptor->ListCreateNode(Id, Controller));

	return Id;
}

/* Create Event */
void UsbEventCreate(UsbHc_t *Hc, int Port, UsbEventType_t Type)
{
	UsbEvent_t *Event;

	/* Allocate */
	Event = (UsbEvent_t*)GlbDescriptor->MemAlloc(sizeof(UsbEvent_t));
	Event->Controller = Hc;
	Event->Port = Port;
	Event->Type = Type;

	/* Append */
	GlbDescriptor->ListAppend(GlbUsbEvents, GlbDescriptor->ListCreateNode((int)Type, Event));

	/* Signal */
	GlbDescriptor->SemaphoreV(GlbEventLock);
}

/* Device Connected */
void UsbDeviceSetup(UsbHc_t *Hc, int Port)
{
	UsbHcDevice_t *Device;
	int i;

	/* Make sure we have the port allocated */
	if (Hc->Ports[Port] == NULL)
		Hc->Ports[Port] = UsbPortCreate(Port);

	/* Sanity */
	if (Hc->Ports[Port]->Connected
		&& Hc->Ports[Port]->Device != NULL)
		return;

	/* Create a device */
	Device = (UsbHcDevice_t*)GlbDescriptor->MemAlloc(sizeof(UsbHcDevice_t));
	Device->HcDriver = Hc;
	Device->Port = (uint8_t)Port;
	Device->Destroy = NULL;

	Device->NumInterfaces = 0;
	for (i = 0; i < X86_USB_CORE_MAX_IF; i++)
		Device->Interfaces[i] = NULL;
	
	/* Initial Address must be 0 */
	Device->Address = 0;

	/* Allocate control endpoint */
	Device->CtrlEndpoint = (UsbHcEndpoint_t*)GlbDescriptor->MemAlloc(sizeof(UsbHcEndpoint_t));
	Device->CtrlEndpoint->Address = 0;
	Device->CtrlEndpoint->Type = X86_USB_EP_TYPE_CONTROL;
	Device->CtrlEndpoint->Toggle = 0;
	Device->CtrlEndpoint->Bandwidth = 1;
	Device->CtrlEndpoint->MaxPacketSize = 64;
	Device->CtrlEndpoint->Direction = X86_USB_EP_DIRECTION_BOTH;
	Device->CtrlEndpoint->Interval = 0;

	/* Bind it */
	Hc->Ports[Port]->Device = Device;

	/* Setup Port */
	Hc->PortSetup(Hc->Hc, Hc->Ports[Port]);

	/* Sanity */
	if (Hc->Ports[Port]->Connected != 1
		&& Hc->Ports[Port]->Enabled != 1)
		goto DevError;

	/* Set Device Address (Just bind it to the port number + 1 (never set address 0) ) */
	if (UsbFunctionSetAddress(Hc, Port, (uint32_t)(Port + 1)) != TransferFinished)
	{
		/* Try again */
		if (UsbFunctionSetAddress(Hc, Port, (uint32_t)(Port + 1)) != TransferFinished)
		{
			GlbDescriptor->DebugPrint("USB_Handler: (Set_Address) Failed to setup port %u\n", Port);
			goto DevError;
		}
	}

	/* After SetAddress device is allowed 2 ms recovery */
	GlbDescriptor->StallMs(2);

	/* Get Device Descriptor */
	if (UsbFunctionGetDeviceDescriptor(Hc, Port) != TransferFinished)
	{
		/* Try Again */
		if (UsbFunctionGetDeviceDescriptor(Hc, Port) != TransferFinished)
		{
			GlbDescriptor->DebugPrint("USB_Handler: (Get_Device_Desc) Failed to setup port %u\n", Port);
			goto DevError;
		}
	}
	
	/* Get Config Descriptor */
	if (UsbFunctionGetConfigDescriptor(Hc, Port) != TransferFinished)
	{
		/* Try Again */
		if (UsbFunctionGetConfigDescriptor(Hc, Port) != TransferFinished)
		{
			GlbDescriptor->DebugPrint("USB_Handler: (Get_Config_Desc) Failed to setup port %u\n", Port);
			goto DevError;
		}
	}

	/* Set Configuration */
	if (UsbFunctionSetConfiguration(Hc, Port, Hc->Ports[Port]->Device->Configuration) != TransferFinished)
	{
		/* Try Again */
		if (UsbFunctionSetConfiguration(Hc, Port, Hc->Ports[Port]->Device->Configuration) != TransferFinished)
		{
			GlbDescriptor->DebugPrint("USB_Handler: (Set_Configuration) Failed to setup port %u\n", Port);
			goto DevError;
		}
	}

	/* Go through interfaces and add them */
	for (i = 0; i < (int)Hc->Ports[Port]->Device->NumInterfaces; i++)
	{
		/* We want to support Hubs, HIDs and MSDs*/
		//uint32_t IfIndex = (uint32_t)i;

		/* Is this an HID Interface? :> */
		if (Hc->Ports[Port]->Device->Interfaces[i]->Class == X86_USB_CLASS_HID)
		{
			/* Registrate us with HID Manager */
			//UsbHidInit(Hc->Ports[Port]->Device, IfIndex);
		}

		/* Is this an MSD Interface? :> */
		if (Hc->Ports[Port]->Device->Interfaces[i]->Class == X86_USB_CLASS_MSD)
		{
			/* Registrate us with MSD Manager */
			//UsbMsdInit(Hc->Ports[Port]->Device, IfIndex);
		}

		/* Is this an HUB Interface? :> */
		if (Hc->Ports[Port]->Device->Interfaces[i]->Class == X86_USB_CLASS_HUB)
		{
			/* Protocol specifies usb interface (high or low speed) */

			/* Registrate us with Hub Manager */
		}
	}

	/* Done */
	GlbDescriptor->DebugPrint("UsbCore: Setup of port %u done!\n", Port);
	return;

DevError:
	/* Free Control Endpoint */
	GlbDescriptor->MemFree(Device->CtrlEndpoint);

	/* Free Interfaces */
	for (i = 0; i < (int)Device->NumInterfaces; i++)
	{
		/* Free Endpoints */
		if (Device->Interfaces[i]->Endpoints != NULL)
			GlbDescriptor->MemFree(Device->Interfaces[i]->Endpoints);

		/* Free the Interface */
		GlbDescriptor->MemFree(Device->Interfaces[i]);
	}

	/* Free Descriptor Buffer */
	if (Device->Descriptors != NULL)
		GlbDescriptor->MemFree(Device->Descriptors);

	/* Free base */
	GlbDescriptor->MemFree(Device);
}

/* Device Disconnected */
void UsbDeviceDestroy(UsbHc_t *Hc, int Port)
{
	/* Shortcut */
	UsbHcDevice_t *Device = Hc->Ports[Port]->Device;
	int i;

	/* Sanity */
	if (Device == NULL)
		return;

	/* Notify Driver */
	if (Device->Destroy != NULL)
		Device->Destroy((void*)Device);

	/* Free Interfaces */
	for (i = 0; i < (int)Device->NumInterfaces; i++)
	{
		/* Free Endpoints */
		if (Device->Interfaces[i]->Endpoints != NULL)
			GlbDescriptor->MemFree(Device->Interfaces[i]->Endpoints);

		/* Free the Interface */
		GlbDescriptor->MemFree(Device->Interfaces[i]);
	}

	/* Free Descriptor Buffer */
	if (Device->Descriptors != NULL)
		GlbDescriptor->MemFree(Device->Descriptors);

	/* Free base */
	GlbDescriptor->MemFree(Device);

	/* Update Port */
	Hc->Ports[Port]->Connected = 0;
	Hc->Ports[Port]->Enabled = 0;
	Hc->Ports[Port]->FullSpeed = 0;
	Hc->Ports[Port]->Device = NULL;
}

/* Ports */
UsbHcPort_t *UsbPortCreate(int Port)
{
	UsbHcPort_t *HcPort;

	/* Allocate Resources */
	HcPort = GlbDescriptor->MemAlloc(sizeof(UsbHcPort_t));

	/* Get Port Status */
	HcPort->Id = Port;
	HcPort->Connected = 0;
	HcPort->Device = NULL;
	HcPort->Enabled = 0;

	/* Done */
	return HcPort;
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
		GlbDescriptor->SemaphoreP(GlbEventLock);

		/* Pop Event */
		lNode = GlbDescriptor->ListPopFront(GlbUsbEvents);

		/* Sanity */
		if (lNode == NULL)
			continue;

		/* Cast */
		Event = (UsbEvent_t*)lNode->data;

		/* Free the node */
		GlbDescriptor->MemFree(lNode);

		/* Again, sanity */
		if (Event == NULL)
			continue;

		/* Handle Event */
		switch (Event->Type)
		{
			case HcdConnectedEvent:
			{
				/* Setup Device */
				GlbDescriptor->DebugPrint("Setting up Port %i\n", Event->Port);
				UsbDeviceSetup(Event->Controller, Event->Port);

			} break;

			case HcdDisconnectedEvent:
			{
				/* Destroy Device */
				GlbDescriptor->DebugPrint("Destroying Port %i\n", Event->Port);
				UsbDeviceDestroy(Event->Controller, Event->Port);

			} break;

			case HcdRootHubEvent:
			{
				/* Check Ports for Activity */
				Event->Controller->RootHubCheck(Event->Controller->Hc);

			} break;

			default:
			{
				GlbDescriptor->DebugPrint("Unhandled Event: %u on port %i\n", Event->Type, Event->Port);
			} break;
		}
	}
}

/* Gets */
UsbHc_t *UsbGetHcd(uint32_t ControllerId)
{
	return (UsbHc_t*)GlbDescriptor->ListGetDataById(GlbUsbControllers, ControllerId, 0);
}

UsbHcPort_t *UsbGetPort(UsbHc_t *Controller, int Port)
{
	/* Sanity */
	if (Controller == NULL || Port >= (int)Controller->NumPorts)
		return NULL;

	return Controller->Ports[Port];
}