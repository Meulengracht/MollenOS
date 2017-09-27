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
#include <os/driver/contracts/usbhost.h>
#include <os/driver/bufferpool.h>
#include <os/driver/driver.h>
#include <os/driver/usb.h>
#include <os/thread.h>
#include <os/utils.h>

/* Includes
 * - Library */
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

/* Globals
 * State-keeping variables for libusb */
static BufferPool_t *__LibUsbBufferPool = NULL;

/* UsbInitialize
 * Initializes libusb and enables the use of all the control
 * functions that require a shared buffer-pool. */
OsStatus_t
UsbInitialize(void)
{

}

/* UsbCleanup
 * Frees the shared resources allocated by UsbInitialize. */
OsStatus_t
UsbCleanup(void)
{

}

/* UsbTransferInitialize
 * Initializes the usb-transfer structure from the given
 * device and requested transfer type. */
OsStatus_t
UsbTransferInitialize(
    _InOut_ UsbTransfer_t *Transfer,
    _In_ UsbHcDevice_t *Device,
    _In_ UsbHcEndpointDescriptor_t *Endpoint,
    _In_ UsbTransferType_t Type)
{
    // Initialize structure
    memset(&Transfer, 0, sizeof(UsbTransfer_t));
    memcpy(&Transfer->Endpoint, Endpoint, sizeof(UsbHcEndpointDescriptor_t));

    // Initialize
    Transfer->Type = Type;
    Transfer->Speed = Device->Speed;
    Transfer->Pipe = ((Device->Address & 0xFFFF) << 16) | (Endpoint->Address & 0xFFFF);
    
    // Done
    return OsSuccess;
}

/* UsbTransferSetup 
 * Initializes a transfer for a control setup-transaction. 
 * If there is no data-stage then set Data members to 0. */
OsStatus_t
UsbTransferSetup(
    _InOut_ UsbTransfer_t *Transfer,
    _In_ uintptr_t SetupAddress,
    _In_ uintptr_t DataAddress,
    _In_ size_t DataLength,
    _In_ UsbTransactionType_t DataType)
{
    // Variables
    UsbTransactionType_t AckType = InTransaction;
    int AckIndex = 1;

    // Initialize the setup stage
    Transfer->Transactions[0].Type = SetupTransaction;
    Transfer->Transactions[0].BufferAddress = SetupAddress;
    Transfer->Transactions[0].Length = sizeof(UsbPacket_t);

    // Is there a data-stage?
    if (DataAddress != 0) {
        AckIndex = 2;
        Transfer->Transactions[1].BufferAddress = DataAddress;
        Transfer->Transactions[1].Length = DataLength;
        Transfer->Transactions[1].Type = DataType;
        if (DataType == InTransaction) {
            AckType = OutTransaction;
        }
    }

    // Ack-stage
    Transfer->Transactions[AckIndex].ZeroLength = 1;
    Transfer->Transactions[AckIndex].Handshake = 1;
    Transfer->Transactions[AckIndex].Type = AckType;

    // Update number of transactions
    Transfer->TransactionCount = AckIndex + 1;

    // Done
    return OsSuccess;
}

/* UsbTransferIn 
 * Creates an In-transaction in the given usb-transfer. Both buffer and length 
 * must be pre-allocated - and passed here. If handshake == 1 it's an ack-transaction. */
OsStatus_t
UsbTransferIn(
	_Out_ UsbTransfer_t *Transfer,
	_In_ uintptr_t BufferAddress, 
	_In_ size_t Length,
	_In_ int Handshake)
{
	// Variables
    UsbTransaction_t *Transaction = NULL;

    // Sanitize count
    if (Transfer->TransactionCount >= 3) {
        return OsError;
    }

	// Initialize variables
    Transaction = &Transfer->Transactions[Transfer->TransactionCount];
    Transaction->Type = InTransaction;
	Transaction->Handshake = Handshake;
	Transaction->BufferAddress = BufferAddress;
    Transaction->Length = Length;

	// Zero-length?
	if (Length == 0) {
		Transaction->ZeroLength = 1;
	}

	// Done
    Transfer->TransactionCount++;
	return OsSuccess;
}

/* UsbTransferOut 
 * Creates an Out-transaction in the given usb-transfer. Both buffer and length 
 * must be pre-allocated - and passed here. If handshake == 1 it's an ack-transaction. */
