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
#include <os/utils.h>

/* Includes
 * - Library */
#include <threads.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

/* Globals
 * State-keeping variables for libusb */
static const size_t LIBUSB_SHAREDBUFFER_SIZE = 0x2000;
static BufferObject_t *__LibUsbBuffer = NULL;
static BufferPool_t *__LibUsbBufferPool = NULL;

/* UsbInitialize
 * Initializes libusb and enables the use of all the control
 * functions that require a shared buffer-pool. */
OsStatus_t
UsbInitialize(void)
{
    // Create buffer and pool
    __LibUsbBuffer = CreateBuffer(LIBUSB_SHAREDBUFFER_SIZE);
    return BufferPoolCreate(__LibUsbBuffer, &__LibUsbBufferPool);
}

/* UsbCleanup
 * Frees the shared resources allocated by UsbInitialize. */
OsStatus_t
UsbCleanup(void)
{
    // Free resources
    BufferPoolDestroy(__LibUsbBufferPool);
    return DestroyBuffer(__LibUsbBuffer);
}

/* UsbRetrievePool 
 * Retrieves the shared usb-memory pool for transfers. Only
 * use this for small short-term use buffers. */
__EXTERN
BufferPool_t*
UsbRetrievePool(void)
{
    return __LibUsbBufferPool;
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
    // Debug
    TRACE("UsbTransferInitialize()");

    // Initialize structure
    memset(Transfer, 0, sizeof(UsbTransfer_t));
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

    // Debug
    TRACE("UsbTransferSetup()");

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

/* UsbTransferPeriodic 
 * Initializes a transfer for a interrupt-transaction. */
OsStatus_t
UsbTransferPeriodic(
    _InOut_ UsbTransfer_t *Transfer,
    _In_ uintptr_t BufferAddress,
    _In_ size_t BufferLength,
    _In_ size_t DataLength,
    _In_ UsbTransactionType_t DataDirection,
    _In_ int Notify,
    _In_ __CONST void *NotifyData)
{
    // Sanitize, an interrupt transfer must not consist
    // of other transfers
    if (Transfer->TransactionCount != 0) {
        return OsError;
    }

    // Initialize the data stage
    Transfer->Transactions[0].Type = DataDirection;
    Transfer->Transactions[0].BufferAddress = BufferAddress;
    Transfer->Transactions[0].Length = DataLength;

    // Initialize the transfer for interrupt
    Transfer->UpdatesOn = Notify;
    Transfer->PeriodicData = NotifyData;
    Transfer->PeriodicBufferSize = BufferLength;
    Transfer->TransactionCount = 1;
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

    // Debug
    TRACE("UsbTransferQueue()");

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
UsbTransferStatus_t
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
    if (QueryDriver(&Contract, __USBHOST_QUEUEPERIODIC,
        &Device, sizeof(UUId_t), &Transfer->Pipe, sizeof(UUId_t), 
        Transfer, sizeof(UsbTransfer_t), 
        &Result, sizeof(UsbTransferResult_t)) != OsSuccess) {
        *TransferId = UUID_INVALID;
        return TransferInvalid;
    }
    else {
        *TransferId = Result.Id;
        return Result.Status;
    }
}

/* UsbTransferDequeuePeriodic 
 * Dequeues an existing periodic transfer from the given controller. The transfer
 * and the controller must be valid. */
UsbTransferStatus_t
UsbTransferDequeuePeriodic(
    _In_ UUId_t Driver,
    _In_ UUId_t Device,
    _In_ UUId_t TransferId)
{
    // Variables
    UsbTransferStatus_t Result = TransferNotProcessed;
    MContract_t Contract;

    // Setup contract stuff for request
    Contract.DriverId = Driver;
    Contract.Type = ContractController;
    Contract.Version = __USBMANAGER_INTERFACE_VERSION;

    // Query the driver directly
    if (QueryDriver(&Contract, __USBHOST_DEQUEUEPERIODIC,
        &Device, sizeof(UUId_t), 
        &TransferId, sizeof(UUId_t), 
        NULL, 0, &Result, sizeof(UsbTransferStatus_t)) != OsSuccess) {
        return TransferInvalid;
    }
    else {
        return Result;
    }
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

/* UsbEndpointReset
 * Resets the data for the given endpoint. This includes the data-toggles. 
 * This function is unavailable for control-endpoints. */
OsStatus_t
UsbEndpointReset(
    _In_ UUId_t Driver,
    _In_ UUId_t Device,
    _In_ UsbHcDevice_t *UsbDevice, 
    _In_ UsbHcEndpointDescriptor_t *Endpoint)
{
    // Variables
    OsStatus_t Result = OsError;
    UUId_t Pipe = ((UsbDevice->Address & 0xFFFF) << 16) | (Endpoint->Address & 0xFFFF);
    MContract_t Contract;

    // Setup contract stuff for request
    Contract.DriverId = Driver;
    Contract.Type = ContractController;
    Contract.Version = __USBMANAGER_INTERFACE_VERSION;

    // Query the driver directly
    if (QueryDriver(&Contract, __USBHOST_RESETENDPOINT,
        &Device, sizeof(UUId_t), 
        &Pipe, sizeof(UUId_t), NULL, 0, 
        &Result, sizeof(OsStatus_t)) != OsSuccess) {
        return OsError;
    }
    else {
        return Result;
    }
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

    // Debug
    TRACE("UsbSetAddress()");

    // Sanitize current address
    if (UsbDevice->Address != 0) {
        return TransferNotProcessed;
    }

    // Allocate buffers
    if (BufferPoolAllocate(__LibUsbBufferPool, 8, 
            &PacketBuffer, &PacketPhysical) != OsSuccess) {
        ERROR("Failed to allocate a transfer buffer");
        return TransferInvalid;
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
        ERROR("Failed to allocate a transfer buffer");
        return TransferInvalid;
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
        ERROR("Failed to allocate a transfer data buffer");
        BufferPoolFree(__LibUsbBufferPool, PacketBuffer);
        return TransferInvalid;
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
        memcpy(DeviceDescriptor, Descriptor, sizeof(UsbDeviceDescriptor_t));
    }

    // Cleanup allocations
    BufferPoolFree(__LibUsbBufferPool, DescriptorVirtual);
    BufferPoolFree(__LibUsbBufferPool, PacketBuffer);

    // Done
    return Result.Status;
}

/* UsbGetInitialConfigDescriptor
 * Queries the initial configuration descriptor, and is neccessary to know how
 * long the full configuration descriptor is. */
UsbTransferStatus_t
UsbGetInitialConfigDescriptor(
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
        ERROR("Failed to allocate a transfer buffer");
        return TransferInvalid;
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
        ERROR("Failed to allocate a transfer data buffer");
        BufferPoolFree(__LibUsbBufferPool, PacketBuffer);
        return TransferInvalid;
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
        memcpy(ConfigDescriptor, Descriptor, sizeof(UsbConfigDescriptor_t));
    }

    // Cleanup allocations
    BufferPoolFree(__LibUsbBufferPool, DescriptorVirtual);
    BufferPoolFree(__LibUsbBufferPool, PacketBuffer);

    // Done
    return Result.Status;
}

/* UsbGetConfigDescriptor
 * Queries the configuration descriptor. Ideally this function is called twice to get
 * the full configuration descriptor. Once to retrieve the actual descriptor, and then
 * twice to retrieve the full descriptor with all information. */
UsbTransferStatus_t
UsbGetConfigDescriptor(
    _In_ UUId_t Driver,
    _In_ UUId_t Device,
    _In_ UsbHcDevice_t *UsbDevice, 
    _In_ UsbHcEndpointDescriptor_t *Endpoint,
    _Out_ void *ConfigDescriptorBuffer,
    _Out_ size_t ConfigDescriptorBufferLength)
{
    // Variables
    uintptr_t *DescriptorVirtual = NULL;
    uintptr_t DescriptorPhysical = 0;
    uintptr_t *PacketBuffer = NULL;
    uintptr_t PacketPhysical = 0;
    UsbPacket_t *Packet = NULL;

    // Buffers
    UsbTransferResult_t Result = { 0 };
    UsbTransfer_t Transfer = { 0 };
    
    // Sanitize parameters
    if (ConfigDescriptorBuffer == NULL 
        || ConfigDescriptorBufferLength < sizeof(UsbConfigDescriptor_t)) {
        return TransferInvalid;
    }

    // Are we requesting the initial descriptor?
    if (ConfigDescriptorBufferLength == sizeof(UsbConfigDescriptor_t)) {
        return UsbGetInitialConfigDescriptor(Driver, Device, UsbDevice, 
            Endpoint, (UsbConfigDescriptor_t*)ConfigDescriptorBuffer);
    }

    // Allocate buffers
    if (BufferPoolAllocate(__LibUsbBufferPool, 8, 
            &PacketBuffer, &PacketPhysical) != OsSuccess) {
        ERROR("Failed to allocate a transfer buffer");
        return TransferInvalid;
    }

    // Initialize the packet
    Packet = (UsbPacket_t*)PacketBuffer;
    Packet->Direction = USBPACKET_DIRECTION_IN;
    Packet->Type = USBPACKET_TYPE_GET_DESC;
    Packet->ValueHi = USB_DESCRIPTOR_CONFIG;
    Packet->ValueLo = 0;
    Packet->Index = 0;
    Packet->Length = ConfigDescriptorBufferLength;

    // Allocate a data-buffer
    if (BufferPoolAllocate(__LibUsbBufferPool, ConfigDescriptorBufferLength, 
        &DescriptorVirtual, &DescriptorPhysical) != OsSuccess) {
        ERROR("Failed to allocate a transfer data buffer");
        BufferPoolFree(__LibUsbBufferPool, PacketBuffer);
        return TransferInvalid;
    }
    
    // Initialize transfer
    // Setup, In (Data) and Out (ACK)
    UsbTransferInitialize(&Transfer, UsbDevice, 
        Endpoint, ControlTransfer);
    UsbTransferSetup(&Transfer, PacketPhysical, DescriptorPhysical, 
        ConfigDescriptorBufferLength, InTransaction);

    // Execute the transaction and cleanup the buffer
    if (UsbTransferQueue(Driver, Device, &Transfer, &Result) != OsSuccess) {
        ERROR("Usb transfer returned error");
        Result.Status = TransferNotProcessed;
    }

    // Did it complete?
    if (Result.Status == TransferFinished) {
        memcpy(ConfigDescriptorBuffer, DescriptorVirtual, ConfigDescriptorBufferLength);
    }

    // Cleanup allocations
    BufferPoolFree(__LibUsbBufferPool, DescriptorVirtual);
    BufferPoolFree(__LibUsbBufferPool, PacketBuffer);

    // Done
    return Result.Status;
}

/* UsbSetConfiguration
 * Updates the configuration of an usb-device. This changes active endpoints. */
UsbTransferStatus_t
UsbSetConfiguration(
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
        ERROR("Failed to allocate a transfer buffer");
        return TransferInvalid;
    }

    // Initialize the packet
    Packet = (UsbPacket_t*)PacketBuffer;
    Packet->Direction = USBPACKET_DIRECTION_OUT;
    Packet->Type = USBPACKET_TYPE_SET_CONFIG;
    Packet->ValueHi = 0;
    Packet->ValueLo = (Configuration & 0xFF);
    Packet->Index = 0;
    Packet->Length = 0;        // No data for us

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

/* UsbGetStringLanguages
 * Gets the device string language descriptors (Index 0). The retrieved string descriptors are
 * stored in the given descriptor storage. */
UsbTransferStatus_t
UsbGetStringLanguages(
    _In_ UUId_t Driver,
    _In_ UUId_t Device,
    _In_ UsbHcDevice_t *UsbDevice, 
    _In_ UsbHcEndpointDescriptor_t *Endpoint,
    _Out_ UsbStringDescriptor_t *StringDescriptor)
{
    // Variables
    UsbStringDescriptor_t *Descriptor = NULL;
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
        ERROR("Failed to allocate a transfer buffer");
        return TransferInvalid;
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
        ERROR("Failed to allocate a transfer data buffer");
        BufferPoolFree(__LibUsbBufferPool, PacketBuffer);
        return TransferInvalid;
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
        memcpy(StringDescriptor, Descriptor, sizeof(UsbStringDescriptor_t));
    }

    // Cleanup allocations
    BufferPoolFree(__LibUsbBufferPool, DescriptorVirtual);
    BufferPoolFree(__LibUsbBufferPool, PacketBuffer);

    // Done
    return Result.Status;
}

