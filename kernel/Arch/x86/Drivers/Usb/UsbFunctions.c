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
* MollenOS X86-32 USB Functions Core Driver
*/

/* Includes */
#include <Arch.h>
#include <Drivers\Usb\Usb.h>
#include <Heap.h>
#include <stddef.h>
#include <stdio.h>

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
void UsbTransactionInit(UsbHc_t *Hc, UsbHcRequest_t *Request, uint32_t Type,
	UsbHcDevice_t *Device, uint32_t Endpoint, uint32_t MaxLength)
{
	/* Control - Endpoint 0 */
	Request->Type = Type;
	Request->Data = NULL;
	Request->Device = Device;
	Request->Length = MIN(Device->Endpoints[Endpoint]->MaxPacketSize, MaxLength);
	Request->Endpoint = Device->Endpoints[Endpoint]->Address;
	Request->Transactions = NULL;

	/* Perform */
	Hc->TransactionInit(Hc->Hc, Request);
}

/* The Setup Transaction */
void UsbTransactionSetup(UsbHc_t *Hc, UsbHcRequest_t *Request, uint32_t PacketSize)
{
	UsbHcTransaction_t *Transaction;

	/* Set toggle and token-bytes */
	Request->Toggle = 0;
	Request->TokenBytes = PacketSize;

	/* Perform it */
	Transaction = Hc->TransactionSetup(Hc->Hc, Request);

	/* Append it */
	Transaction->Type = X86_USB_TRANSACTION_SETUP;
	UsbTransactionAppend(Request, Transaction);

	/* Toggle Goggle*/
	Request->Device->Endpoints[Request->Endpoint]->Toggle = 1;
}

/* The In Data Transaction */
void UsbTransactionIn(UsbHc_t *Hc, UsbHcRequest_t *Request, uint32_t Handshake, void *Buffer, uint32_t Length)
{
	/* Get length */
	UsbHcTransaction_t *Transaction;
	uint32_t FixedLen = MIN(Request->Length, Length);
	uint32_t RemainingLen = Length - FixedLen;
	uint32_t TransfersLeft = RemainingLen / Request->Length;

	/* Fix transfers */
	if (RemainingLen % Request->Length)
		TransfersLeft++;

	/* Set request io buffer */
	Request->IoBuffer = Buffer;
	Request->IoLength = FixedLen;

	if (Handshake)
		Request->Device->Endpoints[Request->Endpoint]->Toggle = 1;

	/* Get toggle */
	Request->Toggle = Request->Device->Endpoints[Request->Endpoint]->Toggle;

	/* Perform */
	Transaction = Hc->TransactionIn(Hc->Hc, Request);

	/* Append Transaction */
	Transaction->Type = X86_USB_TRANSACTION_IN;
	UsbTransactionAppend(Request, Transaction);

	/* Toggle Goggle */
	Request->Device->Endpoints[Request->Endpoint]->Toggle =
		(Request->Device->Endpoints[Request->Endpoint]->Toggle == 0) ? 1 : 0;

	if (TransfersLeft > 0)
		UsbTransactionIn(Hc, Request,
		Request->Device->Endpoints[Request->Endpoint]->Toggle,
		(void*)((uint32_t)Buffer + FixedLen), RemainingLen);
}

/* The Out Data Transaction */
void UsbTransactionOut(UsbHc_t *Hc, UsbHcRequest_t *Request, uint32_t Handshake, void *Buffer, uint32_t Length)
{
	/* Get length */
	UsbHcTransaction_t *Transaction;
	uint32_t FixedLen = MIN(Request->Length, Length);
	uint32_t RemainingLen = Length - FixedLen;
	uint32_t TransfersLeft = RemainingLen / Request->Length;

	/* Fix transfers */
	if (RemainingLen % Request->Length)
		TransfersLeft++;

	/* Set request io buffer */
	Request->IoBuffer = Buffer;
	Request->IoLength = FixedLen;

	if (Handshake)
		Request->Device->Endpoints[Request->Endpoint]->Toggle = 1;

	/* Get toggle */
	Request->Toggle = Request->Device->Endpoints[Request->Endpoint]->Toggle;

	/* Perform */
	Transaction = Hc->TransactionOut(Hc->Hc, Request);

	/* Append Transaction */
	Transaction->Type = X86_USB_TRANSACTION_OUT;
	UsbTransactionAppend(Request, Transaction);

	/* Toggle Goggle */
	Request->Device->Endpoints[Request->Endpoint]->Toggle =
		(Request->Device->Endpoints[Request->Endpoint]->Toggle == 0) ? 1 : 0;

	/* Check up on this ! !*/
	if (TransfersLeft > 0)
		UsbTransactionOut(Hc, Request,
		Request->Device->Endpoints[Request->Endpoint]->Toggle,
		(void*)((uint32_t)Buffer + FixedLen), RemainingLen);
}

