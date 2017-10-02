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

/* Includes
 * - System */
#include <os/utils.h>
#include <os/driver/usb.h>
#include "hid.h"

/* Includes
 * - Library */
#include <stdlib.h>

/* HidDeviceCreate
 * Initializes a new hid-device from the given usb-device */
HidDevice_t*
HidDeviceCreate(
    _In_ MCoreUsbDevice_t *UsbDevice)
{
    // Variables
    HidDevice_t *Device = NULL;

	/* Setup vars 
	 * and prepare for a descriptor loop */
	uint8_t *BufPtr = (uint8_t*)UsbDevice->Descriptors;
	size_t BytesLeft = UsbDevice->DescriptorsLength;
	size_t ReportLength = 0;

	/* Needed for parsing */
	UsbHidDescriptor_t *HidDescriptor = NULL;
	uint8_t *ReportDescriptor = NULL;
    size_t i;
    
    // Debug
    TRACE("HidDeviceCreate()");

	// Allocate new resources
    Device = (HidDevice_t*)malloc(sizeof(HidDevice_t));
    memset(Device, 0, sizeof(HidDevice_t));
    memcpy(&Device->Base, UsbDevice, sizeof(MCoreUsbDevice_t));
    Device->Control = &UsbDevice->Endpoints[0];

    // Find neccessary endpoints
    for (i = 1; i < UsbDevice->Interface.Versions[0].EndpointCount; i++) {
        if (UsbDevice->Endpoints[i].Type == EndpointInterrupt) {
            Device->Interrupt = &UsbDevice->Endpoints[i];
            break;
        }
    }

	// Make sure we at-least found an interrupt endpoint
	if (Device->Interrupt == NULL) {
        ERROR("HID Endpoint (In, Interrupt) did not exist.");
		goto Error;
    }
    
    /* Locate the HID descriptor 
	 * TODO: there can be multiple 
	 * hid descriptors, which means 
	 * we must make sure our interface
	 * has been "passed" before selecting */
	i = 0;
	while (BytesLeft > 0)
	{
		/* Cast */
		uint8_t Length = *BufPtr;
		uint8_t Type = *(BufPtr + 1);

		/* Is this a HID descriptor ? */
		if (Type == USB_DESCRIPTOR_TYPE_HID
			&& Length == sizeof(UsbHidDescriptor_t)
			&& i == 1)
		{
			HidDescriptor = (UsbHidDescriptor_t*)BufPtr;
			break;
		}
		else if (Type == USB_DESC_TYPE_INTERFACE
			&& Length == sizeof(UsbInterfaceDescriptor_t)) {
			UsbInterfaceDescriptor_t *_If = (UsbInterfaceDescriptor_t*)BufPtr;
			if ((int)_If->NumInterface == InterfaceIndex)
				i = 1;
		}
		
		/* Next */
		BufPtr += Length;
		BytesLeft -= Length;
	}

	/* Sanity */
	if (HidDescriptor == NULL)
	{
		LogFatal("USBH", "HID Descriptor did not exist.");
		kfree(mDevice);
		kfree(DevData);
		return;
	}

	/* Switch to Report Protocol (ONLY if we are in boot protocol) */
	if (UsbDevice->Interfaces[InterfaceIndex]->Subclass == USB_HID_SUBCLASS_BOOT) {
		UsbFunctionSendPacket((UsbHc_t*)UsbDevice->HcDriver, UsbDevice->Port, 0,
			USB_REQUEST_TARGET_CLASS | USB_REQUEST_TARGET_INTERFACE,
			USB_HID_SET_PROTOCOL, 0, 1, (uint8_t)InterfaceIndex, 0);
	}

	/* Set idle and silence the endpoint unless events */
	/* We might have to set ValueHi to 500 ms for keyboards, but has to be tested
	* time is calculated in 4ms resolution, so 500ms = HiVal = 125 */

	/* This request MAY stall, which means it's unsupported */
	UsbFunctionSendPacket((UsbHc_t*)UsbDevice->HcDriver, UsbDevice->Port, NULL,
		USB_REQUEST_TARGET_CLASS | USB_REQUEST_TARGET_INTERFACE,
		USB_HID_SET_IDLE, 0, 0, (uint8_t)InterfaceIndex, 0);

	/* Get Report Descriptor */
	ReportDescriptor = (uint8_t*)kmalloc(HidDescriptor->ClassDescriptorLength);
	if (UsbFunctionGetDescriptor((UsbHc_t*)UsbDevice->HcDriver, UsbDevice->Port,
		ReportDescriptor, USB_REQUEST_DIR_IN | USB_REQUEST_TARGET_INTERFACE,
		HidDescriptor->ClassDescriptorType,
		0, (uint8_t)InterfaceIndex, HidDescriptor->ClassDescriptorLength) != TransferFinished)
	{
		LogFatal("USBH", "Failed to get Report Descriptor.");
		kfree(mDevice);
		kfree(ReportDescriptor);
		kfree(DevData);
		return;
	}

	/* Parse Report Descriptor */
	DevData->UsbDevice = UsbDevice;
	DevData->Collection = NULL;
	ReportLength = UsbHidParseReportDescriptor(DevData, 
		ReportDescriptor, HidDescriptor->ClassDescriptorLength);

	/* Free the report descriptor, we don't need it anymore */
	kfree(ReportDescriptor);

	/* Adjust if shorter than MPS */
	if (ReportLength < DevData->EpInterrupt->MaxPacketSize)
		ReportLength = DevData->EpInterrupt->MaxPacketSize;

	/* Store length */
	DevData->DataLength = ReportLength;

	/* Reset EP toggle */
	DevData->EpInterrupt->Toggle = 0;

	/* Allocate Interrupt Channel */
	DevData->InterruptChannel = (UsbHcRequest_t*)kmalloc(sizeof(UsbHcRequest_t));
	memset(DevData->InterruptChannel, 0, sizeof(UsbHcRequest_t));

	/* Setup Callback */
	DevData->InterruptChannel->Callback =
		(UsbInterruptCallback_t*)kmalloc(sizeof(UsbInterruptCallback_t));
	DevData->InterruptChannel->Callback->Callback = UsbHidCallback;
	DevData->InterruptChannel->Callback->Args = DevData;

	/* Set driver data */
	DevData->PrevDataBuffer = (uint8_t*)kmalloc(ReportLength);
	DevData->DataBuffer = (uint8_t*)kmalloc(ReportLength);

	/* Memset Databuffers */
	memset(DevData->PrevDataBuffer, 0, ReportLength);
	memset(DevData->DataBuffer, 0, ReportLength);

	/* Some keyboards don't work before their LEDS are set. */

	/* Install Interrupt */
	UsbFunctionInstallPipe(UsbHcd, UsbDevice, DevData->InterruptChannel,
		DevData->EpInterrupt, DevData->DataBuffer, ReportLength);

Error:

    return NULL;
}