OsStatus_t
UsbTransferOut(
	_Out_ UsbTransfer_t *Transfer,
	_In_ uintptr_t BufferAddress, 
	_In_ size_t Length,
	_In_ int Handshake)
{
	// Variables
    UsbTransaction_t *Transaction = NULL;

    // Sanitize count
    if (Transfer->TransactionCount >= 3) {
        return OsError;
    }

	// Initialize variables
    Transaction = &Transfer->Transactions[Transfer->TransactionCount];
	Transaction->Type = OutTransaction;
	Transaction->Handshake = Handshake;
	Transaction->BufferAddress = BufferAddress;
	Transaction->Length = Length;

	// Zero-length?
	if (Length == 0) {
		Transaction->ZeroLength = 1;
	}

	// Done
    Transfer->TransactionCount++;
    return OsSuccess;
}

/* UsbTransferQueue 
 * Queues a new Control or Bulk transfer for the given driver
 * and pipe. They must exist. The function blocks untill execution */
OsStatus_t
UsbTransferQueue(
	_In_ UUId_t Driver,
	_In_ UUId_t Device,
	_In_ UsbTransfer_t *Transfer,
	_Out_ UsbTransferResult_t *Result)
{
	// Variables
	MContract_t Contract;

	// Setup contract stuff for request
	Contract.DriverId = Driver;
	Contract.Type = ContractController;
	Contract.Version = __USBMANAGER_INTERFACE_VERSION;

	// Query the driver directly
	return QueryDriver(&Contract, __USBHOST_QUEUETRANSFER,
		&Device, sizeof(UUId_t), &Transfer->Pipe, sizeof(UUId_t), 
		Transfer, sizeof(UsbTransfer_t), 
		Result, sizeof(UsbTransferResult_t));
}

/* UsbTransferQueuePeriodic 
 * Queues a new Interrupt or Isochronous transfer. This transfer is 
 * persistant untill device is disconnected or Dequeue is called. */
OsStatus_t
UsbTransferQueuePeriodic(
	_In_ UUId_t Driver,
	_In_ UUId_t Device,
	_In_ UsbTransfer_t *Transfer,
	_Out_ UUId_t *TransferId)
{
	// Variables
	UsbTransferResult_t Result;
	MContract_t Contract;

	// Setup contract stuff for request
	Contract.DriverId = Driver;
	Contract.Type = ContractController;
	Contract.Version = __USBMANAGER_INTERFACE_VERSION;

	// Query the driver directly
	return QueryDriver(&Contract, __USBHOST_QUEUEPERIODIC,
		&Device, sizeof(UUId_t), &Transfer->Pipe, sizeof(UUId_t), 
		Transfer, sizeof(UsbTransfer_t), 
		&Result, sizeof(UsbTransferResult_t));
}

/* UsbTransferDequeuePeriodic 
 * Dequeues an existing periodic transfer from the given controller. The transfer
 * and the controller must be valid. */
OsStatus_t
UsbTransferDequeuePeriodic(
	_In_ UUId_t Driver,
	_In_ UUId_t Device,
	_In_ UUId_t TransferId)
{
	// Variables
	MContract_t Contract;
	OsStatus_t Result;

	// Setup contract stuff for request
	Contract.DriverId = Driver;
	Contract.Type = ContractController;
	Contract.Version = __USBMANAGER_INTERFACE_VERSION;

	// Query the driver directly
	return QueryDriver(&Contract, __USBHOST_QUEUETRANSFER,
		&Device, sizeof(UUId_t), 
		&TransferId, sizeof(UUId_t), 
		NULL, 0, &Result, sizeof(OsStatus_t));
}

/* UsbHostResetPort
 * Resets the given port on the given controller and queries it's
 * status afterwards. This returns an updated status of the port after
 * the reset. */
OsStatus_t
UsbHostResetPort(
	_In_ UUId_t Driver,
	_In_ UUId_t Device,
	_In_ int Index,
	_Out_ UsbHcPortDescriptor_t *Descriptor)
{
	// Variables
	MContract_t Contract;

	// Setup contract stuff for request
	Contract.DriverId = Driver;
	Contract.Type = ContractController;
	Contract.Version = __USBMANAGER_INTERFACE_VERSION;

	// Query the driver directly
	return QueryDriver(&Contract, __USBHOST_RESETPORT,
		&Device, sizeof(UUId_t), 
		&Index, sizeof(int), NULL, 0, 
		Descriptor, sizeof(UsbHcPortDescriptor_t));
}

