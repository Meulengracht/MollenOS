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
    // Constants
    const size_t DESCRIPTOR_SIZE = 0x12; // Max Descriptor Length is 18 bytes

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
	Packet->Length = DESCRIPTOR_SIZE;	

	// Allocate a data-buffer
    if (BufferPoolAllocate(__LibUsbBufferPool, DESCRIPTOR_SIZE, 
            &DescriptorVirtual, &DescriptorPhysical) != OsSuccess) {
        BufferPoolFree(__LibUsbBufferPool, PacketBuffer);
		return TransferInvalidData;
	}

	// Initialize pointer
	Descriptor = (UsbDeviceDescriptor_t*)DescriptorVirtual;

    // Initialize transfer
    // Setup, In (Data) and Out (ACK)
    UsbTransferInitialize(&Transfer, UsbDevice, 
        Endpoint, ControlTransfer);
    UsbTransferSetup(&Transfer, PacketPhysical, 
        DescriptorPhysical, DESCRIPTOR_SIZE, InTransaction);

	// Execute the transaction and cleanup the buffer
	if (UsbTransferQueue(Driver, Device, &Transfer, &Result) != OsSuccess) {
        ERROR("Usb transfer returned error");
        Result.Status = TransferNotProcessed;
    }

	// If the transfer finished correctly update the stored
	// device information to the queried
    if (Result.Status == TransferFinished && DeviceDescriptor != NULL) {
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
	_In_ UUId_t Driver,
	_In_ UUId_t Device,
	_In_ UsbHcDevice_t *UsbDevice, 
    _In_ UsbHcEndpointDescriptor_t *Endpoint,
    _Out_ UsbConfigDescriptor_t *ConfigDescriptor)
{
	// Variables
	UsbConfigDescriptor_t *Descriptor = NULL;
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
	Packet->ValueHi = USB_DESCRIPTOR_CONFIG;
	Packet->ValueLo = 0;
	Packet->Index = 0;
	Packet->Length = sizeof(UsbConfigDescriptor_t);

	// Allocate a data-buffer
	if (BufferPoolAllocate(__LibUsbBufferPool,
		sizeof(UsbConfigDescriptor_t), &DescriptorVirtual, 
		&DescriptorPhysical) != OsSuccess) {
        BufferPoolFree(__LibUsbBufferPool, PacketBuffer);
		return TransferInvalidData;
	}

	// Initialize pointer
	Descriptor = (UsbConfigDescriptor_t*)DescriptorVirtual;

	// Initialize transfer
    // Setup, In (Data) and Out (ACK)
    UsbTransferInitialize(&Transfer, UsbDevice, 
        Endpoint, ControlTransfer);
    UsbTransferSetup(&Transfer, PacketPhysical, DescriptorPhysical, 
        sizeof(UsbConfigDescriptor_t), InTransaction);

	// Execute the transaction and cleanup the buffer
	if (UsbTransferQueue(Driver, Device, &Transfer, &Result) != OsSuccess) {
        ERROR("Usb transfer returned error");
        Result.Status = TransferNotProcessed;
    }

	// Did it complete?
	if (Result.Status == TransferFinished) {
		memcpy(ConfigDescriptor, Descriptor, sizeof(DeviceDescriptor_t));
	}

	// Cleanup allocations
	BufferPoolFree(__LibUsbBufferPool, DescriptorVirtual);
	BufferPoolFree(__LibUsbBufferPool, PacketBuffer);

	// Done
	return Result.Status;
}

/* UsbFunctionGetConfigDescriptor
 * Queries the full configuration descriptor setup including all endpoints and interfaces.
 * This relies on the GetInitialConfigDescriptor. Also allocates all resources neccessary. */