/* UsbGetStringDescriptor
 * Queries the usb device for a string with the given language and index. 
 * The provided buffer must be of size at-least 64 bytes. */
UsbTransferStatus_t
UsbGetStringDescriptor(
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
    uintptr_t PacketPhysical = 0;
    UsbPacket_t *Packet = NULL;

    // Buffers
    UsbTransferResult_t Result = { 0 };
    UsbTransfer_t Transfer = { 0 };
    
    // Allocate buffers
    if (BufferPoolAllocate(__LibUsbBufferPool, 8, 
            &PacketBuffer, &PacketPhysical) != OsSuccess) {
        ERROR("Failed to allocate a transfer buffer");
        return TransferInvalid;
    }

    // Initialize the packet
    Packet = (UsbPacket_t*)PacketBuffer;
    Packet->Direction = USBPACKET_DIRECTION_IN;
    Packet->Type = USBPACKET_TYPE_GET_DESC;
    Packet->ValueHi = USB_DESCRIPTOR_STRING;
    Packet->ValueLo = (uint8_t)StringIndex;
    Packet->Index = (uint16_t)LanguageId;
    Packet->Length = 64;

    // Allocate a data-buffer
    if (BufferPoolAllocate(__LibUsbBufferPool, 64, 
        &DescriptorVirtual, &DescriptorPhysical) != OsSuccess) {
        ERROR("Failed to allocate a transfer data buffer");
        BufferPoolFree(__LibUsbBufferPool, PacketBuffer);
        return TransferInvalid;
    }

    // Initialize pointer
    StringBuffer = (char*)DescriptorVirtual;

    // Initialize transfer
    // Setup, In (Data) and Out (ACK)
    UsbTransferInitialize(&Transfer, UsbDevice, 
        Endpoint, ControlTransfer);
    UsbTransferSetup(&Transfer, PacketPhysical, DescriptorPhysical, 
        64, InTransaction);

    // Execute the transaction and cleanup the buffer
    if (UsbTransferQueue(Driver, Device, &Transfer, &Result) != OsSuccess) {
        ERROR("Usb transfer returned error");
        Result.Status = TransferNotProcessed;
    }

    // Update the out variable with the string
    if (Result.Status == TransferFinished) {
        /* Convert to Utf8 */
        //size_t StringLength = (*((uint8_t*)TempBuffer + 1) - 2);
        _CRT_UNUSED(StringBuffer);
    }

    // Cleanup allocations
    BufferPoolFree(__LibUsbBufferPool, DescriptorVirtual);
    BufferPoolFree(__LibUsbBufferPool, PacketBuffer);

    // Done
    return Result.Status;
}

