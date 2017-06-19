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
	_In_ UsbPacket_t *Packet,
	_Out_ uintptr_t **PacketBuffer)
{
	// Variables
	UsbTransaction_t *Transaction = &Transfer->Transactions[0];
	uintptr_t *PacketVirtual = NULL;
	uintptr_t PacketPhysical = 0;

	// Initialize variables
	Transaction->Type = SetupTransaction;
	Transaction->Handshake = 0;

	// Allocate some buffer space
	if (BufferPoolAllocate(UsbCoreGetBufferPool(),
		sizeof(UsbPacket_t), &PacketVirtual, 
		&PacketPhysical) != OsSuccess) {
		return OsError;
	}
	
	// Copy packet data to buffer
	memcpy(PacketVirtual, Packet, sizeof(UsbPacket_t));

	// Set rest of members
	Transaction->BufferAddress = PacketPhysical;
	Transaction->Length = sizeof(UsbPacket_t);

	// Store virtual for freeing
	*PacketBuffer = PacketVirtual;

	// Done
	return OsSuccess;
}

/* UsbTransferIn 
 * Creates an In-transaction in the given usb-transfer. Both buffer and length 
 * must be pre-allocated - and passed here. If handshake == 1 it's an ack-transaction. */
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

/* UsbTransferOut 
 * Creates an Out-transaction in the given usb-transfer. Both buffer and length 
 * must be pre-allocated - and passed here. If handshake == 1 it's an ack-transaction. */
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
 * Finalizes the transfer and queues it up for execution. This function does not
 * return untill the transfer has completed, result will be spit out in <Result>. */
OsStatus_t
UsbTransferSend(
	_In_ UsbController_t *Controller,
	_In_ UsbPort_t *Port, 
	_In_ UsbTransfer_t *Transfer,
	_Out_ UsbTransferResult_t *Result)
{
	// Variables
	UUId_t Pipe;

	// Build pipe-id
	Pipe = ((Port->Device->Base.Address & 0xFFFF) << 16) 
		  | (Transfer->Endpoint.Address & 0xFFFF);

	// Send
	return UsbQueueTransfer(Controller->Driver, Controller->Device,
		Pipe, Transfer, Result);
}

/* UsbFunctionSetAddress
 * Set address of an usb device - the device structure is automatically updated. */
UsbTransferStatus_t
UsbFunctionSetAddress(
	_In_ UsbController_t *Controller, 
	_In_ UsbPort_t *Port, 
	_In_ int Address)
{
	// Variables
	uintptr_t *PacketBuffer = NULL;

	// Buffers
	UsbTransferResult_t Result = { 0 };
	UsbTransfer_t Transfer = { 0 };
	UsbPacket_t Packet = { 0 };

	// Setup packet
	Packet.Direction = USBPACKET_DIRECTION_OUT;
	Packet.Type = USBPACKET_TYPE_SET_ADDRESS;
	Packet.ValueLo = (uint8_t)(Address & 0xFF);
	Packet.ValueHi = 0;
	Packet.Index = 0;
	Packet.Length = 0;

	// Initialize transfer
	UsbTransferInitialize(Port, ControlTransfer,
		&Port->Device->ControlEndpoint, &Transfer);

	// SetAddress request consists of two transactions
	// Setup and In (ACK)
	UsbTransferSetup(&Transfer, &Packet, &PacketBuffer);
	UsbTransferIn(&Transfer, 1, 0, 0, 1);

	// Execute the transaction and cleanup
	// the buffer
	UsbTransferSend(Controller, Port, &Transfer, &Result);
	BufferPoolFree(UsbCoreGetBufferPool(), PacketBuffer);

	// If the transfer was successfull update the address
	if (Result.Status == TransferFinished) {
		Port->Device->Base.Address = Address;
	}

	// Done
	return Result.Status;
}

/* UsbFunctionGetDeviceDescriptor
 * Queries the device descriptor of an usb device on a given port. The information
 * is automatically filled in, in the device structure */