/* UsbHostQueryPort 
 * Queries the port-descriptor of host-controller port. */
OsStatus_t
UsbHostQueryPort(
	_In_ UUId_t Driver,
	_In_ UUId_t Device,
	_In_ int Index,
	_Out_ UsbHcPortDescriptor_t *Descriptor)
{
	// Variables
	MContract_t Contract;

	// Setup contract stuff for request
	Contract.DriverId = Driver;
	Contract.Type = ContractController;
	Contract.Version = __USBMANAGER_INTERFACE_VERSION;

	// Query the driver directly
	return QueryDriver(&Contract, __USBHOST_QUERYPORT,
		&Device, sizeof(UUId_t), 
		&Index, sizeof(int), NULL, 0, 
		Descriptor, sizeof(UsbHcPortDescriptor_t));
}

/* UsbSetAddress
 * Changes the address of the usb-device. This permanently updates the address. 
 * It is not possible to change the address once enumeration is done. */
UsbTransferStatus_t
UsbSetAddress(
	_In_ UUId_t Driver,
	_In_ UUId_t Device,
	_In_ UsbHcDevice_t *UsbDevice, 
	_In_ UsbHcEndpointDescriptor_t *Endpoint, 
	_In_ int Address)
{
	// Variables
    uintptr_t *PacketBuffer = NULL;
    uintptr_t PacketPhysical = 0;
	UsbPacket_t *Packet = NULL;

	// Buffers
	UsbTransferResult_t Result = { 0 };
	UsbTransfer_t Transfer = { 0 };

    // Allocate buffers
    if (BufferPoolAllocate(__LibUsbBufferPool, 8, 
            &PacketBuffer, &PacketPhysical) != OsSuccess) {
		return TransferInvalidData;
	}

    // Setup packet
    Packet = (UsbPacket_t*)PacketBuffer;
	Packet->Direction = USBPACKET_DIRECTION_OUT;
	Packet->Type = USBPACKET_TYPE_SET_ADDRESS;
	Packet->ValueLo = (uint8_t)(Address & 0xFF);
	Packet->ValueHi = 0;
	Packet->Index = 0;
	Packet->Length = 0;

	// Initialize transfer
	UsbTransferInitialize(&Transfer, UsbDevice, Endpoint, ControlTransfer);

	// SetAddress does not have a data-stage
    UsbTransferSetup(&Transfer, PacketPhysical, 0, 0, InTransaction);
    
	// Execute the transaction and cleanup the buffer
	if (UsbTransferQueue(Driver, Device, &Transfer, &Result) != OsSuccess) {
        ERROR("Usb transfer returned error");
        Result.Status = TransferNotProcessed;
    }
	BufferPoolFree(__LibUsbBufferPool, PacketBuffer);

	// Done
	return Result.Status;
}

/* UsbGetDeviceDescriptor
 * Queries the device descriptor of an usb device on a given port. */
