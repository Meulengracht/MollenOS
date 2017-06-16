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
#include <string.h>

/* UsbTransferInitialize
 * Initializes the transfer with the given information and sets up
 * all target parameters */
OsStatus_t
UsbTransferInitialize(
	_In_ UsbPort_t *Port,
	_In_ UsbTransferType_t Type,
	_In_ UsbHcEndpointDescriptor_t *Endpoint,
	_Out_ UsbTransfer_t *Transfer)
{
	// Reset entire transfer structure
	memset(Transfer, 0, sizeof(UsbTransfer_t));

	// Set speed, type and endpoint data
	Transfer->Type = Type;
	Transfer->Speed = Port->Speed;
	memcpy(&Transfer->Endpoint, Endpoint, 
		sizeof(UsbHcEndpointDescriptor_t));

	// Done
	return OsSuccess;
}

/* UsbTransactionSetup
 * Never neccessary to pass an index here since it'll always be the 
 * initial transfer. The transfer buffer here is allocated automatically. */
OsStatus_t
UsbTransferSetup(
	_Out_ UsbTransfer_t *Transfer, 
	_In_ UsbPacket_t *Packet)
{
	// Variables
	UsbTransaction_t *Transaction = &Transfer->Transactions[0];

	// Initialize variables
	Transaction->Type = SetupTransaction;

	// Allocate some buffer space
	
	// Copy packet data to buffer

	// Done
	return OsSuccess;
}

/* UsbTransferIn */
OsStatus_t
UsbTransferIn(
	_Out_ UsbTransfer_t *Transfer,
	_In_ int Index,
	_In_ uintptr_t BufferAddress, 
	_In_ size_t Length,
	_In_ int Handshake)
{
	// Variables
	UsbTransaction_t *Transaction = &Transfer->Transactions[Index];

	// Initialize variables
	Transaction->Type = InTransaction;
	Transaction->Handshake = Handshake;
	Transaction->BufferAddress = BufferAddress;
	Transaction->Length = Length;

	// Zero-length?
	if (Length == 0) {
		Transaction->ZeroLength = 1;
	}

	// Done
	return OsSuccess;
}

/* UsbTransferOut */
OsStatus_t
UsbTransferOut(
	_Out_ UsbTransfer_t *Transfer,
	_In_ int Index,
	_In_ uintptr_t BufferAddress, 
	_In_ size_t Length,
	_In_ int Handshake)
{
	// Variables
	UsbTransaction_t *Transaction = &Transfer->Transactions[Index];

	// Initialize variables
	Transaction->Type = OutTransaction;
	Transaction->Handshake = Handshake;
	Transaction->BufferAddress = BufferAddress;
	Transaction->Length = Length;

	// Zero-length?
	if (Length == 0) {
		Transaction->ZeroLength = 1;
	}

	// Done
	return OsSuccess;
}

/* UsbTransferSend
 *  */
OsStatus_t
UsbTransferSend(
	_In_ UsbController_t *Controller, 
	_In_ UsbTransfer_t *Transfer,
	_Out_ UsbTransferResult_t *Result)
{
	// Variables
	UsbTransferResult_t Result;

	// Build pipe-id

	// Send
	return UsbQueueTransfer(Controller->Driver, Controller->Device,
		0, Transfer, &Result);
}

/* Cleanup Transaction */
void UsbTransactionDestroy(UsbHc_t *Hc, UsbHcRequest_t *Request)
{
	/* Cast */
	UsbHcTransaction_t *Transaction = Request->Transactions, *NextTransaction;

	/* Destroy */
	Hc->TransactionDestroy(Hc->Hc, Request);

	/* Free List */
	while (Transaction)
	{
		/* Get Next */
		NextTransaction = Transaction->Link;

		/* Free */
		kfree(Transaction);

		/* Set next */
		Transaction = NextTransaction;
	}
}