/* UsbClearFeature
 * Indicates to an usb-device that we want to request a feature/state disabled. */
UsbTransferStatus_t
UsbClearFeature(
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
        ERROR("Failed to allocate a transfer buffer");
        return TransferInvalid;
    }

    // Initialize the packet
    Packet = (UsbPacket_t*)PacketBuffer;
    Packet->Direction = Target;
    Packet->Type = USBPACKET_TYPE_CLR_FEATURE;
    Packet->ValueHi = ((Feature >> 8) & 0xFF);
    Packet->ValueLo = (Feature & 0xFF);
    Packet->Index = Index;
    Packet->Length = 0;        // No data for us

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

/* UsbSetFeature
 * Indicates to an usb-device that we want to request a feature/state enabled. */
UsbTransferStatus_t
UsbSetFeature(
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
        ERROR("Failed to allocate a transfer buffer");
        return TransferInvalid;
    }

    // Initialize the packet
    Packet = (UsbPacket_t*)PacketBuffer;
    Packet->Direction = Target;
    Packet->Type = USBPACKET_TYPE_SET_FEATURE;
    Packet->ValueHi = ((Feature >> 8) & 0xFF);
    Packet->ValueLo = (Feature & 0xFF);
    Packet->Index = Index;
    Packet->Length = 0;        // No data for us

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