UsbTransferStatus_t
UsbFunctionGetDeviceDescriptor(
	_In_ UsbController_t *Controller, 
	_In_ UsbPort_t *Port)
{
	// Variables
	UsbDeviceDescriptor_t *Descriptor = NULL;
	uintptr_t *DescriptorVirtual = NULL;
	uintptr_t DescriptorPhysical = 0;
	uintptr_t *PacketBuffer = NULL;

	// Buffers
	UsbTransferResult_t Result = { 0 };
	UsbTransfer_t Transfer = { 0 };
	UsbPacket_t Packet = { 0 };

	// Initialize the packet
	Packet.Direction = USBPACKET_DIRECTION_IN;
	Packet.Type = USBPACKET_TYPE_GET_DESC;
	Packet.ValueHi = USB_DESCRIPTOR_DEVICE;
	Packet.ValueLo = 0;
	Packet.Index = 0;
	Packet.Length = 0x12;	// Max Descriptor Length is 18 bytes

	// Allocate a data-buffer
	if (BufferPoolAllocate(UsbCoreGetBufferPool(),
		0x12, &DescriptorVirtual, &DescriptorPhysical) != OsSuccess) {
		return TransferInvalidData;
	}

	// Initialize pointer
	Descriptor = (UsbDeviceDescriptor_t*)DescriptorVirtual;

	// Initialize transfer
	UsbTransferInitialize(Port, ControlTransfer,
		&Port->Device->ControlEndpoint, &Transfer);

	// GetDeviceDescriptor request consists of three transactions
	// Setup, In (Data) and Out (ACK)
	UsbTransferSetup(&Transfer, &Packet, &PacketBuffer);
	UsbTransferIn(&Transfer, 1, DescriptorPhysical, 0x12, 0);
	UsbTransferOut(&Transfer, 2, 0, 0, 1);

	// Execute the transaction and cleanup
	// the buffer
	UsbTransferSend(Controller, Port, &Transfer, &Result);

	// If the transfer finished correctly update the stored
	// device information to the queried
	if (Result.Status == TransferFinished) {
		TRACE("USB Length 0x%x - Device Vendor Id & Product Id: 0x%x - 0x%x", 
			Descriptor->Length, Descriptor->VendorId, Descriptor->ProductId);
		TRACE("Device Configurations 0x%x, Max Packet Size: 0x%x",
			Descriptor->NumConfigurations, DDescriptor->MaxPacketSize);

		// Update information
		Port->Device->Base.Class = Descriptor->Class;
		Port->Device->Base.Subclass = Descriptor->Subclass;
		Port->Device->Base.Protocol = Descriptor->Protocol;
		Port->Device->Base.VendorId = Descriptor->VendorId;
		Port->Device->Base.ProductId = Descriptor->ProductId;
		Port->Device->Base.StringIndexManufactor = Descriptor->StringIndexManufactor;
		Port->Device->Base.StringIndexProduct = Descriptor->StringIndexProduct;
		Port->Device->Base.StringIndexSerialNumber = Descriptor->StringIndexSerialNumber;
		Port->Device->Base.ConfigurationCount = Descriptor->ConfigurationCount;
		Port->Device->Base.MaxPacketSize = Descriptor->MaxPacketSize;
		
		// Update MPS
		Port->Device->ControlEndpoint.MaxPacketSize = Descriptor->MaxPacketSize;
	}

	// Cleanup allocations
	BufferPoolFree(UsbCoreGetBufferPool(), DescriptorVirtual);
	BufferPoolFree(UsbCoreGetBufferPool(), PacketBuffer);

	// Done
	return Result.Status;
}

/* UsbFunctionGetInitialConfigDescriptor
 * Queries the initial configuration descriptor, and is neccessary to know how
 * long the full configuration descriptor is. */
UsbTransferStatus_t
UsbFunctionGetInitialConfigDescriptor(
	_In_ UsbController_t *Controller, 
	_In_ UsbPort_t *Port)
{
	// Variables
	UsbConfigDescriptor_t *Descriptor = NULL;
	uintptr_t *DescriptorVirtual = NULL;
	uintptr_t DescriptorPhysical = 0;
	uintptr_t *PacketBuffer = NULL;

	// Buffers
	UsbTransferResult_t Result = { 0 };
	UsbTransfer_t Transfer = { 0 };
	UsbPacket_t Packet = { 0 };

	// Initialize the packet
	Packet.Direction = USBPACKET_DIRECTION_IN;
	Packet.Type = USBPACKET_TYPE_GET_DESC;
	Packet.ValueHi = USB_DESCRIPTOR_CONFIG;
	Packet.ValueLo = 0;
	Packet.Index = 0;
	Packet.Length = sizeof(UsbConfigDescriptor_t);

	// Allocate a data-buffer
	if (BufferPoolAllocate(UsbCoreGetBufferPool(),
		sizeof(UsbConfigDescriptor_t), &DescriptorVirtual, 
		&DescriptorPhysical) != OsSuccess) {
		return TransferInvalidData;
	}

	// Initialize pointer
	Descriptor = (UsbConfigDescriptor_t*)DescriptorVirtual;

	// Initialize transfer
	UsbTransferInitialize(Port, ControlTransfer,
		&Port->Device->ControlEndpoint, &Transfer);

	// GetDeviceDescriptor request consists of three transactions
	// Setup, In (Data) and Out (ACK)
	UsbTransferSetup(&Transfer, &Packet, &PacketBuffer);
	UsbTransferIn(&Transfer, 1, DescriptorPhysical, sizeof(UsbConfigDescriptor_t), 0);
	UsbTransferOut(&Transfer, 2, 0, 0, 1);

	// Execute the transaction and cleanup
	// the buffer
	UsbTransferSend(Controller, Port, &Transfer, &Result);

	// Did it complete?
	if (Result.Status == TransferFinished) {
		Port->Device->Base.Configuration = Descriptor->ConfigurationValue;
		Port->Device->Base.ConfigMaxLength = Descriptor->TotalLength;
		Port->Device->Base.InterfaceCount = (int)Descriptor->NumInterfaces;
		Port->Device->Base.MaxPowerConsumption = (uint16_t)(Descriptor->MaxPowerConsumption * 2);
	}

	// Cleanup allocations
	BufferPoolFree(UsbCoreGetBufferPool(), DescriptorVirtual);
	BufferPoolFree(UsbCoreGetBufferPool(), PacketBuffer);

	// Done
	return Result.Status;
}