/* Set address of an usb device */
UsbTransferStatus_t
UsbFunctionSetAddress(
	UsbHc_t *Hc, 
	int Port, 
	size_t Address)
{
	/* Vars */
	UsbHcRequest_t Request = { 0 };
	UsbPacket_t Packet = { 0 };

	/* Setup Packet */
	Packet.Direction = USB_REQUEST_DIR_OUT;
	Packet.Type = USB_REQUEST_SET_ADDR;
	Packet.ValueLo = (uint8_t)(Address & 0xFF);
	Packet.ValueHi = 0;
	Packet.Index = 0;
	Packet.Length = 0;

	/* Init transfer */
	UsbTransactionInit(Hc, &Request, ControlTransfer,
		Hc->Ports[Port]->Device, Hc->Ports[Port]->Device->CtrlEndpoint);

	/* Setup Transfer */
	UsbTransactionSetup(Hc, &Request, &Packet);

	/* ACK Transfer */
	UsbTransactionIn(Hc, &Request, 1, NULL, 0);

	/* Send it */
	UsbTransactionSend(Hc, &Request);

	/* Cleanup */
	UsbTransactionDestroy(Hc, &Request);

	/* Check if it completed */
	if (Request.Status == TransferFinished)
		Request.Device->Address = Address;

	return Request.Status;
}

/* Gets the device descriptor */
UsbTransferStatus_t UsbFunctionGetDeviceDescriptor(UsbHc_t *Hc, int Port)
{
	/* Vars */
	UsbDeviceDescriptor_t DevInfo;
	UsbHcRequest_t Request = { 0 };
	UsbPacket_t Packet = { 0 };

	/* Setup Packet */
	Packet.Direction = USB_REQUEST_DIR_IN;
	Packet.Type = USB_REQUEST_GET_DESC;
	Packet.ValueHi = USB_DESC_TYPE_DEVICE;
	Packet.ValueLo = 0;
	Packet.Index = 0;
	Packet.Length = 0x12;		/* Max Descriptor Length is 18 bytes */

	/* Init transfer */
	UsbTransactionInit(Hc, &Request, ControlTransfer,
		Hc->Ports[Port]->Device, Hc->Ports[Port]->Device->CtrlEndpoint);
	
	/* Setup Transfer */
	UsbTransactionSetup(Hc, &Request, &Packet);

	/* In Transfer, we want to fill the descriptor */
	UsbTransactionIn(Hc, &Request, 0, &DevInfo, 0x12);

	/* Out Transfer, STATUS Stage */
	UsbTransactionOut(Hc, &Request, 1, NULL, 0);

	/* Send it */
	UsbTransactionSend(Hc, &Request);

	/* Update Device Information */
	if (Request.Status == TransferFinished)
	{
		LogInformation("USBC", "USB Length 0x%x - Device Vendor Id & Product Id: 0x%x - 0x%x", DevInfo.Length, DevInfo.VendorId, DevInfo.ProductId);
		LogInformation("USBC", "Device Configurations 0x%x, Max Packet Size: 0x%x", DevInfo.NumConfigurations, DevInfo.MaxPacketSize);

		Hc->Ports[Port]->Device->Class = DevInfo.Class;
		Hc->Ports[Port]->Device->Subclass = DevInfo.Subclass;
		Hc->Ports[Port]->Device->Protocol = DevInfo.Protocol;
		Hc->Ports[Port]->Device->VendorId = DevInfo.VendorId;
		Hc->Ports[Port]->Device->ProductId = DevInfo.ProductId;
		Hc->Ports[Port]->Device->StrIndexManufactor = DevInfo.StrIndexManufactor;
		Hc->Ports[Port]->Device->StrIndexProduct = DevInfo.StrIndexProduct;
		Hc->Ports[Port]->Device->StrIndexSerialNum = DevInfo.StrIndexSerialNum;
		Hc->Ports[Port]->Device->NumConfigurations = DevInfo.NumConfigurations;
		Hc->Ports[Port]->Device->MaxPacketSize = DevInfo.MaxPacketSize;
		
		/* Set MPS */
		Hc->Ports[Port]->Device->CtrlEndpoint->MaxPacketSize = DevInfo.MaxPacketSize;
	}

	/* Cleanup */
	UsbTransactionDestroy(Hc, &Request);

	/* Done! */
	return Request.Status;
}

