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
* MollenOS X86 USB Core MSD Driver
*/

/* Includes */
#include <Arch.h>
#include <Drivers\Usb\Msd\MsdManager.h>
#include <Semaphore.h>
#include <Heap.h>
#include <List.h>

#include <stdio.h>
#include <string.h>

/* Prototypes */
void UsbMsdDestroy(void *UsbDevice);

/* Initialise Driver for a MSD */
void UsbMsdInit(UsbHcDevice_t *UsbDevice, uint32_t InterfaceIndex)
{
	/* Allocate */
	MsdDevice_t *DevData = (MsdDevice_t*)kmalloc(sizeof(MsdDevice_t));
	uint32_t i;

	/* Debug */
	printf("Msd Device Detected\n");

	/* Sanity */
	if (UsbDevice->Interfaces[InterfaceIndex]->Subclass == X86_USB_MSD_SUBCLASS_FLOPPY)
		DevData->IsUFI = 1;
	else
		DevData->IsUFI = 0;

	/* Set */
	DevData->UsbDevice = UsbDevice;
	DevData->Interface = InterfaceIndex;

	/* Locate neccessary endpoints */
	for (i = 0; i < UsbDevice->Interfaces[InterfaceIndex]->NumEndpoints; i++)
	{
		/* Interrupt? */
		if (UsbDevice->Interfaces[InterfaceIndex]->Endpoints[i]->Type == X86_USB_EP_TYPE_INTERRUPT)
			DevData->EpInterrupt = UsbDevice->Interfaces[InterfaceIndex]->Endpoints[i];
		
		/* Bulk? */
		if (UsbDevice->Interfaces[InterfaceIndex]->Endpoints[i]->Type == X86_USB_EP_TYPE_BULK)
		{
			/* In or out? */
			if (UsbDevice->Interfaces[InterfaceIndex]->Endpoints[i]->Direction == X86_USB_EP_DIRECTION_IN)
				DevData->EpIn = UsbDevice->Interfaces[InterfaceIndex]->Endpoints[i];
			else if (UsbDevice->Interfaces[InterfaceIndex]->Endpoints[i]->Direction == X86_USB_EP_DIRECTION_OUT)
				DevData->EpOut = UsbDevice->Interfaces[InterfaceIndex]->Endpoints[i];
		}
	}

	/* Sanity Ep's */
	if (DevData->EpIn == NULL
		|| DevData->EpOut == NULL)
	{
		printf("Msd is missing either in or out endpoint\n");
		kfree(DevData);
		return;
	}

	/* Reset Toggles */
	DevData->EpIn->Toggle = 0;
	DevData->EpOut->Toggle = 0;

	/* Save Data */
	UsbDevice->Destroy = UsbMsdDestroy;
	UsbDevice->DriverData = (void*)DevData;

	/* Test & Setup Disk */

	/* Reset Bulk */
	if (DevData->IsUFI == 0)
	{
		/* Send control packet */
		UsbFunctionSendPacket((UsbHc_t*)UsbDevice->HcDriver, UsbDevice->Port,
			NULL, X86_USB_REQ_TARGET_CLASS | X86_USB_REQ_TARGET_INTERFACE,
			X86_USB_MSD_REQUEST_RESET, 0, 0, (uint16_t)InterfaceIndex, 0);
	}

	/* Send Inquiry */

	/* Register Us */
	DevData->DeviceId =
		DmCreateDevice("Usb Disk Drive", MCORE_DEVICE_TYPE_STORAGE, (void*)DevData);
}

/* Cleanup */
void UsbMsdDestroy(void *UsbDevice)
{
	/* Cast */
	UsbHcDevice_t *Dev = (UsbHcDevice_t*)UsbDevice;
	MsdDevice_t *DevData = (MsdDevice_t*)Dev->DriverData;

	/* Free Data */

	/* Unregister Us */
	DmDestroyDevice(DevData->DeviceId);
}