/* UsbExecutePacket
 * Executes a custom packet with or without a data-stage. Use this for vendor-specific
 * control requests. */
UsbTransferStatus_t
UsbExecutePacket(
    _In_ UUId_t Driver,
    _In_ UUId_t Device,
    _In_ UsbHcDevice_t *UsbDevice, 
    _In_ UsbHcEndpointDescriptor_t *Endpoint,
    _In_ uint8_t Direction,
    _In_ uint8_t Type,
    _In_ uint8_t ValueHigh,
    _In_ uint8_t ValueLow,
    _In_ uint16_t Index,
    _In_ uint16_t Length,
    _Out_ void *Buffer)
{
    // Variables
    UsbTransactionType_t DataStageType = OutTransaction;
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
        ERROR("Failed to allocate a transfer buffer");
        return TransferInvalid;
    }

    // Initialize the packet
    Packet = (UsbPacket_t*)PacketBuffer;
    Packet->Direction = Direction;
    Packet->Type = Type;
    Packet->ValueHi = ValueHigh;
    Packet->ValueLo = ValueLow;
    Packet->Index = Index;
    Packet->Length = Length;

    // Allocate a data-buffer
    if (Length != 0 && BufferPoolAllocate(__LibUsbBufferPool,
        Length, &DescriptorVirtual, &DescriptorPhysical) != OsSuccess) {
        ERROR("Failed to allocate a transfer data buffer");
        BufferPoolFree(__LibUsbBufferPool, PacketBuffer);
        return TransferInvalid;
    }

    // Get direction
    if (Direction & USBPACKET_DIRECTION_IN) {
        DataStageType = InTransaction;
    }

    // Copy data if out
    if (DataStageType == OutTransaction 
        && Length != 0 && Buffer != NULL) {
        memcpy(DescriptorVirtual, Buffer, Length);
    }

    // Initialize setup transfer
    UsbTransferInitialize(&Transfer, UsbDevice, Endpoint, ControlTransfer);
    UsbTransferSetup(&Transfer, PacketPhysical, DescriptorPhysical, Length, DataStageType);

    // Execute the transaction and cleanup the buffer
    if (UsbTransferQueue(Driver, Device, &Transfer, &Result) != OsSuccess) {
        ERROR("Usb transfer returned error");
        Result.Status = TransferNotProcessed;
    }

    // Update the device structure with the queried langauges
    if (Result.Status == TransferFinished && Length != 0 
        && Buffer != NULL && DataStageType == InTransaction) {
        memcpy(Buffer, DescriptorVirtual, Length);
    }

    // Cleanup allocations
    if (Length != 0) {
        BufferPoolFree(__LibUsbBufferPool, DescriptorVirtual);
    }
    BufferPoolFree(__LibUsbBufferPool, PacketBuffer);

    // Done
    return Result.Status;
}