/* Gets the initial config descriptor */
UsbTransferStatus_t UsbFunctionGetInitialConfigDescriptor(UsbHc_t *Hc, int Port)
{
	/* Vars */
	UsbConfigDescriptor_t DevConfig;
	UsbHcRequest_t Request = { 0 };
	UsbPacket_t Packet = { 0 };

	/* Setup Packet */
	Packet.Direction = USB_REQUEST_DIR_IN;
	Packet.Type = USB_REQUEST_GET_DESC;
	Packet.ValueHi = USB_DESC_TYPE_CONFIG;
	Packet.ValueLo = 0;
	Packet.Index = 0;
	Packet.Length = sizeof(UsbConfigDescriptor_t);

	/* Step 1. Get configuration descriptor */

	/* Init transfer */
	UsbTransactionInit(Hc, &Request, ControlTransfer,
		Hc->Ports[Port]->Device, Hc->Ports[Port]->Device->CtrlEndpoint);

	/* Setup Transfer */
	UsbTransactionSetup(Hc, &Request, &Packet);

	/* In Transfer, we want to fill the descriptor */
	UsbTransactionIn(Hc, &Request, 0, &DevConfig, sizeof(UsbConfigDescriptor_t));

	/* Out Transfer, STATUS Stage */
	UsbTransactionOut(Hc, &Request, 1, NULL, 0);

	/* Send it */
	UsbTransactionSend(Hc, &Request);

	/* Complete ? */
	if (Request.Status == TransferFinished)
	{
		Hc->Ports[Port]->Device->Configuration = DevConfig.ConfigurationValue;
		Hc->Ports[Port]->Device->ConfigMaxLength = DevConfig.TotalLength;
		Hc->Ports[Port]->Device->NumInterfaces = DevConfig.NumInterfaces;
		Hc->Ports[Port]->Device->MaxPowerConsumption = (uint16_t)(DevConfig.MaxPowerConsumption * 2);
	}

	/* Cleanup */
	UsbTransactionDestroy(Hc, &Request);

	/* Done */
	return Request.Status;
}