/* Gets the config descriptor */
UsbTransferStatus_t
UsbFunctionGetConfigDescriptor(
	_In_ UsbController_t *Controller, 
	_In_ UsbPort_t *Port)
{
	// Variables
	uintptr_t *DescriptorVirtual = NULL;
	uintptr_t DescriptorPhysical = 0;
	uintptr_t *PacketBuffer = NULL;

	// Buffers
	UsbTransferResult_t Result = { 0 };
	UsbTransfer_t Transfer = { 0 };
	UsbPacket_t Packet = { 0 };

	// Make sure the initial configuration descriptor has been queried.
	Result.Status = UsbFunctionGetInitialConfigDescriptor(Controller, Port);
	if (Result.Status != TransferFinished) {
		return Result.Status;
	}

	// Initialize the packet
	Packet.Direction = USBPACKET_DIRECTION_IN;
	Packet.Type = USBPACKET_TYPE_GET_DESC;
	Packet.ValueHi = USB_DESCRIPTOR_CONFIG;
	Packet.ValueLo = 0;
	Packet.Index = 0;
	Packet.Length = Port->Device->Base.ConfigMaxLength;

	// Allocate a data-buffer
	if (BufferPoolAllocate(UsbCoreGetBufferPool(),
		Port->Device->Base.ConfigMaxLength, &DescriptorVirtual, 
		&DescriptorPhysical) != OsSuccess) {
		return TransferInvalidData;
	}
	
	// Allocate a buffer for the entire descriptor
	Port->Device->Descriptors = malloc(Port->Device->Base.ConfigMaxLength);
	Port->Device->DescriptorsBufferLength = Port->Device->Base.ConfigMaxLength;

	// Initialize transfer
	UsbTransferInitialize(Port, ControlTransfer,
		&Port->Device->ControlEndpoint, &Transfer);

	// GetDeviceDescriptor request consists of three transactions
	// Setup, In (Data) and Out (ACK)
	UsbTransferSetup(&Transfer, &Packet, &PacketBuffer);
	UsbTransferIn(&Transfer, 1, DescriptorPhysical, 
		Port->Device->Base.ConfigMaxLength, 0);
	UsbTransferOut(&Transfer, 2, 0, 0, 1);

	// Execute the transaction and cleanup
	// the buffer
	UsbTransferSend(Controller, Port, &Transfer, &Result);

	// Copy data over
	memcpy(Port->Device->Descriptors, DescriptorVirtual, 
		Port->Device->Base.ConfigMaxLength);

	// Cleanup allocations
	BufferPoolFree(UsbCoreGetBufferPool(), DescriptorVirtual);
	BufferPoolFree(UsbCoreGetBufferPool(), PacketBuffer);

	// Did it complete?
	if (Result.Status == TransferFinished) {

		// Iteration variables
		uint8_t *BufferPointer = (uint8_t*)Port->Device->Descriptors;
		int BytesLeft = (int)Port->Device->Base.ConfigMaxLength;
		size_t EpIterator = 1;
		int CurrentIfVersion = 0;
		
		// Update initials
		Port->Device->Base.InterfaceCount = 0;

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
				UsbInterface = &Port->Device->Interfaces[Interface->NumInterface];

				// Has it been setup yet?
				if (!UsbInterface->Exists) {

					// Copy data over
					UsbInterface->Base.Id = Interface->NumInterface;
					UsbInterface->Base.Class = Interface->Class;
					UsbInterface->Base.Subclass = Interface->Subclass;
					UsbInterface->Base.Protocol = Interface->Protocol;
					UsbInterface->Base.StringIndex = Interface->StrIndexInterface;

					// Update count
					Port->Device->Base.InterfaceCount++;
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

	// Done
	return Result.Status;
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