UsbTransferStatus_t
UsbFunctionGetConfigDescriptor(
	_In_ UUId_t Driver,
	_In_ UUId_t Device,
	_In_ UsbHcDevice_t *UsbDevice, 
    _In_ UsbHcEndpointDescriptor_t *Endpoint,
    _Out_ void **ConfigDescriptorBuffer,
    _Out_ size_t *ConfigDescriptorBufferLength)
{
	// Variables
	uintptr_t *DescriptorVirtual = NULL;
	uintptr_t DescriptorPhysical = 0;
    uintptr_t *PacketBuffer = NULL;
	uintptr_t *PacketBuffer = NULL;
    uintptr_t PacketPhysical = 0;
	UsbPacket_t *Packet = NULL;

    // Buffers
    UsbConfigDescriptor_t Initial = { 0 };
	UsbTransferResult_t Result = { 0 };
    UsbTransfer_t Transfer = { 0 };
    
	// Make sure the initial configuration descriptor has been queried.
    Result.Status = UsbFunctionGetInitialConfigDescriptor(Driver, Device,
        UsbDevice, Endpoint, &Initial);
	if (Result.Status != TransferFinished) {
		return Result.Status;
	}

	// Allocate buffers
    if (BufferPoolAllocate(__LibUsbBufferPool, 8, 
            &PacketBuffer, &PacketPhysical) != OsSuccess) {
		return TransferInvalidData;
	}

    // Initialize the packet
    Packet = (UsbPacket_t*)PacketBuffer;
	Packet->Direction = USBPACKET_DIRECTION_IN;
	Packet->Type = USBPACKET_TYPE_GET_DESC;
	Packet->ValueHi = USB_DESCRIPTOR_CONFIG;
	Packet->ValueLo = 0;
	Packet->Index = 0;
	Packet->Length = Initial.TotalLength;

	// Allocate a data-buffer
    if (BufferPoolAllocate(__LibUsbBufferPool, Initial.TotalLength, 
        &DescriptorVirtual, &DescriptorPhysical) != OsSuccess) {
        BufferPoolFree(__LibUsbBufferPool, PacketBuffer);
		return TransferInvalidData;
	}
	
	// Initialize transfer
    // Setup, In (Data) and Out (ACK)
    UsbTransferInitialize(&Transfer, UsbDevice, 
        Endpoint, ControlTransfer);
    UsbTransferSetup(&Transfer, PacketPhysical, DescriptorPhysical, 
        sizeof(UsbConfigDescriptor_t), InTransaction);

	// Execute the transaction and cleanup the buffer
	if (UsbTransferQueue(Driver, Device, &Transfer, &Result) != OsSuccess) {
        ERROR("Usb transfer returned error");
        Result.Status = TransferNotProcessed;
    }

    // Did it complete?
	if (Result.Status == TransferFinished) {
        void *Buffer = malloc(Initial.TotalLength);
        memcpy(Buffer, DescriptorVirtual, Initial.TotalLength);
        *ConfigDescriptorBuffer = Buffer;
        *ConfigDescriptorBufferLength = Initial.TotalLength;
	}
	else {
        *ConfigDescriptorBuffer = NULL;
        *ConfigDescriptorBufferLength = 0;
	}

	// Cleanup allocations
	BufferPoolFree(UsbCoreGetBufferPool(), DescriptorVirtual);
	BufferPoolFree(UsbCoreGetBufferPool(), PacketBuffer);

	// Done
	return Result.Status;
}

/* UsbFunctionSetConfiguration
 * Updates the configuration of an usb-device. This changes active endpoints. */
UsbTransferStatus_t
UsbFunctionSetConfiguration(
	_In_ UUId_t Driver,
	_In_ UUId_t Device,
	_In_ UsbHcDevice_t *UsbDevice, 
    _In_ UsbHcEndpointDescriptor_t *Endpoint,
	_In_ int Configuration)
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

    // Initialize the packet
    Packet = (UsbPacket_t*)PacketBuffer;
	Packet->Direction = USBPACKET_DIRECTION_OUT;
	Packet->Type = USBPACKET_TYPE_SET_CONFIG;
	Packet->ValueHi = 0;
	Packet->ValueLo = (Configuration & 0xFF);
	Packet->Index = 0;
	Packet->Length = 0;		// No data for us

	// Initialize transfer
	UsbTransferInitialize(&Transfer, UsbDevice, Endpoint, ControlTransfer);

	// SetConfiguration does not have a data-stage
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

/* UsbFunctionGetStringLanguages
 * Gets the device string language descriptors (Index 0). The retrieved string descriptors are
 * stored in the given descriptor storage. */
