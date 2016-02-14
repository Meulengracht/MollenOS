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
* MollenOS USB Functions Core Driver
*/

/* Includes */
#include <UsbCore.h>
#include <Module.h>

/* Kernel */
#include <Heap.h>
#include <Log.h>

/* CLib */
#include <string.h>

/* Transaction List Functions */
void UsbTransactionAppend(UsbHcRequest_t *Request, UsbHcTransaction_t *Transaction)
{
	if (Request->Transactions == NULL)
		Request->Transactions = Transaction;
	else
	{
		UsbHcTransaction_t *Head = Request->Transactions;

		/* Go to last element */
		while (Head->Link != NULL)
			Head = Head->Link;

		/* Append */
		Head->Link = Transaction;
		Transaction->Link = NULL;
	}
}

/* Transaction Wrappers */
void UsbTransactionInit(UsbHc_t *Hc, UsbHcRequest_t *Request, UsbTransferType_t Type,
	UsbHcDevice_t *Device, UsbHcEndpoint_t *Endpoint)
{
	/* Control - Endpoint 0 */
	Request->Type = Type;
	Request->Data = NULL;
	Request->Device = Device;
	Request->Endpoint = Endpoint;
	Request->Speed = Hc->Ports[Device->Port]->Speed;
	Request->Transactions = NULL;

	/* Perform */
	Hc->TransactionInit(Hc->Hc, Request);
}

/* The Setup Transaction */
void UsbTransactionSetup(UsbHc_t *Hc, UsbHcRequest_t *Request, size_t PacketSize)
{
	UsbHcTransaction_t *Transaction;

	/* Set toggle and token-bytes */
	Request->TokenBytes = PacketSize;
	Request->Endpoint->Toggle = 0;

	/* Perform it */
	Transaction = Hc->TransactionSetup(Hc->Hc, Request);

	/* Append it */
	Transaction->Type = SetupTransaction;
	UsbTransactionAppend(Request, Transaction);

	/* Toggle Goggle */
	Request->Endpoint->Toggle = 1;
}

/* The In Data Transaction */
void UsbTransactionIn(UsbHc_t *Hc, UsbHcRequest_t *Request, int Handshake, void *Buffer, size_t Length)
{
	/* Get length */
	UsbHcTransaction_t *Transaction;

	/* Either it's maxpacket size or remaining len */
	size_t FixedLen = MIN(Request->Endpoint->MaxPacketSize, Length);

	/* Calculate rest */
	int RemainingLen = Length - FixedLen;
	size_t TransfersLeft = 0;

	/* Sanity */
	if (RemainingLen <= 0)
	{
		RemainingLen = 0;
		TransfersLeft = 0;
	}
	else
	{
		TransfersLeft = RemainingLen / Request->Endpoint->MaxPacketSize;
		
		/* Fix transfers */
		if (RemainingLen % Request->Endpoint->MaxPacketSize)
			TransfersLeft++;
	}
	
	/* Set request io buffer */
	Request->IoBuffer = Buffer;
	Request->IoLength = FixedLen;

	/* Sanity, ACK */
	if (Handshake)
		Request->Endpoint->Toggle = 1;

	/* Perform */
	Transaction = Hc->TransactionIn(Hc->Hc, Request);

	/* Append Transaction */
	Transaction->Type = InTransaction;
	UsbTransactionAppend(Request, Transaction);

	/* Toggle Goggle */
	Request->Endpoint->Toggle = (Request->Endpoint->Toggle == 0) ? 1 : 0;

	/* Do we need more transfers? */
	if (TransfersLeft > 0)
		UsbTransactionIn(Hc, Request, Request->Endpoint->Toggle,
		(void*)((size_t)Buffer + FixedLen), RemainingLen);
}