UsbTransferStatus_t
UsbGetDeviceDescriptor(
	_In_ UUId_t Driver,
	_In_ UUId_t Device,
	_In_ UsbHcDevice_t *UsbDevice, 
    _In_ UsbHcEndpointDescriptor_t *Endpoint,
    _Out_ UsbDeviceDescriptor_t *DeviceDescriptor)
{
	// Variables
	UsbDeviceDescriptor_t *Descriptor = NULL;
	uintptr_t *DescriptorVirtual = NULL;
	uintptr_t DescriptorPhysical = 0;
	uintptr_t *PacketBuffer = NULL;
    uintptr_t PacketPhysical = 0;
	UsbPacket_t *Packet = NULL;

	// Buffers
	UsbTransferResult_t Result = { 0 };
	UsbTransfer_t Transfer = { 0 };
    
    // Allocate buffers
    if (BufferPoolAllocate(__LibUsbBufferPool, 8, 
            &PacketBuffer, &PacketPhysical) != OsSuccess) {
		return TransferInvalidData;
	}

    // Initialize the packet
    Packet = (UsbPacket_t*)PacketBuffer;
	Packet->Direction = USBPACKET_DIRECTION_IN;
	Packet->Type = USBPACKET_TYPE_GET_DESC;
	Packet->ValueHi = USB_DESCRIPTOR_DEVICE;
	Packet->ValueLo = 0;
	Packet->Index = 0;
	Packet->Length = 0x12;	// Max Descriptor Length is 18 bytes

	// Allocate a data-buffer
    if (BufferPoolAllocate(__LibUsbBufferPool, 0x12, 
            &DescriptorVirtual, &DescriptorPhysical) != OsSuccess) {
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
		memcpy(DeviceDescriptor, Descriptor, sizeof(DeviceDescriptor_t));
	}

	// Cleanup allocations
	BufferPoolFree(__LibUsbBufferPool, DescriptorVirtual);
	BufferPoolFree(__LibUsbBufferPool, PacketBuffer);

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

	// GetInitialConfigDescriptor request consists of three transactions
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

/* UsbFunctionGetConfigDescriptor
 * Queries the full configuration descriptor setup including all endpoints and interfaces.
 * This relies on the GetInitialConfigDescriptor. Also allocates all resources neccessary. */
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
			else if ((Length == 7 || Length == 9)
				&& Type == USB_DESCRIPTOR_ENDPOINT) {
				
				// Variables
				UsbHcEndpointDescriptor_t *HcEndpoint = NULL;
				UsbEndpointDescriptor_t *Endpoint = NULL;
				UsbEndpointType_t EndpointType;
				size_t EndpointAddress = 0;

				// Protect against null interface-endpoints
				if (Port->Device->Base.InterfaceCount == 0) {
					goto NextEntry;
				}

				// Instantiate pointer
				Endpoint = (UsbEndpointDescriptor_t*)BufferPointer;
				HcEndpoint = &Port->Device->Interfaces[
					Port->Device->Base.InterfaceCount - 1].
						Versions[CurrentIfVersion].Endpoints[EpIterator];

				// Extract some information
				EndpointAddress = (Endpoint->Address & 0xF);
				EndpointType = (UsbEndpointType_t)(Endpoint->Attributes & 0x3);

				// Trace some information
				TRACE("Endpoint %u (%s) - Attributes 0x%x (MaxPacketSize 0x%x)",
					EndpointAddress, ((Endpoint->Address & 0x80) != 0 ? "IN" : "OUT"), 
					Endpoint->Attributes, Endpoint->MaxPacketSize);

				// Update the hc-endpoint
				HcEndpoint->Address = EndpointAddress;
				HcEndpoint->MaxPacketSize = (Endpoint->MaxPacketSize & 0x7FF);
				HcEndpoint->Bandwidth = ((Endpoint->MaxPacketSize >> 11) & 0x3) + 1;
				HcEndpoint->Interval = Endpoint->Interval;
				HcEndpoint->Type = EndpointType;

				// Determine the direction of the EP
				if (Endpoint->Address & 0x80) {
					HcEndpoint->Direction = USB_ENDPOINT_IN;
				}	
				else {
					HcEndpoint->Direction = USB_ENDPOINT_OUT;
				}
				
				// Sanitize the endpoint count, we've experienced they
				// don't always match... which is awkward.
				if (Port->Device->Interfaces[Port->Device->Base.InterfaceCount - 1].
						Versions[CurrentIfVersion].Base.EndpointCount < (EpIterator + 1)) {

					// Yeah we got to correct it. Bad implementation
					Port->Device->Interfaces[
						Port->Device->Base.InterfaceCount - 1].
							Versions[CurrentIfVersion].Base.EndpointCount++;
				}

				// Increase the EP index
				EpIterator++;
			}

			// Go to next descriptor entry
		NextEntry:
			BufferPointer += Length;
			BytesLeft -= Length;
		}
	}
	else {
		// Cleanup the allocated buffer
		free(Port->Device->Descriptors);
		Port->Device->Descriptors = NULL;
	}

	// Done
	return Result.Status;
}

/* UsbFunctionSetConfiguration
 * Updates the configuration of an usb-device. This changes active endpoints. */
UsbTransferStatus_t
UsbFunctionSetConfiguration(
	_In_ UsbController_t *Controller, 
	_In_ UsbPort_t *Port, 
	_In_ size_t Configuration)
{
	// Variables
	uintptr_t *PacketBuffer = NULL;

	// Buffers
	UsbTransferResult_t Result = { 0 };
	UsbTransfer_t Transfer = { 0 };
	UsbPacket_t Packet = { 0 };

	// Initialize the packet
	Packet.Direction = USBPACKET_DIRECTION_OUT;
	Packet.Type = USBPACKET_TYPE_SET_CONFIG;
	Packet.ValueHi = 0;
	Packet.ValueLo = (Configuration & 0xFF);
	Packet.Index = 0;
	Packet.Length = 0;		// No data for us

	// Initialize transfer
	UsbTransferInitialize(Port, ControlTransfer,
		&Port->Device->ControlEndpoint, &Transfer);

	// SetConfiguration request consists of two transactions
	// Setup and In (ACK)
	UsbTransferSetup(&Transfer, &Packet, &PacketBuffer);
	UsbTransferIn(&Transfer, 1, 0, 0, 1);

	// Execute the transaction and cleanup
	// the buffer
	UsbTransferSend(Controller, Port, &Transfer, &Result);

	// Cleanup allocations
	BufferPoolFree(UsbCoreGetBufferPool(), PacketBuffer);

	// Done!
	return Result.Status;
}