/* Gets the config descriptor */
UsbTransferStatus_t UsbFunctionGetConfigDescriptor(UsbHc_t *Hc, int Port)
{
	/* Vars */
	UsbHcRequest_t Request = { 0 };
	UsbPacket_t Packet = { 0 };
	void *Buffer;

	/* Step 1. Get configuration descriptor */
	Request.Status = UsbFunctionGetInitialConfigDescriptor(Hc, Port);
	if (Request.Status != TransferFinished)
		return Request.Status;
	
	/* Step 2. Get FULL descriptor */
	Buffer = kmalloc(Hc->Ports[Port]->Device->ConfigMaxLength);
	
	/* Setup Packet */
	Packet.Direction = USB_REQUEST_DIR_IN;
	Packet.Type = USB_REQUEST_GET_DESC;
	Packet.ValueHi = USB_DESC_TYPE_CONFIG;
	Packet.ValueLo = 0;
	Packet.Index = 0;
	Packet.Length = Hc->Ports[Port]->Device->ConfigMaxLength;

	/* Init transfer */
	UsbTransactionInit(Hc, &Request, ControlTransfer,
		Hc->Ports[Port]->Device, Hc->Ports[Port]->Device->CtrlEndpoint);

	/* Setup Transfer */
	UsbTransactionSetup(Hc, &Request, &Packet);

	/* In Transfer, we want to fill the descriptor */
	UsbTransactionIn(Hc, &Request, 0, Buffer, Hc->Ports[Port]->Device->ConfigMaxLength);

	/* Out Transfer, STATUS Stage */
	UsbTransactionOut(Hc, &Request, 1, NULL, 0);

	/* Send it */
	UsbTransactionSend(Hc, &Request);

	/* Completed ? */
	if (Request.Status == TransferFinished)
	{
		/* Iteration Vars */
		uint8_t *BufPtr = (uint8_t*)Buffer;
		int BytesLeft = (int)Hc->Ports[Port]->Device->ConfigMaxLength;
		size_t EpIterator = 1;
		int CurrentIfVersion = 0;
		
		/* Set Initial */
		Hc->Ports[Port]->Device->NumInterfaces = 0;
		Hc->Ports[Port]->Device->Descriptors = Buffer;
		Hc->Ports[Port]->Device->DescriptorsLength = Hc->Ports[Port]->Device->ConfigMaxLength;

		/* Parse Interface & Endpoints */
		while (BytesLeft > 0)
		{
			/* Cast */
			uint8_t length = *BufPtr;
			uint8_t type = *(BufPtr + 1);

			/* Is this an interface or endpoint? :O */
			if (length == sizeof(UsbInterfaceDescriptor_t)
				&& type == USB_DESC_TYPE_INTERFACE)
			{
				/* Vars */
				UsbInterfaceDescriptor_t *Interface = (UsbInterfaceDescriptor_t*)BufPtr;
				UsbHcInterface_t *UsbInterface;
				UsbHcInterfaceVersion_t *UsbIfVersion;

				/* Sanity */
				if (Hc->Ports[Port]->Device->Interfaces[Interface->NumInterface] == NULL)
				{
					/* Allocate */
					UsbInterface = (UsbHcInterface_t*)kmalloc(sizeof(UsbHcInterface_t));

					/* Reset */
					memset(UsbInterface, 0, sizeof(UsbHcInterface_t));

					/* Setup */
					UsbInterface->Id = Interface->NumInterface;
					UsbInterface->Class = Interface->Class;
					UsbInterface->Subclass = Interface->Subclass;
					UsbInterface->Protocol = Interface->Protocol;
					UsbInterface->StrIndex = Interface->StrIndexInterface;

					/* Update Device */
					Hc->Ports[Port]->Device->Interfaces[Hc->Ports[Port]->Device->NumInterfaces] = UsbInterface;
					Hc->Ports[Port]->Device->NumInterfaces++;
				}
				else
					UsbInterface = Hc->Ports[Port]->Device->Interfaces[Interface->NumInterface];

				/* Sanity the Version */
				if (UsbInterface->Versions[Interface->AlternativeSetting] == NULL)
				{
					/* Allocate */
					UsbIfVersion = (UsbHcInterfaceVersion_t*)kmalloc(sizeof(UsbHcInterfaceVersion_t));

					/* Reset */
					memset(UsbIfVersion, 0, sizeof(UsbHcInterfaceVersion_t));

					/* Debug */
					LogInformation("USBC", "Interface %u.%u - Endpoints %u (Class %u, Subclass %u, Protocol %u)",
						Interface->NumInterface, Interface->AlternativeSetting, Interface->NumEndpoints, Interface->Class,
						Interface->Subclass, Interface->Protocol);

					/* Setup */
					UsbIfVersion->NumEndpoints = Interface->NumEndpoints;

					/* Allocate */
					CurrentIfVersion = Interface->AlternativeSetting;
					EpIterator = 0;

					if (UsbIfVersion->NumEndpoints != 0)
						UsbIfVersion->Endpoints =
							(UsbHcEndpoint_t**)kmalloc(sizeof(UsbHcEndpoint_t*) * Interface->NumEndpoints);
					else
						UsbIfVersion->Endpoints = NULL;

					/* Update */
					UsbInterface->Versions[Interface->AlternativeSetting] = UsbIfVersion;
				}

			}
			else if ((length == 7 || length == 9)
				&& type == USB_DESC_TYPE_ENDPOINT)
			{
				/* Null Ep's ? ?*/
				if (Hc->Ports[Port]->Device->NumInterfaces == 0)
					goto NextEntry;

				/* Cast */
				UsbEndpointDescriptor_t *Ep = (UsbEndpointDescriptor_t*)BufPtr;
				UsbHcEndpoint_t *HcdEp = (UsbHcEndpoint_t*)kmalloc(sizeof(UsbHcEndpoint_t));

				/* Get Info */
				size_t EpAddr = Ep->Address & 0xF;
				UsbHcEndpointType_t EpType = (UsbHcEndpointType_t)(Ep->Attributes & 0x3);

				/* Debug */
				LogInformation("USBC", "Endpoint %u (%s) - Attributes 0x%x (MaxPacketSize 0x%x)",
					EpAddr, ((Ep->Address & 0x80) != 0 ? "IN" : "OUT"), Ep->Attributes, Ep->MaxPacketSize);

				/* Update Device */
				HcdEp->Address = EpAddr;
				HcdEp->MaxPacketSize = (Ep->MaxPacketSize & 0x7FF);
				HcdEp->Bandwidth = ((Ep->MaxPacketSize >> 11) & 0x3) + 1;
				HcdEp->Interval = Ep->Interval;
				HcdEp->Toggle = 0;
				HcdEp->Type = EpType;

				/* In or Out? */
				if (Ep->Address & 0x80)
					HcdEp->Direction = USB_EP_DIRECTION_IN;
				else
					HcdEp->Direction = USB_EP_DIRECTION_OUT;

				/* Sanity */
				if (Hc->Ports[Port]->Device->Interfaces[
					Hc->Ports[Port]->Device->NumInterfaces - 1]->
						Versions[CurrentIfVersion]->NumEndpoints < (EpIterator + 1))
				{
					/* The fuck?? -.- */
					if (Hc->Ports[Port]->Device->Interfaces[
						Hc->Ports[Port]->Device->NumInterfaces - 1]->
							Versions[CurrentIfVersion]->Endpoints == NULL)
					{
						/* Dummy Allocate */
						Hc->Ports[Port]->Device->Interfaces[
							Hc->Ports[Port]->Device->NumInterfaces - 1]->
								Versions[CurrentIfVersion]->Endpoints =
								(UsbHcEndpoint_t**)kmalloc(sizeof(UsbHcEndpoint_t*) * 16);
					}

					/* Increament */
					Hc->Ports[Port]->Device->Interfaces[
						Hc->Ports[Port]->Device->NumInterfaces - 1]->
							Versions[CurrentIfVersion]->NumEndpoints++;
				}

				/* Set */
				Hc->Ports[Port]->Device->Interfaces[
					Hc->Ports[Port]->Device->NumInterfaces - 1]->
						Versions[CurrentIfVersion]->Endpoints[EpIterator] = HcdEp;

				/* Next */
				EpIterator++;
			}

			/* Increase */
		NextEntry:
			BufPtr += length;
			BytesLeft -= (int32_t)length;
		}
	}
	else
	{
		/* Cleanup */
		kfree(Buffer);
		Hc->Ports[Port]->Device->Descriptors = NULL;
	}

	/* Cleanup */
	UsbTransactionDestroy(Hc, &Request);

	/* Done */
	return Request.Status;
}