/* Send to Device */
void UsbTransactionSend(UsbHc_t *Hc, UsbHcRequest_t *Request)
{
	UsbHcTransaction_t *Transaction = Request->Transactions, *NextTransaction;

	/* Perform */
	Hc->TransactionSend(Hc->Hc, Request);

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
int UsbFunctionSetAddress(UsbHc_t *Hc, int Port, uint32_t Address)
{
	UsbHcRequest_t Request;

	/* Init transfer */
	UsbTransactionInit(Hc, &Request, X86_USB_REQUEST_TYPE_CONTROL,
		Hc->Ports[Port]->Device, 0, 64);

	/* Setup Packet */
	Request.LowSpeed = (Hc->Ports[Port]->FullSpeed == 0) ? 1 : 0;
	Request.Packet.Direction = 0;
	Request.Packet.Type = X86_USB_REQ_SET_ADDR;
	Request.Packet.ValueHi = 0;
	Request.Packet.ValueLo = (Address & 0xFF);
	Request.Packet.Index = 0;
	Request.Packet.Length = 0;		/* We do not want data */

	/* Setup Transfer */
	UsbTransactionSetup(Hc, &Request, sizeof(UsbPacket_t));

	/* ACK Transfer */
	UsbTransactionIn(Hc, &Request, 1, NULL, 0);

	/* Send it */
	UsbTransactionSend(Hc, &Request);

	/* Check if it completed */
	if (Request.Completed)
		Request.Device->Address = Address;

	return Request.Completed;
}

/* Gets the device descriptor */
int UsbFunctionGetDeviceDescriptor(UsbHc_t *Hc, int Port)
{
	int i;
	UsbDeviceDescriptor_t DevInfo;
	UsbHcRequest_t Request;

	/* Init transfer */
	UsbTransactionInit(Hc, &Request, X86_USB_REQUEST_TYPE_CONTROL,
		Hc->Ports[Port]->Device, 0, 64);
	
	/* Setup Packet */
	Request.LowSpeed = (Hc->Ports[Port]->FullSpeed == 0) ? 1 : 0;
	Request.Packet.Direction = 0x80;
	Request.Packet.Type = X86_USB_REQ_GET_DESC;
	Request.Packet.ValueHi = X86_USB_DESC_TYPE_DEVICE;
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
	if (Request.Completed)
	{
		printf("USB Length 0x%x - Device Vendor Id & Product Id: 0x%x - 0x%x\n", DevInfo.Length, DevInfo.VendorId, DevInfo.ProductId);
		printf("Device Configurations 0x%x, Max Packet Size: 0x%x\n", DevInfo.NumConfigurations, DevInfo.MaxPacketSize);

		Hc->Ports[Port]->Device->Class = DevInfo.Class;
		Hc->Ports[Port]->Device->Subclass = DevInfo.Subclass;
		Hc->Ports[Port]->Device->Protocol = DevInfo.Protocol;
		Hc->Ports[Port]->Device->VendorId = DevInfo.VendorId;
		Hc->Ports[Port]->Device->ProductId = DevInfo.ProductId;
		Hc->Ports[Port]->Device->StrIndexManufactor = DevInfo.StrIndexManufactor;
		Hc->Ports[Port]->Device->StrIndexProduct = DevInfo.StrIndexProduct;
		Hc->Ports[Port]->Device->StrIndexSerialNum = DevInfo.StrIndexSerialNum;
		Hc->Ports[Port]->Device->NumConfigurations = DevInfo.NumConfigurations;
		
		/* Set MPS */
		for (i = 0; i < (int)Hc->Ports[Port]->Device->NumEndpoints; i++)
			Hc->Ports[Port]->Device->Endpoints[i]->MaxPacketSize = DevInfo.MaxPacketSize;
	}

	return Request.Completed;
}

/* Gets the initial config descriptor */
int UsbFunctionGetInitialConfigDescriptor(UsbHc_t *Hc, int Port)
{
	UsbHcRequest_t Request;
	UsbConfigDescriptor_t DevConfig;

	/* Step 1. Get configuration descriptor */

	/* Init transfer */
	UsbTransactionInit(Hc, &Request, X86_USB_REQUEST_TYPE_CONTROL,
		Hc->Ports[Port]->Device, 0, 64);

	/* Setup Packet */
	Request.LowSpeed = (Hc->Ports[Port]->FullSpeed == 0) ? 1 : 0;
	Request.Packet.Direction = 0x80;
	Request.Packet.Type = X86_USB_REQ_GET_DESC;
	Request.Packet.ValueHi = X86_USB_DESC_TYPE_CONFIG;
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
	if (Request.Completed)
	{
		Hc->Ports[Port]->Device->Configuration = DevConfig.ConfigurationValue;
		Hc->Ports[Port]->Device->ConfigMaxLength = DevConfig.TotalLength;
		Hc->Ports[Port]->Device->NumInterfaces = DevConfig.NumInterfaces;
		Hc->Ports[Port]->Device->MaxPowerConsumption = (uint16_t)(DevConfig.MaxPowerConsumption * 2);
	}

	/* Done */
	return Request.Completed;
}

/* Gets the config descriptor */
int UsbFunctionGetConfigDescriptor(UsbHc_t *Hc, int Port)
{
	UsbHcRequest_t Request;
	void *Buffer;

	/* Step 1. Get configuration descriptor */
	if (!UsbFunctionGetInitialConfigDescriptor(Hc, Port))
		return 0;
	
	/* Step 2. Get FULL descriptor */
	printf("OHCI_Handler: (Get_Config_Desc) Configuration Length: 0x%x\n",
		Hc->Ports[Port]->Device->ConfigMaxLength);
	Buffer = kmalloc(Hc->Ports[Port]->Device->ConfigMaxLength);
	
	/* Init transfer */
	UsbTransactionInit(Hc, &Request, X86_USB_REQUEST_TYPE_CONTROL,
		Hc->Ports[Port]->Device, 0, 64);

	/* Setup Packet */
	Request.LowSpeed = (Hc->Ports[Port]->FullSpeed == 0) ? 1 : 0;
	Request.Packet.Direction = 0x80;
	Request.Packet.Type = X86_USB_REQ_GET_DESC;
	Request.Packet.ValueHi = X86_USB_DESC_TYPE_CONFIG;
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
	if (Request.Completed)
	{
		uint8_t *buf_ptr = (uint8_t*)Buffer;
		uint32_t bytes_left = Hc->Ports[Port]->Device->ConfigMaxLength;
		uint32_t endpoints = 1;
		uint32_t ep_itr = 1;
		int i;
		Hc->Ports[Port]->Device->NumInterfaces = 0;
		Hc->Ports[Port]->Device->Descriptors = Buffer;
		Hc->Ports[Port]->Device->DescriptorsLength = Hc->Ports[Port]->Device->ConfigMaxLength;

		/* Parse Interface & Endpoints */
		while (bytes_left > 0)
		{
			/* Cast */
			uint8_t length = *buf_ptr;
			uint8_t type = *(buf_ptr + 1);

			/* Is this an interface or endpoint? :O */
			if (length == sizeof(UsbInterfaceDescriptor_t)
				&& type == X86_USB_DESC_TYPE_INTERFACE)
			{
				UsbHcInterface_t *usb_if;
				UsbInterfaceDescriptor_t *iface = (UsbInterfaceDescriptor_t*)buf_ptr;

				/* Debug Print */
				if (Hc->Ports[Port]->Device->Interfaces[iface->NumInterface] == NULL)
				{
					printf("Interface %u - Endpoints %u (Class %u, Subclass %u, Protocol %u)\n",
						iface->NumInterface, iface->NumEndpoints, iface->Class,
						iface->Subclass, iface->Protocol);

					/* Allocate */
					usb_if = (UsbHcInterface_t*)kmalloc(sizeof(UsbHcInterface_t));
					usb_if->Id = iface->NumInterface;
					usb_if->Endpoints = iface->NumEndpoints;
					usb_if->Class = iface->Class;
					usb_if->Subclass = iface->Subclass;
					usb_if->Protocol = iface->Protocol;
					endpoints += iface->NumEndpoints;

					/* Update Device */
					Hc->Ports[Port]->Device->Interfaces[Hc->Ports[Port]->Device->NumInterfaces] = usb_if;
					Hc->Ports[Port]->Device->NumInterfaces++;
				}
				
				/* Increase Pointer */
				bytes_left -= iface->Length;
				buf_ptr += iface->Length;
			}
			else
			{
				buf_ptr += length;
				bytes_left -= length;
			}
		}

		/* Prepare Endpoint Loop */
		buf_ptr = (uint8_t*)Buffer;
		bytes_left = Hc->Ports[Port]->Device->ConfigMaxLength;

		/* Reallocate new endpoints */
		if (endpoints > 1)
		{
			for (i = 1; i < (int)endpoints; i++)
				Hc->Ports[Port]->Device->Endpoints[i] = (UsbHcEndpoint_t*)kmalloc(sizeof(UsbHcEndpoint_t));
		}
		else
			return Request.Completed;

		/* Update Device */
		Hc->Ports[Port]->Device->NumEndpoints = endpoints;
		
		while (bytes_left > 0)
		{
			/* Cast */
			uint8_t length = *buf_ptr;
			uint8_t type = *(buf_ptr + 1);

			/* Is this an interface or endpoint? :O */
			if (length == sizeof(UsbEndpointDescriptor_t)
				&& type == X86_USB_DESC_TYPE_ENDP)
			{
				UsbEndpointDescriptor_t *endpoint = (UsbEndpointDescriptor_t*)buf_ptr;
				uint32_t ep_address = endpoint->Address & 0xF;
				uint32_t ep_type = endpoint->Attributes & 0x3;

				if (ep_itr < endpoints)
				{
					printf("Endpoint %u - Attributes 0x%x (MaxPacketSize 0x%x)\n",
						endpoint->Address, endpoint->Attributes, endpoint->MaxPacketSize);

					/* Update Device */
					Hc->Ports[Port]->Device->Endpoints[ep_itr]->Address = ep_address;
					Hc->Ports[Port]->Device->Endpoints[ep_itr]->MaxPacketSize = (endpoint->MaxPacketSize & 0x7FF);
					Hc->Ports[Port]->Device->Endpoints[ep_itr]->Bandwidth = ((endpoint->MaxPacketSize >> 11) & 0x3) + 1;
					Hc->Ports[Port]->Device->Endpoints[ep_itr]->Interval = endpoint->Interval;
					Hc->Ports[Port]->Device->Endpoints[ep_itr]->Toggle = 0;
					Hc->Ports[Port]->Device->Endpoints[ep_itr]->Type = ep_type;

					/* In or Out? */
					if (endpoint->Address & 0x80)
						Hc->Ports[Port]->Device->Endpoints[ep_itr]->Direction = X86_USB_EP_DIRECTION_IN;
					else
						Hc->Ports[Port]->Device->Endpoints[ep_itr]->Direction = X86_USB_EP_DIRECTION_OUT;

					ep_itr++;
				}
				

				/* Increase Pointer */
				bytes_left -= endpoint->Length;
				buf_ptr += endpoint->Length;
			}
			else
			{
				buf_ptr += length;
				bytes_left -= length;
			}
		}
	}
	else
		Hc->Ports[Port]->Device->Descriptors = NULL;

	/* Done */
	return Request.Completed;
}

/* Set configuration of an usb device */
int UsbFunctionSetConfiguration(UsbHc_t *Hc, int Port, uint32_t Configuration)
{
	UsbHcRequest_t Request;

	/* Init transfer */
	UsbTransactionInit(Hc, &Request, X86_USB_REQUEST_TYPE_CONTROL,
		Hc->Ports[Port]->Device, 0, 64);

	/* Setup Packet */
	Request.LowSpeed = (Hc->Ports[Port]->FullSpeed == 0) ? 1 : 0;
	Request.Packet.Direction = 0;
	Request.Packet.Type = X86_USB_REQ_SET_CONFIG;
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

	/* Done */
	return Request.Completed;
}

/* Get specific descriptor */
int UsbFunctionGetDescriptor(UsbHc_t *Hc, int Port, void *Buffer, uint8_t Direction,
	uint8_t DescriptorType, uint8_t SubType, uint8_t DescriptorIndex, uint16_t DescriptorLength)
{
	UsbHcRequest_t Request;

	/* Init transfer */
	UsbTransactionInit(Hc, &Request, X86_USB_REQUEST_TYPE_CONTROL,
		Hc->Ports[Port]->Device, 0, 64);

	/* Setup Packet */
	Request.LowSpeed = (Hc->Ports[Port]->FullSpeed == 0) ? 1 : 0;
	Request.Packet.Direction = Direction;
	Request.Packet.Type = X86_USB_REQ_GET_DESC;
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

	return Request.Completed;
}

/* Send packet */
int UsbFunctionSendPacket(UsbHc_t *Hc, int Port, void *Buffer, uint8_t RequestType,
	uint8_t Request, uint8_t ValueHi, uint8_t ValueLo, uint16_t Index, uint16_t Length)
{
	UsbHcRequest_t DevRequest;

	/* Init transfer */
	UsbTransactionInit(Hc, &DevRequest, X86_USB_REQUEST_TYPE_CONTROL,
		Hc->Ports[Port]->Device, 0, 64);

	/* Setup Packet */
	DevRequest.LowSpeed = (Hc->Ports[Port]->FullSpeed == 0) ? 1 : 0;
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
		if (RequestType & X86_USB_REQ_DIRECTION_IN)
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

	return DevRequest.Completed;
}

/* Send SCSI Command */