/* UsbFunctionGetStringLanguages
 * Gets the device string language descriptors (Index 0). This automatically stores the available
 * languages in the device structure. */
UsbTransferStatus_t
UsbFunctionGetStringLanguages(
	_In_ UsbController_t *Controller, 
	_In_ UsbPort_t *Port)
{
	// Variables
	UsbStringDescriptor_t *Descriptor = NULL;
	uintptr_t *DescriptorVirtual = NULL;
	uintptr_t DescriptorPhysical = 0;
	uintptr_t *PacketBuffer = NULL;
	int i;

	// Buffers
	UsbTransferResult_t Result = { 0 };
	UsbTransfer_t Transfer = { 0 };
	UsbPacket_t Packet = { 0 };

	// Initialize the packet
	Packet.Direction = USBPACKET_DIRECTION_IN;
	Packet.Type = USBPACKET_TYPE_GET_DESC;
	Packet.ValueHi = USB_DESCRIPTOR_STRING;
	Packet.ValueLo = 0;
	Packet.Index = 0;
	Packet.Length = sizeof(UsbStringDescriptor_t);

	// Allocate a data-buffer
	if (BufferPoolAllocate(UsbCoreGetBufferPool(),
		sizeof(UsbStringDescriptor_t), &DescriptorVirtual, 
		&DescriptorPhysical) != OsSuccess) {
		return TransferInvalidData;
	}

	// Initialize pointer
	Descriptor = (UsbStringDescriptor_t*)DescriptorVirtual;

	// Initialize transfer
	UsbTransferInitialize(Port, ControlTransfer,
		&Port->Device->ControlEndpoint, &Transfer);

	// GetInitialConfigDescriptor request consists of three transactions
	// Setup, In (Data) and Out (ACK)
	UsbTransferSetup(&Transfer, &Packet, &PacketBuffer);
	UsbTransferIn(&Transfer, 1, DescriptorPhysical, sizeof(UsbStringDescriptor_t), 0);
	UsbTransferOut(&Transfer, 2, 0, 0, 1);

	// Execute the transaction and cleanup
	// the buffer
	UsbTransferSend(Controller, Port, &Transfer, &Result);

	// Update the device structure with the queried langauges
	if (Result.Status == TransferFinished) {
		
		// Get count
		Port->Device->Base.LanguageCount = (Descriptor->Length - 2) / 2;

		// Fill the list
		if (Port->Device->Base.LanguageCount > 0) {
			for (i = 0; i < Port->Device->Base.LanguageCount; i++) {
				Port->Device->Base.Languages[i] = Descriptor->Data[i];
			}
		}
	}

	// Cleanup allocations
	BufferPoolFree(UsbCoreGetBufferPool(), DescriptorVirtual);
	BufferPoolFree(UsbCoreGetBufferPool(), PacketBuffer);

	// Done
	return Result.Status;
}

/* UsbFunctionGetStringDescriptor
 * Queries the usb device for a string with the given language and index. */