/* Set configuration of an usb device */
UsbTransferStatus_t UsbFunctionSetConfiguration(UsbHc_t *Hc, int Port, size_t Configuration)
{
	/* Vars */
	UsbHcRequest_t Request = { 0 };
	UsbPacket_t Packet = { 0 };

	/* Setup Packet */
	Packet.Direction = USB_REQUEST_DIR_OUT;
	Packet.Type = USB_REQUEST_SET_CONFIG;
	Packet.ValueHi = 0;
	Packet.ValueLo = (Configuration & 0xFF);
	Packet.Index = 0;
	Packet.Length = 0;		/* We do not want data */

	/* Init transfer */
	UsbTransactionInit(Hc, &Request, ControlTransfer,
		Hc->Ports[Port]->Device, Hc->Ports[Port]->Device->CtrlEndpoint);

	/* Setup Transfer */
	UsbTransactionSetup(Hc, &Request, &Packet);

	/* ACK Transfer */
	UsbTransactionIn(Hc, &Request, 1, NULL, 0);

	/* Send it */
	UsbTransactionSend(Hc, &Request);

	/* Cleanup */
	UsbTransactionDestroy(Hc, &Request);

	/* Done */
	return Request.Status;
}

/* Gets the device string language descriptors (Index 0) */
UsbTransferStatus_t UsbFunctionGetStringLanguages(UsbHc_t *Hc, int Port)
{
	/* Vars */
	UsbStringDescriptor_t StringDesc;
	UsbHcRequest_t Request = { 0 };
	UsbPacket_t Packet = { 0 };

	/* Setup Packet */
	Packet.Direction = USB_REQUEST_DIR_IN;
	Packet.Type = USB_REQUEST_GET_DESC;
	Packet.ValueHi = USB_DESC_TYPE_STRING;
	Packet.ValueLo = 0;
	Packet.Index = 0;
	Packet.Length = sizeof(UsbStringDescriptor_t);

	/* Init transfer */
	UsbTransactionInit(Hc, &Request, ControlTransfer,
		Hc->Ports[Port]->Device, Hc->Ports[Port]->Device->CtrlEndpoint);

	/* Setup Transfer */
	UsbTransactionSetup(Hc, &Request, &Packet);

	/* In Transfer, we want to fill the descriptor */
	UsbTransactionIn(Hc, &Request, 0, &StringDesc, sizeof(UsbStringDescriptor_t));

	/* Out Transfer, STATUS Stage */
	UsbTransactionOut(Hc, &Request, 1, NULL, 0);

	/* Send it */
	UsbTransactionSend(Hc, &Request);

	/* Update Device Information */
	if (Request.Status == TransferFinished)
	{
		/* Build a list of available languages */
		Hc->Ports[Port]->Device->NumLanguages = (StringDesc.Length - 2) / 2;

		/* Allocate List */
		if (Hc->Ports[Port]->Device->NumLanguages > 0) {
			uint16_t *LangList = (uint16_t*)kmalloc(2 * Hc->Ports[Port]->Device->NumLanguages);
			int i;
			for (i = 0; i < Hc->Ports[Port]->Device->NumLanguages; i++)
				LangList[i] = StringDesc.WString[i];
		}
	}

	/* Cleanup */
	UsbTransactionDestroy(Hc, &Request);

	/* Done */
	return Request.Status;
}