UsbTransferStatus_t
UsbFunctionGetStringLanguages(
	_In_ UUId_t Driver,
	_In_ UUId_t Device,
	_In_ UsbHcDevice_t *UsbDevice, 
    _In_ UsbHcEndpointDescriptor_t *Endpoint,
    _Out_ UsbStringDescriptor_t *StringDescriptor)
{
	// Variables
	UsbConfigDescriptor_t *Descriptor = NULL;
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
	Packet->ValueHi = USB_DESCRIPTOR_STRING;
	Packet->ValueLo = 0;
	Packet->Index = 0;
	Packet->Length = sizeof(UsbStringDescriptor_t);

	// Allocate a data-buffer
	if (BufferPoolAllocate(__LibUsbBufferPool,
		sizeof(UsbStringDescriptor_t), &DescriptorVirtual, 
		&DescriptorPhysical) != OsSuccess) {
        BufferPoolFree(__LibUsbBufferPool, PacketBuffer);
		return TransferInvalidData;
	}

	// Initialize pointer
    Descriptor = (UsbStringDescriptor_t*)DescriptorVirtual;
    
    // Initialize transfer
    // Setup, In (Data) and Out (ACK)
    UsbTransferInitialize(&Transfer, UsbDevice, 
        Endpoint, ControlTransfer);
    UsbTransferSetup(&Transfer, PacketPhysical, DescriptorPhysical, 
        sizeof(UsbStringDescriptor_t), InTransaction);

	// Execute the transaction and cleanup the buffer
	if (UsbTransferQueue(Driver, Device, &Transfer, &Result) != OsSuccess) {
        ERROR("Usb transfer returned error");
        Result.Status = TransferNotProcessed;
    }

	// Update the device structure with the queried langauges
	if (Result.Status == TransferFinished) {
		memcpy(StringDescriptor, Descriptor, sizeof(StringDescriptor_t));
	}

	// Cleanup allocations
	BufferPoolFree(__LibUsbBufferPool, DescriptorVirtual);
	BufferPoolFree(__LibUsbBufferPool, PacketBuffer);

	// Done
	return Result.Status;
}

/* UsbFunctionGetStringDescriptor
 * Queries the usb device for a string with the given language and index. */
UsbTransferStatus_t
UsbFunctionGetStringDescriptor(
	_In_ UUId_t Driver,
	_In_ UUId_t Device,
	_In_ UsbHcDevice_t *UsbDevice, 
    _In_ UsbHcEndpointDescriptor_t *Endpoint,
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
	_In_ UUId_t Driver,
	_In_ UUId_t Device,
	_In_ UsbHcDevice_t *UsbDevice, 
    _In_ UsbHcEndpointDescriptor_t *Endpoint,
	_In_ uint8_t Target, 
	_In_ uint16_t Index, 
	_In_ uint16_t Feature)
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

    // Initialize the packet
    Packet = (UsbPacket_t*)PacketBuffer;
	Packet->Direction = Target;
	Packet->Type = USBPACKET_TYPE_CLR_FEATURE;
	Packet->ValueHi = ((Feature >> 8) & 0xFF);
	Packet->ValueLo = (Feature & 0xFF);
	Packet->Index = Index;
	Packet->Length = 0;		// No data for us

	// Initialize transfer
	UsbTransferInitialize(&Transfer, UsbDevice, Endpoint, ControlTransfer);

	// ClearFeature does not have a data-stage
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

/* UsbFunctionSetFeature
 * Indicates to an usb-device that we want to request a feature/state enabled. */
UsbTransferStatus_t
UsbFunctionSetFeature(
	_In_ UUId_t Driver,
	_In_ UUId_t Device,
	_In_ UsbHcDevice_t *UsbDevice, 
    _In_ UsbHcEndpointDescriptor_t *Endpoint,
	_In_ uint8_t Target, 
	_In_ uint16_t Index, 
	_In_ uint16_t Feature)
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

    // Initialize the packet
    Packet = (UsbPacket_t*)PacketBuffer;
	Packet->Direction = Target;
	Packet->Type = USBPACKET_TYPE_SET_FEATURE;
	Packet->ValueHi = ((Feature >> 8) & 0xFF);
	Packet->ValueLo = (Feature & 0xFF);
	Packet->Index = Index;
	Packet->Length = 0;		// No data for us

	// Initialize transfer
	UsbTransferInitialize(&Transfer, UsbDevice, Endpoint, ControlTransfer);

	// SetFeature does not have a data-stage
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