/* The Out Data Transaction */
void UsbTransactionOut(UsbHc_t *Hc, UsbHcRequest_t *Request, int Handshake, void *Buffer, size_t Length)
{
	/* Get length */
	UsbHcTransaction_t *Transaction;
	size_t FixedLen = MIN(Request->Endpoint->MaxPacketSize, Length);

	/* Calculate rest */
	int RemainingLen = Length - FixedLen;
	size_t TransfersLeft = 0;

	/* Sanity */
	if (RemainingLen <= 0)
	{
		RemainingLen = 0;
		TransfersLeft = 0;
	}
	else
	{
		TransfersLeft = RemainingLen / Request->Endpoint->MaxPacketSize;

		/* Fix transfers */
		if (RemainingLen % Request->Endpoint->MaxPacketSize)
			TransfersLeft++;
	}

	/* Set request io buffer */
	Request->IoBuffer = Buffer;
	Request->IoLength = FixedLen;

	/* Sanity, ACK packet */
	if (Handshake)
		Request->Endpoint->Toggle = 1;

	/* Perform */
	Transaction = Hc->TransactionOut(Hc->Hc, Request);

	/* Append Transaction */
	Transaction->Type = OutTransaction;
	UsbTransactionAppend(Request, Transaction);

	/* Toggle Goggle */
	Request->Endpoint->Toggle = (Request->Endpoint->Toggle == 0) ? 1 : 0;

	/* Check up on this ! !*/
	if (TransfersLeft > 0)
		UsbTransactionOut(Hc, Request, Request->Endpoint->Toggle,
		(void*)((size_t)Buffer + FixedLen), RemainingLen);
}