/* Gets a string descriptor */
UsbTransferStatus_t UsbFunctionGetStringDescriptor(UsbHc_t *Hc, 
	int Port, size_t LanguageId, size_t StringIndex, char *StrBuffer)
{
	/* Vars */
	UsbHcRequest_t Request = { 0 };
	UsbPacket_t Packet = { 0 };
	char TempBuffer[64];

	/* Setup Packet */
	Packet.Direction = USB_REQUEST_DIR_IN;
	Packet.Type = USB_REQUEST_GET_DESC;
	Packet.ValueHi = USB_DESC_TYPE_STRING;
	Packet.ValueLo = (uint8_t)StringIndex;
	Packet.Index = (uint16_t)LanguageId;
	Packet.Length = 64;

	/* Init transfer */
	UsbTransactionInit(Hc, &Request, ControlTransfer,
		Hc->Ports[Port]->Device, Hc->Ports[Port]->Device->CtrlEndpoint);

	/* Setup Transfer */
	UsbTransactionSetup(Hc, &Request, &Packet);

	/* In Transfer, we want to fill the descriptor */
	UsbTransactionIn(Hc, &Request, 0, TempBuffer, 64);

	/* Out Transfer, STATUS Stage */
	UsbTransactionOut(Hc, &Request, 1, NULL, 0);

	/* Send it */
	UsbTransactionSend(Hc, &Request);

	/* Update Device Information */
	if (Request.Status == TransferFinished)
	{
		/* Convert to Utf8 */
		//size_t StringLength = (*((uint8_t*)TempBuffer + 1) - 2);
		_CRT_UNUSED(StrBuffer);

		/* Create a MString */
	}

	/* Cleanup */
	UsbTransactionDestroy(Hc, &Request);

	/* Done */
	return Request.Status;
}

/* Clear Feature */
UsbTransferStatus_t UsbFunctionClearFeature(UsbHc_t *Hc, int Port,
	uint8_t Target, uint16_t Index, uint16_t Feature)
{
	/* Vars */
	UsbHcRequest_t Request = { 0 };
	UsbPacket_t Packet = { 0 };

	/* Setup Packet */
	Packet.Direction = Target;
	Packet.Type = USB_REQUEST_CLR_FEATURE;
	Packet.ValueHi = ((Feature >> 8) & 0xFF);
	Packet.ValueLo = (Feature & 0xFF);
	Packet.Index = Index;
	Packet.Length = 0;

	/* Init transfer */
	UsbTransactionInit(Hc, &Request, ControlTransfer,
		Hc->Ports[Port]->Device, Hc->Ports[Port]->Device->CtrlEndpoint);

	/* Setup Transfer */
	UsbTransactionSetup(Hc, &Request, &Packet);

	/* ACK Transfer */
	UsbTransactionIn(Hc, &Request, 1, NULL, 0);

	/* Send it */
	UsbTransactionSend(Hc, &Request);

	/* Cleanup */
	UsbTransactionDestroy(Hc, &Request);

	/* Done */
	return Request.Status;
}

/* Set Feature */
UsbTransferStatus_t UsbFunctionSetFeature(UsbHc_t *Hc, int Port,
	uint8_t Target, uint16_t Index, uint16_t Feature)
{
	/* Vars */
	UsbHcRequest_t Request = { 0 };
	UsbPacket_t Packet = { 0 };

	/* Setup Packet */
	Packet.Direction = Target;
	Packet.Type = USB_REQUEST_SET_FEATURE;
	Packet.ValueHi = ((Feature >> 8) & 0xFF);
	Packet.ValueLo = (Feature & 0xFF);
	Packet.Index = Index;
	Packet.Length = 0;

	/* Init transfer */
	UsbTransactionInit(Hc, &Request, ControlTransfer,
		Hc->Ports[Port]->Device, Hc->Ports[Port]->Device->CtrlEndpoint);

	/* Setup Transfer */
	UsbTransactionSetup(Hc, &Request, &Packet);

	/* ACK Transfer */
	UsbTransactionIn(Hc, &Request, 1, NULL, 0);

	/* Send it */
	UsbTransactionSend(Hc, &Request);

	/* Cleanup */
	UsbTransactionDestroy(Hc, &Request);

	/* Done! */
	return Request.Status;
}