/* Cleanup HID driver */
void UsbHidDestroy(void *UsbDevice, int Interface)
{
	/* Cast */
	UsbHcDevice_t *Dev = (UsbHcDevice_t*)UsbDevice;
	MCoreDevice_t *mDevice = 
		(MCoreDevice_t*)Dev->Interfaces[Interface]->DriverData;
	HidDevice_t *Device = (HidDevice_t*)mDevice->Driver.Data;
	UsbHc_t *UsbHcd = (UsbHc_t*)Dev->HcDriver;

	/* Destroy Channel */
	UsbTransactionDestroy(UsbHcd, Device->InterruptChannel);

	/* Unregister Us */
	DmDestroyDevice(Device->DeviceId);

	/* Free endpoints */
	UsbHcd->EndpointDestroy(UsbHcd->Hc, Device->EpInterrupt);

	/* Free Collections */

	/* Free Data */
	kfree(Device->DataBuffer);
	kfree(Device->PrevDataBuffer);

	/* Last cleanup */
	kfree(mDevice->Driver.Data);
}

/* The callback for device-feedback */
void UsbHidCallback(void *Device, UsbTransferStatus_t Status)
{
	/* Vars */
	HidDevice_t *DevData = (HidDevice_t*)Device;

	/* Sanity */
	if (DevData->Collection == NULL
		|| Status == TransferNAK)
		return;

	/* Parse Collection (Recursively) */
	if (!UsbHidApplyCollectionData(DevData, DevData->Collection))
		return;

	/* Now store this in old buffer */
	memcpy(DevData->PrevDataBuffer, DevData->DataBuffer, DevData->DataLength);
}