/* Send to Device */
void UsbTransactionSend(UsbHc_t *Hc, UsbHcRequest_t *Request)
{
	/* Perform */
	Hc->TransactionSend(Hc->Hc, Request);	
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
UsbTransferStatus_t UsbFunctionSetAddress(UsbHc_t *Hc, int Port, size_t Address)
{
	UsbHcRequest_t Request;

	/* Init transfer */
	UsbTransactionInit(Hc, &Request, ControlTransfer,
		Hc->Ports[Port]->Device, Hc->Ports[Port]->Device->CtrlEndpoint);

	/* Setup Packet */
	Request.Packet.Direction = USB_REQUEST_DIR_OUT;
	Request.Packet.Type = USB_REQUEST_SET_ADDR;
	Request.Packet.ValueLo = (Address & 0xFF);
	Request.Packet.ValueHi = 0;
	Request.Packet.Index = 0;
	Request.Packet.Length = 0;		/* We do not want data */

	/* Setup Transfer */
	UsbTransactionSetup(Hc, &Request, sizeof(UsbPacket_t));

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
	UsbHcRequest_t Request;

	/* Init transfer */
	UsbTransactionInit(Hc, &Request, ControlTransfer,
		Hc->Ports[Port]->Device, Hc->Ports[Port]->Device->CtrlEndpoint);
	
	/* Setup Packet */
	Request.Packet.Direction = USB_REQUEST_DIR_IN;
	Request.Packet.Type = USB_REQUEST_GET_DESC;
	Request.Packet.ValueHi = USB_DESC_TYPE_DEVICE;
	Request.Packet.ValueLo = 0;
	Request.Packet.Index = 0;
	Request.Packet.Length = 0x12;		/* Max Descriptor Length is 18 bytes */

	/* Setup Transfer */
	UsbTransactionSetup(Hc, &Request, sizeof(UsbPacket_t));

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
	UsbHcRequest_t Request;
	UsbConfigDescriptor_t DevConfig;

	/* Step 1. Get configuration descriptor */

	/* Init transfer */
	UsbTransactionInit(Hc, &Request, ControlTransfer,
		Hc->Ports[Port]->Device, Hc->Ports[Port]->Device->CtrlEndpoint);

	/* Setup Packet */
	Request.Packet.Direction = USB_REQUEST_DIR_IN;
	Request.Packet.Type = USB_REQUEST_GET_DESC;
	Request.Packet.ValueHi = USB_DESC_TYPE_CONFIG;
	Request.Packet.ValueLo = 0;
	Request.Packet.Index = 0;
	Request.Packet.Length = sizeof(UsbConfigDescriptor_t);

	/* Setup Transfer */
	UsbTransactionSetup(Hc, &Request, sizeof(UsbPacket_t));

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
	UsbHcRequest_t Request;
	void *Buffer;

	/* Step 1. Get configuration descriptor */
	Request.Status = UsbFunctionGetInitialConfigDescriptor(Hc, Port);
	if (Request.Status != TransferFinished)
		return Request.Status;
	
	/* Step 2. Get FULL descriptor */
	Buffer = kmalloc(Hc->Ports[Port]->Device->ConfigMaxLength);
	
	/* Init transfer */
	UsbTransactionInit(Hc, &Request, ControlTransfer,
		Hc->Ports[Port]->Device, Hc->Ports[Port]->Device->CtrlEndpoint);

	/* Setup Packet */
	Request.Packet.Direction = USB_REQUEST_DIR_IN;
	Request.Packet.Type = USB_REQUEST_GET_DESC;
	Request.Packet.ValueHi = USB_DESC_TYPE_CONFIG;
	Request.Packet.ValueLo = 0;
	Request.Packet.Index = 0;
	Request.Packet.Length = Hc->Ports[Port]->Device->ConfigMaxLength;

	/* Setup Transfer */
	UsbTransactionSetup(Hc, &Request, sizeof(UsbPacket_t));

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
			else if (length == sizeof(UsbEndpointDescriptor_t)
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
				UsbHcEndpointType_T EpType = (UsbHcEndpointType_T)(Ep->Attributes & 0x3);

				/* Debug */
				LogInformation("USBC", "Endpoint %u - Attributes 0x%x (MaxPacketSize 0x%x)",
					Ep->Address, Ep->Attributes, Ep->MaxPacketSize);

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
	UsbHcRequest_t Request;

	/* Init transfer */
	UsbTransactionInit(Hc, &Request, ControlTransfer,
		Hc->Ports[Port]->Device, Hc->Ports[Port]->Device->CtrlEndpoint);

	/* Setup Packet */
	Request.Packet.Direction = USB_REQUEST_DIR_OUT;
	Request.Packet.Type = USB_REQUEST_SET_CONFIG;
	Request.Packet.ValueHi = 0;
	Request.Packet.ValueLo = (Configuration & 0xFF);
	Request.Packet.Index = 0;
	Request.Packet.Length = 0;		/* We do not want data */

	/* Setup Transfer */
	UsbTransactionSetup(Hc, &Request, sizeof(UsbPacket_t));

	/* ACK Transfer */
	UsbTransactionIn(Hc, &Request, 1, NULL, 0);

	/* Send it */
	UsbTransactionSend(Hc, &Request);

	/* Cleanup */
	UsbTransactionDestroy(Hc, &Request);

	/* Done */
	return Request.Status;
}

/* Gets the device descriptor */
UsbTransferStatus_t UsbFunctionGetStringDescriptor(UsbHc_t *Hc, int Port)
{
	UsbStringDescriptor_t StringDesc;
	UsbHcRequest_t Request;

	/* Init transfer */
	UsbTransactionInit(Hc, &Request, ControlTransfer,
		Hc->Ports[Port]->Device, Hc->Ports[Port]->Device->CtrlEndpoint);

	/* Setup Packet */
	Request.Packet.Direction = USB_REQUEST_DIR_IN;
	Request.Packet.Type = USB_REQUEST_GET_DESC;
	Request.Packet.ValueHi = USB_DESC_TYPE_STRING;
	Request.Packet.ValueLo = 0;
	Request.Packet.Index = 0;
	Request.Packet.Length = sizeof(UsbStringDescriptor_t);

	/* Setup Transfer */
	UsbTransactionSetup(Hc, &Request, sizeof(UsbPacket_t));

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
	UsbHcRequest_t Request;

	/* Init transfer */
	UsbTransactionInit(Hc, &Request, ControlTransfer,
		Hc->Ports[Port]->Device, Hc->Ports[Port]->Device->CtrlEndpoint);

	/* Setup Packet */
	Request.Packet.Direction = Target;
	Request.Packet.Type = USB_REQUEST_CLR_FEATURE;
	Request.Packet.ValueHi = ((Feature >> 8) & 0xFF);
	Request.Packet.ValueLo = (Feature & 0xFF);
	Request.Packet.Index = Index;
	Request.Packet.Length = 0;

	/* Setup Transfer */
	UsbTransactionSetup(Hc, &Request, sizeof(UsbPacket_t));

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
	UsbHcRequest_t Request;

	/* Init transfer */
	UsbTransactionInit(Hc, &Request, ControlTransfer,
		Hc->Ports[Port]->Device, Hc->Ports[Port]->Device->CtrlEndpoint);

	/* Setup Packet */
	Request.Packet.Direction = Target;
	Request.Packet.Type = USB_REQUEST_SET_FEATURE;
	Request.Packet.ValueHi = ((Feature >> 8) & 0xFF);
	Request.Packet.ValueLo = (Feature & 0xFF);
	Request.Packet.Index = Index;
	Request.Packet.Length = 0;

	/* Setup Transfer */
	UsbTransactionSetup(Hc, &Request, sizeof(UsbPacket_t));

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
	UsbHcRequest_t Request;

	/* Init transfer */
	UsbTransactionInit(Hc, &Request, ControlTransfer,
		Hc->Ports[Port]->Device, Hc->Ports[Port]->Device->CtrlEndpoint);

	/* Setup Packet */
	Request.Packet.Direction = Direction;
	Request.Packet.Type = USB_REQUEST_GET_DESC;
	Request.Packet.ValueHi = DescriptorType;
	Request.Packet.ValueLo = SubType;
	Request.Packet.Index = DescriptorIndex;
	Request.Packet.Length = DescriptorLength;

	/* Setup Transfer */
	UsbTransactionSetup(Hc, &Request, sizeof(UsbPacket_t));

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
	uint8_t Request, uint8_t ValueHi, uint8_t ValueLo, uint16_t Index, uint16_t Length)
{
	UsbHcRequest_t DevRequest;

	/* Init transfer */
	UsbTransactionInit(Hc, &DevRequest, ControlTransfer,
		Hc->Ports[Port]->Device, Hc->Ports[Port]->Device->CtrlEndpoint);

	/* Setup Packet */
	DevRequest.Packet.Direction = RequestType;
	DevRequest.Packet.Type = Request;
	DevRequest.Packet.ValueHi = ValueHi;
	DevRequest.Packet.ValueLo = ValueLo;
	DevRequest.Packet.Index = Index;
	DevRequest.Packet.Length = Length;

	/* Setup Transfer */
	UsbTransactionSetup(Hc, &DevRequest, sizeof(UsbPacket_t));

	/* In/Out Transfer, we want to fill data */
	if (Length != 0)
	{
		if (RequestType & USB_REQUEST_DIR_IN)
		{
			UsbTransactionIn(Hc, &DevRequest, 0, Buffer, Length);

			/* Out Transfer, STATUS Stage */
			UsbTransactionOut(Hc, &DevRequest, 1, NULL, 0);
		}
			
		else
		{
			UsbTransactionOut(Hc, &DevRequest, 0, Buffer, Length);

			/* In Transfer, ACK Stage */
			UsbTransactionIn(Hc, &DevRequest, 1, NULL, 0);
		}
			
	}
	else
	{
		/* In Transfer, ACK Stage */
		UsbTransactionIn(Hc, &DevRequest, 1, NULL, 0);
	}
	
	/* Send it */
	UsbTransactionSend(Hc, &DevRequest);

	/* Cleanup */
	UsbTransactionDestroy(Hc, &DevRequest);

	/* Done! */
	return DevRequest.Status;
}

/* Install Interrupt Pipe */
void UsbFunctionInstallPipe(UsbHc_t *Hc, UsbHcDevice_t *Device, UsbHcRequest_t *Request, 
	UsbHcEndpoint_t *Endpoint, void *Buffer, size_t Length)
{
	/* Setup Transfer */
	UsbTransactionInit(Hc, Request, InterruptTransfer, Device, Endpoint);

	/* Add in transfers */
	UsbTransactionIn(Hc, Request, 0, Buffer, Length);

	/* Initiate the transfers */
	UsbTransactionSend(Hc, Request);
}