/* Get specific descriptor */
UsbTransferStatus_t UsbFunctionGetDescriptor(UsbHc_t *Hc, int Port, void *Buffer, uint8_t Direction,
	uint8_t DescriptorType, uint8_t SubType, uint8_t DescriptorIndex, uint16_t DescriptorLength)
{
	/* Vars */
	UsbHcRequest_t Request = { 0 };
	UsbPacket_t Packet = { 0 };

	/* Setup Packet */
	Packet.Direction = Direction;
	Packet.Type = USB_REQUEST_GET_DESC;
	Packet.ValueHi = DescriptorType;
	Packet.ValueLo = SubType;
	Packet.Index = DescriptorIndex;
	Packet.Length = DescriptorLength;

	/* Init transfer */
	UsbTransactionInit(Hc, &Request, ControlTransfer,
		Hc->Ports[Port]->Device, Hc->Ports[Port]->Device->CtrlEndpoint);

	/* Setup Transfer */
	UsbTransactionSetup(Hc, &Request, &Packet);

	/* In Transfer, we want to fill the descriptor */
	UsbTransactionIn(Hc, &Request, 0, Buffer, DescriptorLength);

	/* Out Transfer, STATUS Stage */
	UsbTransactionOut(Hc, &Request, 1, NULL, 0);

	/* Send it */
	UsbTransactionSend(Hc, &Request);

	/* Cleanup */
	UsbTransactionDestroy(Hc, &Request);

	/* Done! */
	return Request.Status;
}

/* Send packet */
UsbTransferStatus_t UsbFunctionSendPacket(UsbHc_t *Hc, int Port, void *Buffer, uint8_t RequestType,
	uint8_t pRequest, uint8_t ValueHi, uint8_t ValueLo, uint16_t Index, uint16_t Length)
{
	/* Vars */
	UsbHcRequest_t Request = { 0 };
	UsbPacket_t Packet = { 0 };

	/* Setup Packet */
	Packet.Direction = RequestType;
	Packet.Type = pRequest;
	Packet.ValueHi = ValueHi;
	Packet.ValueLo = ValueLo;
	Packet.Index = Index;
	Packet.Length = Length;

	/* Init transfer */
	UsbTransactionInit(Hc, &Request, ControlTransfer,
		Hc->Ports[Port]->Device, Hc->Ports[Port]->Device->CtrlEndpoint);

	/* Setup Transfer */
	UsbTransactionSetup(Hc, &Request, &Packet);

	/* In/Out Transfer, we want to fill data */
	if (Length != 0)
	{
		if (RequestType & USB_REQUEST_DIR_IN)
		{
			UsbTransactionIn(Hc, &Request, 0, Buffer, Length);

			/* Out Transfer, STATUS Stage */
			UsbTransactionOut(Hc, &Request, 1, NULL, 0);
		}
			
		else
		{
			UsbTransactionOut(Hc, &Request, 0, Buffer, Length);

			/* In Transfer, ACK Stage */
			UsbTransactionIn(Hc, &Request, 1, NULL, 0);
		}
			
	}
	else
	{
		/* In Transfer, ACK Stage */
		UsbTransactionIn(Hc, &Request, 1, NULL, 0);
	}
	
	/* Send it */
	UsbTransactionSend(Hc, &Request);

	/* Cleanup */
	UsbTransactionDestroy(Hc, &Request);

	/* Done! */
	return Request.Status;
}

/* Install Interrupt Pipe */
void UsbFunctionInstallPipe(UsbHc_t *Hc, UsbHcDevice_t *Device, UsbHcRequest_t *Request, 
	UsbHcEndpoint_t *Endpoint, void *Buffer, size_t Length)
{
	/* Setup Transfer */
	UsbTransactionInit(Hc, Request, InterruptTransfer, Device, Endpoint);

	/* Add in transfers */
	if (Endpoint->Direction == USB_EP_DIRECTION_IN)
		UsbTransactionIn(Hc, Request, 0, Buffer, Length);
	else
		UsbTransactionOut(Hc, Request, 0, Buffer, Length);

	/* Initiate the transfers */
	UsbTransactionSend(Hc, Request);
}