UsbTransferStatus_t
UsbFunctionGetStringDescriptor(
	_In_ UsbController_t *Controller, 
	_In_ UsbPort_t *Port, 
	_In_ size_t LanguageId, 
	_In_ size_t StringIndex, 
	_Out_ char *String)
{
	// Variables
	char *StringBuffer = NULL;
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
	Packet.ValueHi = USB_DESCRIPTOR_STRING;
	Packet.ValueLo = (uint8_t)StringIndex;
	Packet.Index = (uint16_t)LanguageId;
	Packet.Length = 64;

	// Allocate a data-buffer
	if (BufferPoolAllocate(UsbCoreGetBufferPool(),
		64, &DescriptorVirtual, 
		&DescriptorPhysical) != OsSuccess) {
		return TransferInvalidData;
	}

	// Initialize pointer
	StringBuffer = (char*)DescriptorVirtual;

	// Initialize transfer
	UsbTransferInitialize(Port, ControlTransfer,
		&Port->Device->ControlEndpoint, &Transfer);

	// GetInitialConfigDescriptor request consists of three transactions
	// Setup, In (Data) and Out (ACK)
	UsbTransferSetup(&Transfer, &Packet, &PacketBuffer);
	UsbTransferIn(&Transfer, 1, DescriptorPhysical, 64, 0);
	UsbTransferOut(&Transfer, 2, 0, 0, 1);

	// Execute the transaction and cleanup
	// the buffer
	UsbTransferSend(Controller, Port, &Transfer, &Result);

	// Update the out variable with the string
	if (Result.Status == TransferFinished) {
		/* Convert to Utf8 */
		//size_t StringLength = (*((uint8_t*)TempBuffer + 1) - 2);
		_CRT_UNUSED(StringBuffer);
	}

	// Cleanup allocations
	BufferPoolFree(UsbCoreGetBufferPool(), DescriptorVirtual);
	BufferPoolFree(UsbCoreGetBufferPool(), PacketBuffer);

	// Done
	return Result.Status;
}

/* UsbFunctionClearFeature
 * Indicates to an usb-device that we want to request a feature/state disabled. */
UsbTransferStatus_t
UsbFunctionClearFeature(
	_In_ UsbController_t *Controller, 
	_In_ UsbPort_t *Port,
	_In_ uint8_t Target, 
	_In_ uint16_t Index, 
	_In_ uint16_t Feature)
{
	// Variables
	uintptr_t *PacketBuffer = NULL;

	// Buffers
	UsbTransferResult_t Result = { 0 };
	UsbTransfer_t Transfer = { 0 };
	UsbPacket_t Packet = { 0 };

	// Initialize the packet
	Packet.Direction = Target;
	Packet.Type = USBPACKET_TYPE_CLR_FEATURE;
	Packet.ValueHi = ((Feature >> 8) & 0xFF);
	Packet.ValueLo = (Feature & 0xFF);
	Packet.Index = Index;
	Packet.Length = 0;		// No data for us

	// Initialize transfer
	UsbTransferInitialize(Port, ControlTransfer,
		&Port->Device->ControlEndpoint, &Transfer);

	// SetConfiguration request consists of two transactions
	// Setup and In (ACK)
	UsbTransferSetup(&Transfer, &Packet, &PacketBuffer);
	UsbTransferIn(&Transfer, 1, 0, 0, 1);

	// Execute the transaction and cleanup
	// the buffer
	UsbTransferSend(Controller, Port, &Transfer, &Result);

	// Cleanup allocations
	BufferPoolFree(UsbCoreGetBufferPool(), PacketBuffer);

	// Done!
	return Result.Status;
}

/* UsbFunctionSetFeature
 * Indicates to an usb-device that we want to request a feature/state enabled. */
UsbTransferStatus_t
UsbFunctionSetFeature(
	_In_ UsbController_t *Controller, 
	_In_ UsbPort_t *Port,
	_In_ uint8_t Target, 
	_In_ uint16_t Index, 
	_In_ uint16_t Feature)
{
	// Variables
	uintptr_t *PacketBuffer = NULL;

	// Buffers
	UsbTransferResult_t Result = { 0 };
	UsbTransfer_t Transfer = { 0 };
	UsbPacket_t Packet = { 0 };

	// Initialize the packet
	Packet.Direction = Target;
	Packet.Type = USBPACKET_TYPE_SET_FEATURE;
	Packet.ValueHi = ((Feature >> 8) & 0xFF);
	Packet.ValueLo = (Feature & 0xFF);
	Packet.Index = Index;
	Packet.Length = 0;		// No data for us

	// Initialize transfer
	UsbTransferInitialize(Port, ControlTransfer,
		&Port->Device->ControlEndpoint, &Transfer);

	// SetConfiguration request consists of two transactions
	// Setup and In (ACK)
	UsbTransferSetup(&Transfer, &Packet, &PacketBuffer);
	UsbTransferIn(&Transfer, 1, 0, 0, 1);

	// Execute the transaction and cleanup
	// the buffer
	UsbTransferSend(Controller, Port, &Transfer, &Result);

	// Cleanup allocations
	BufferPoolFree(UsbCoreGetBufferPool(), PacketBuffer);

	// Done!
	return Result.Status;
}
