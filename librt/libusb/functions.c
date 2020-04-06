/**
 * MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 * Usb function library
 * - Contains helper functions for interacting with usb devices. 
 */
//#define __TRACE

#include <ddk/contracts/usbhost.h>
#include <ddk/usb.h>
#include <ddk/bufferpool.h>
#include <ddk/driver.h>
#include <ddk/utils.h>
#include <os/mollenos.h>
#include <os/dmabuf.h>
#include <threads.h>
#include <stdlib.h>
#include <string.h>

static const size_t          LIBUSB_SHAREDBUFFER_SIZE = 0x2000;
static struct dma_pool*      DmaPool                  = NULL;
static struct dma_attachment DmaAttachment;

OsStatus_t
UsbInitialize(void)
{
    struct dma_buffer_info info;
    OsStatus_t             status;
    
    info.length   = LIBUSB_SHAREDBUFFER_SIZE;
    info.capacity = LIBUSB_SHAREDBUFFER_SIZE;
    info.flags    = 0;
    
    status = dma_create(&info, &DmaAttachment);
    if (status != OsSuccess) {
        return status;
    }
    
    status = dma_pool_create(&DmaAttachment, &DmaPool);
    if (status != OsSuccess) {
        (void)dma_detach(&DmaAttachment);
    }
    return status;
}

OsStatus_t
UsbCleanup(void)
{
    if (!DmaPool) {
        return OsNotSupported;
    }

    dma_pool_destroy(DmaPool);
    dma_detach(&DmaAttachment);
    DmaPool = NULL;
    return OsSuccess;
}

struct dma_pool*
UsbRetrievePool(void)
{
    return DmaPool;
}

OsStatus_t
UsbTransferInitialize(
    _In_ UsbTransfer_t*             Transfer,
    _In_ UsbHcDevice_t*             Device,
    _In_ UsbHcEndpointDescriptor_t* Endpoint,
    _In_ UsbTransferType_t          Type,
    _In_ Flags_t                    Flags)
{
    TRACE("UsbTransferInitialize(%" PRIuIN ")", Endpoint->Address);

    memset(Transfer, 0, sizeof(UsbTransfer_t));
    memcpy(&Transfer->Endpoint, Endpoint, sizeof(UsbHcEndpointDescriptor_t));

    Transfer->Flags = Flags;
    Transfer->Type  = Type;
    Transfer->Speed = Device->Speed;

    Transfer->Address.HubAddress      = Device->HubAddress;
    Transfer->Address.PortAddress     = Device->PortAddress;
    Transfer->Address.DeviceAddress   = Device->DeviceAddress;
    Transfer->Address.EndpointAddress = (uint8_t)(Endpoint->Address & 0xFF);
    return OsSuccess;
}

OsStatus_t
UsbTransferSetup(
    _In_ UsbTransfer_t*       Transfer,
    _In_ UUId_t               SetupBufferHandle,
    _In_ size_t               SetupBufferOffset,
    _In_ UUId_t               DataBufferHandle,
    _In_ size_t               DataBufferOffset,
    _In_ size_t               DataLength,
    _In_ UsbTransactionType_t DataType)
{
    UsbTransactionType_t AckType  = InTransaction;
    int                  AckIndex = 1;

    TRACE("UsbTransferSetup()");

    // Initialize the setup stage
    Transfer->Transactions[0].Type         = SetupTransaction;
    Transfer->Transactions[0].BufferHandle = SetupBufferHandle;
    Transfer->Transactions[0].BufferOffset = SetupBufferOffset;
    Transfer->Transactions[0].Length       = sizeof(UsbPacket_t);

    // Is there a data-stage?
    if (DataBufferHandle != UUID_INVALID) {
        AckIndex++;
        Transfer->Transactions[1].BufferHandle = DataBufferHandle;
        Transfer->Transactions[1].BufferOffset = DataBufferOffset;
        Transfer->Transactions[1].Length       = DataLength;
        Transfer->Transactions[1].Type         = DataType;
        if (DataType == InTransaction) {
            AckType = OutTransaction;
        }
    }

    // Ack-stage
    Transfer->Transactions[AckIndex].Flags        = USB_TRANSACTION_ZLP | USB_TRANSACTION_HANDSHAKE;
    Transfer->Transactions[AckIndex].BufferHandle = UUID_INVALID;
    Transfer->Transactions[AckIndex].Type         = AckType;
    Transfer->TransactionCount                    = AckIndex + 1;
    return OsSuccess;
}

OsStatus_t
UsbTransferPeriodic(
    _In_ UsbTransfer_t*       Transfer,
    _In_ UUId_t               BufferHandle,
    _In_ size_t               BufferOffset,
    _In_ size_t               BufferLength,
    _In_ size_t               DataLength,
    _In_ UsbTransactionType_t DataDirection,
    _In_ const void*          NotifificationData)
{
    // Sanitize, an interrupt transfer must not consist
    // of other transfers
    if (Transfer->TransactionCount != 0) {
        return OsError;
    }

    // Initialize the data stage
    Transfer->Transactions[0].Type         = DataDirection;
    Transfer->Transactions[0].BufferHandle = BufferHandle;
    Transfer->Transactions[0].BufferOffset = BufferOffset;
    Transfer->Transactions[0].Length       = DataLength;

    // Initialize the transfer for interrupt
    Transfer->PeriodicData                  = NotifificationData;
    Transfer->PeriodicBufferSize            = BufferLength;
    Transfer->TransactionCount              = 1;
    return OsSuccess;
}

OsStatus_t
UsbTransferIn(
	_In_ UsbTransfer_t* Transfer,
    _In_ UUId_t         BufferHandle,
    _In_ size_t         BufferOffset,
	_In_ size_t         Length,
    _In_ int            Handshake)
{
    UsbTransaction_t* Transaction;

    // Sanitize count
    if (Transfer->TransactionCount >= 3) {
        return OsError;
    }

    Transaction               = &Transfer->Transactions[Transfer->TransactionCount++];
    Transaction->Type         = InTransaction;
    Transaction->BufferHandle = BufferHandle;
    Transaction->BufferOffset = BufferOffset;
    Transaction->Length       = Length;
    Transaction->Flags        = Handshake ? USB_TRANSACTION_ZLP : 0; 
    
    if (Length == 0) { // Zero-length?
        Transaction->Flags |= USB_TRANSACTION_ZLP;
    }
    return OsSuccess;
}

OsStatus_t
UsbTransferOut(
	_In_ UsbTransfer_t* Transfer,
    _In_ UUId_t         BufferHandle,
    _In_ size_t         BufferOffset,
	_In_ size_t         Length,
    _In_ int            Handshake)
{
    UsbTransaction_t* Transaction;

    // Sanitize count
    if (Transfer->TransactionCount >= 3) {
        return OsError;
    }

    Transaction               = &Transfer->Transactions[Transfer->TransactionCount++];
    Transaction->Type         = OutTransaction;
    Transaction->BufferHandle = BufferHandle;
    Transaction->BufferOffset = BufferOffset;
    Transaction->Length       = Length;
    Transaction->Flags        = Handshake ? USB_TRANSACTION_ZLP : 0; 
    
    if (Length == 0) { // Zero-length?
        Transaction->Flags |= USB_TRANSACTION_ZLP;
    }
    return OsSuccess;
}

OsStatus_t
UsbTransferQueue(
	_In_  UUId_t                InterfaceId,
	_In_  UUId_t                DeviceId,
	_In_  UsbTransfer_t*        Transfer,
	_Out_ UsbTransferResult_t** Result)
{
    IpcMessage_t Message;
    OsStatus_t   Status;
    
    IpcInitialize(&Message);
    
    IPC_SET_TYPED(&Message, 0, __DRIVER_QUERYCONTRACT);
    IPC_SET_TYPED(&Message, 1, __USBHOST_QUEUETRANSFER);
    IPC_SET_TYPED(&Message, 2, ContractController);
    IPC_SET_TYPED(&Message, 3, DeviceId);
    
    IpcSetUntypedArgument(&Message, 0, Transfer, sizeof(UsbTransfer_t));
    
    Status = IpcInvoke(InterfaceId, &Message, 0, 0, (void**)Result);
    return Status;
}

UsbTransferStatus_t
UsbTransferQueuePeriodic(
	_In_  UUId_t         InterfaceId,
	_In_  UUId_t         DeviceId,
	_In_  UsbTransfer_t* Transfer,
	_Out_ UUId_t*        TransferId)
{
    UsbTransferResult_t* Result;
    IpcMessage_t         Message;
    OsStatus_t           Status;
    
    IpcInitialize(&Message);
    
    IPC_SET_TYPED(&Message, 0, __DRIVER_QUERYCONTRACT);
    IPC_SET_TYPED(&Message, 1, __USBHOST_QUEUEPERIODIC);
    IPC_SET_TYPED(&Message, 2, ContractController);
    IPC_SET_TYPED(&Message, 3, DeviceId);
    
    IpcSetUntypedArgument(&Message, 0, Transfer, sizeof(UsbTransfer_t));
    
    Status = IpcInvoke(InterfaceId, &Message, 0, 0, (void**)&Result);
    if (Status == OsSuccess) {
        *TransferId = Result->Id;
        return Result->Status;
    }
    return TransferInvalid;
}

UsbTransferStatus_t
UsbTransferDequeuePeriodic(
	_In_ UUId_t InterfaceId,
	_In_ UUId_t DeviceId,
	_In_ UUId_t TransferId)
{
    UsbTransferStatus_t* Result;
    IpcMessage_t         Message;
    OsStatus_t           Status;
    
    IpcInitialize(&Message);
    
    IPC_SET_TYPED(&Message, 0, __DRIVER_QUERYCONTRACT);
    IPC_SET_TYPED(&Message, 1, __USBHOST_DEQUEUEPERIODIC);
    IPC_SET_TYPED(&Message, 2, ContractController);
    IPC_SET_TYPED(&Message, 3, DeviceId);
    IPC_SET_TYPED(&Message, 4, TransferId);
    
    Status = IpcInvoke(InterfaceId, &Message, 0, 0, (void**)&Result);
    if (Status == OsSuccess) {
        return IPC_CAST_AND_DEREF(Result, UsbTransferStatus_t);
    }
    return TransferInvalid;
}

OsStatus_t
UsbHubResetPort(
	_In_  UUId_t                  InterfaceId,
	_In_  UUId_t                  DeviceId,
	_In_  uint8_t                 PortAddress,
	_Out_ UsbHcPortDescriptor_t** Descriptor)
{
    IpcMessage_t Message;
    OsStatus_t   Status;
    
    IpcInitialize(&Message);
    
    IPC_SET_TYPED(&Message, 0, __DRIVER_QUERYCONTRACT);
    IPC_SET_TYPED(&Message, 1, __USBHOST_RESETPORT);
    IPC_SET_TYPED(&Message, 2, ContractController);
    IPC_SET_TYPED(&Message, 3, DeviceId);
    IPC_SET_TYPED(&Message, 4, PortAddress);
    
    Status = IpcInvoke(InterfaceId, &Message, 0, 0, (void**)Descriptor);
    return Status;
}

OsStatus_t
UsbHubQueryPort(
	_In_  UUId_t                  InterfaceId,
	_In_  UUId_t                  DeviceId,
	_In_  uint8_t                 PortAddress,
	_Out_ UsbHcPortDescriptor_t** Descriptor)
{
    IpcMessage_t Message;
    OsStatus_t   Status;
    
    IpcInitialize(&Message);
    
    IPC_SET_TYPED(&Message, 0, __DRIVER_QUERYCONTRACT);
    IPC_SET_TYPED(&Message, 1, __USBHOST_QUERYPORT);
    IPC_SET_TYPED(&Message, 2, ContractController);
    IPC_SET_TYPED(&Message, 3, DeviceId);
    IPC_SET_TYPED(&Message, 4, PortAddress);
    
    Status = IpcInvoke(InterfaceId, &Message, 0, 0, (void**)Descriptor);
    return Status;
}

OsStatus_t
UsbEndpointReset(
	_In_ UUId_t                     InterfaceId,
	_In_ UUId_t                     DeviceId,
    _In_ UsbHcDevice_t*             UsbDevice, 
    _In_ UsbHcEndpointDescriptor_t* Endpoint)
{
    OsStatus_t*    Result;
    OsStatus_t     Status;
    IpcMessage_t   Message;
    UsbHcAddress_t Address;

    Address.HubAddress      = UsbDevice->HubAddress;
    Address.PortAddress     = UsbDevice->PortAddress;
    Address.DeviceAddress   = UsbDevice->DeviceAddress;
    Address.EndpointAddress = Endpoint->Address;
    
    IpcInitialize(&Message);
    
    IPC_SET_TYPED(&Message, 0, __DRIVER_QUERYCONTRACT);
    IPC_SET_TYPED(&Message, 1, __USBHOST_RESETENDPOINT);
    IPC_SET_TYPED(&Message, 2, ContractController);
    IPC_SET_TYPED(&Message, 3, DeviceId);
    
    IpcSetUntypedArgument(&Message, 0, &Address, sizeof(UsbHcAddress_t));
    
    Status = IpcInvoke(InterfaceId, &Message, 0, 0, (void**)&Result);
    if (Status == OsSuccess) {
        return IPC_CAST_AND_DEREF(Result, OsStatus_t);
    }
    return Status;
}

UsbTransferStatus_t
UsbSetAddress(
	_In_ UUId_t                     Driver,
	_In_ UUId_t                     Device,
	_In_ UsbHcDevice_t*             UsbDevice, 
	_In_ UsbHcEndpointDescriptor_t* Endpoint, 
    _In_ int                        Address)
{
    void*        PacketBuffer = NULL;
    UsbPacket_t* Packet;

    UsbTransferResult_t* Result;
    UsbTransfer_t        Transfer = { 0 };

    // Debug
    TRACE("UsbSetAddress()");

    // Sanitize current address
    if (UsbDevice->DeviceAddress != 0) {
        return TransferNotProcessed;
    }

    if (dma_pool_allocate(DmaPool, sizeof(UsbPacket_t), &PacketBuffer) != OsSuccess) {
        ERROR("Failed to allocate a transfer buffer");
        return TransferInvalid;
    }

    Packet              = (UsbPacket_t*)PacketBuffer;
    Packet->Direction   = USBPACKET_DIRECTION_OUT;
    Packet->Type        = USBPACKET_TYPE_SET_ADDRESS;
    Packet->ValueLo     = (uint8_t)(Address & 0xFF);
    Packet->ValueHi     = 0;
    Packet->Index       = 0;
    Packet->Length      = 0;

    // Initialize transfer
    UsbTransferInitialize(&Transfer, UsbDevice, Endpoint, ControlTransfer, 0);

    // SetAddress does not have a data-stage
    UsbTransferSetup(&Transfer, dma_pool_handle(DmaPool), 
        dma_pool_offset(DmaPool, PacketBuffer), UUID_INVALID, 0, 0, InTransaction);
    
    // Execute the transaction and cleanup the buffer
    if (UsbTransferQueue(Driver, Device, &Transfer, &Result) != OsSuccess) {
        ERROR("Usb transfer returned error");
        Result->Status = TransferNotProcessed;
    }
    dma_pool_free(DmaPool, PacketBuffer);
    return Result->Status;
}

UsbTransferStatus_t
UsbGetDeviceDescriptor(
	_In_  UUId_t                    Driver,
	_In_  UUId_t                    Device,
	_In_  UsbHcDevice_t*            UsbDevice, 
    _In_  UsbHcEndpointDescriptor_t*Endpoint,
    _Out_ UsbDeviceDescriptor_t*    DeviceDescriptor)
{
    const size_t DESCRIPTOR_SIZE = 0x12; // Max Descriptor Length is 18 bytes

    void*        DescriptorVirtual    = NULL;
    void*        PacketBuffer         = NULL;
    UsbPacket_t* Packet;

    UsbTransferResult_t* Result;
    UsbTransfer_t        Transfer = { 0 };
    
    if (dma_pool_allocate(DmaPool, sizeof(UsbPacket_t), &PacketBuffer) != OsSuccess) {
        ERROR("Failed to allocate a transfer buffer");
        return TransferInvalid;
    }

    Packet                  = (UsbPacket_t*)PacketBuffer;
    Packet->Direction       = USBPACKET_DIRECTION_IN;
    Packet->Type            = USBPACKET_TYPE_GET_DESC;
    Packet->ValueHi         = USB_DESCRIPTOR_DEVICE;
    Packet->ValueLo         = 0;
    Packet->Index           = 0;
    Packet->Length          = DESCRIPTOR_SIZE;    

    if (dma_pool_allocate(DmaPool, DESCRIPTOR_SIZE, &DescriptorVirtual) != OsSuccess) {
        ERROR("Failed to allocate a transfer data buffer");
        dma_pool_free(DmaPool, PacketBuffer);
        return TransferInvalid;
    }

    // Setup, In (Data) and Out (ACK)
    UsbTransferInitialize(&Transfer, UsbDevice, 
        Endpoint, ControlTransfer, 0);
    UsbTransferSetup(&Transfer, dma_pool_handle(DmaPool), dma_pool_offset(DmaPool, PacketBuffer), 
        dma_pool_handle(DmaPool), dma_pool_offset(DmaPool, DescriptorVirtual), 
        DESCRIPTOR_SIZE, InTransaction);

    // Execute the transaction and cleanup the buffer
    if (UsbTransferQueue(Driver, Device, &Transfer, &Result) != OsSuccess) {
        ERROR("Usb transfer returned error");
        Result->Status = TransferNotProcessed;
    }

    // If the transfer finished correctly update the stored
    // device information to the queried
    if (Result->Status == TransferFinished && DeviceDescriptor != NULL) {
        memcpy(DeviceDescriptor, DescriptorVirtual, sizeof(UsbDeviceDescriptor_t));
    }

    // Cleanup allocations
    dma_pool_free(DmaPool, DescriptorVirtual);
    dma_pool_free(DmaPool, PacketBuffer);
    return Result->Status;
}

UsbTransferStatus_t
UsbGetInitialConfigDescriptor(
    _In_  UUId_t                     Driver,
    _In_  UUId_t                     Device,
    _In_  UsbHcDevice_t*             UsbDevice, 
    _In_  UsbHcEndpointDescriptor_t* Endpoint,
    _Out_ UsbConfigDescriptor_t*     ConfigDescriptor)
{
    void*        DescriptorVirtual = NULL;
    void*        PacketBuffer      = NULL;
    UsbPacket_t* Packet;

    UsbTransferResult_t* Result;
    UsbTransfer_t        Transfer = { 0 };
    
    if (dma_pool_allocate(DmaPool, sizeof(UsbPacket_t), &PacketBuffer) != OsSuccess) {
        ERROR("Failed to allocate a transfer buffer");
        return TransferInvalid;
    }

    Packet            = (UsbPacket_t*)PacketBuffer;
    Packet->Direction = USBPACKET_DIRECTION_IN;
    Packet->Type      = USBPACKET_TYPE_GET_DESC;
    Packet->ValueHi   = USB_DESCRIPTOR_CONFIG;
    Packet->ValueLo   = 0;
    Packet->Index     = 0;
    Packet->Length    = sizeof(UsbConfigDescriptor_t);

    if (dma_pool_allocate(DmaPool, sizeof(UsbConfigDescriptor_t),
            &DescriptorVirtual) != OsSuccess) {
        ERROR("Failed to allocate a transfer data buffer");
        dma_pool_free(DmaPool, PacketBuffer);
        return TransferInvalid;
    }

    // Setup, In (Data) and Out (ACK)
    UsbTransferInitialize(&Transfer, UsbDevice, 
        Endpoint, ControlTransfer, 0);
    UsbTransferSetup(&Transfer, dma_pool_handle(DmaPool), dma_pool_offset(DmaPool, PacketBuffer),
        dma_pool_handle(DmaPool), dma_pool_offset(DmaPool, DescriptorVirtual), 
        sizeof(UsbConfigDescriptor_t), InTransaction);

    // Execute the transaction and cleanup the buffer
    if (UsbTransferQueue(Driver, Device, &Transfer, &Result) != OsSuccess) {
        ERROR("Usb transfer returned error");
        Result->Status = TransferNotProcessed;
    }

    // Did it complete?
    if (Result->Status == TransferFinished) {
        memcpy(ConfigDescriptor, DescriptorVirtual, sizeof(UsbConfigDescriptor_t));
    }

    // Cleanup allocations
    dma_pool_free(DmaPool, DescriptorVirtual);
    dma_pool_free(DmaPool, PacketBuffer);
    return Result->Status;
}

UsbTransferStatus_t
UsbGetConfigDescriptor(
	_In_  UUId_t                     Driver,
	_In_  UUId_t                     Device,
	_In_  UsbHcDevice_t*             UsbDevice, 
    _In_  UsbHcEndpointDescriptor_t* Endpoint,
    _Out_ void*                      ConfigDescriptorBuffer,
    _Out_ size_t                     ConfigDescriptorBufferLength)
{
    void*        DescriptorVirtual  = NULL;
    void*        PacketBuffer       = NULL;
    UsbPacket_t* Packet;

    UsbTransferResult_t* Result;
    UsbTransfer_t        Transfer = { 0 };
    
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

    if (dma_pool_allocate(DmaPool, sizeof(UsbPacket_t), &PacketBuffer) != OsSuccess) {
        ERROR("Failed to allocate a transfer buffer");
        return TransferInvalid;
    }

    Packet              = (UsbPacket_t*)PacketBuffer;
    Packet->Direction   = USBPACKET_DIRECTION_IN;
    Packet->Type        = USBPACKET_TYPE_GET_DESC;
    Packet->ValueHi     = USB_DESCRIPTOR_CONFIG;
    Packet->ValueLo     = 0;
    Packet->Index       = 0;
    Packet->Length      = ConfigDescriptorBufferLength;

    if (dma_pool_allocate(DmaPool, ConfigDescriptorBufferLength, 
            &DescriptorVirtual) != OsSuccess) {
        ERROR("Failed to allocate a transfer data buffer");
        dma_pool_free(DmaPool, PacketBuffer);
        return TransferInvalid;
    }
    
    // Setup, In (Data) and Out (ACK)
    UsbTransferInitialize(&Transfer, UsbDevice, 
        Endpoint, ControlTransfer, 0);
    UsbTransferSetup(&Transfer, dma_pool_handle(DmaPool), dma_pool_offset(DmaPool, PacketBuffer),
        dma_pool_handle(DmaPool), dma_pool_offset(DmaPool, DescriptorVirtual), 
        ConfigDescriptorBufferLength, InTransaction);

    // Execute the transaction and cleanup the buffer
    if (UsbTransferQueue(Driver, Device, &Transfer, &Result) != OsSuccess) {
        ERROR("Usb transfer returned error");
        Result->Status = TransferNotProcessed;
    }

    // Did it complete?
    if (Result->Status == TransferFinished) {
        memcpy(ConfigDescriptorBuffer, DescriptorVirtual, ConfigDescriptorBufferLength);
    }

    // Cleanup allocations
    dma_pool_free(DmaPool, DescriptorVirtual);
    dma_pool_free(DmaPool, PacketBuffer);
    return Result->Status;
}

UsbTransferStatus_t
UsbSetConfiguration(
	_In_ UUId_t                     Driver,
	_In_ UUId_t                     Device,
	_In_ UsbHcDevice_t*             UsbDevice, 
    _In_ UsbHcEndpointDescriptor_t* Endpoint,
    _In_ int                        Configuration)
{
    void*        PacketBuffer = NULL;
    UsbPacket_t* Packet;

    UsbTransferResult_t* Result;
    UsbTransfer_t        Transfer = { 0 };

    if (dma_pool_allocate(DmaPool, sizeof(UsbPacket_t), &PacketBuffer) != OsSuccess) {
        ERROR("Failed to allocate a transfer buffer");
        return TransferInvalid;
    }

    Packet            = (UsbPacket_t*)PacketBuffer;
    Packet->Direction = USBPACKET_DIRECTION_OUT;
    Packet->Type      = USBPACKET_TYPE_SET_CONFIG;
    Packet->ValueHi   = 0;
    Packet->ValueLo   = (Configuration & 0xFF);
    Packet->Index     = 0;
    Packet->Length    = 0;        // No data for us

    // Initialize transfer
    UsbTransferInitialize(&Transfer, UsbDevice, Endpoint, ControlTransfer, 0);

    // SetConfiguration does not have a data-stage
    UsbTransferSetup(&Transfer, dma_pool_handle(DmaPool), 
        dma_pool_offset(DmaPool, PacketBuffer), UUID_INVALID, 0, 0, InTransaction);
    
    // Execute the transaction and cleanup the buffer
    if (UsbTransferQueue(Driver, Device, &Transfer, &Result) != OsSuccess) {
        ERROR("Usb transfer returned error");
        Result->Status = TransferNotProcessed;
    }
    dma_pool_free(DmaPool, PacketBuffer);
    return Result->Status;
}

UsbTransferStatus_t
UsbGetStringLanguages(
	_In_  UUId_t                     Driver,
	_In_  UUId_t                     Device,
	_In_  UsbHcDevice_t*             UsbDevice, 
    _In_  UsbHcEndpointDescriptor_t* Endpoint,
    _Out_ UsbStringDescriptor_t*     StringDescriptor)
{
    void*        DescriptorVirtual = NULL;
    void*        PacketBuffer      = NULL;
    UsbPacket_t* Packet;

    UsbTransferResult_t* Result;
    UsbTransfer_t        Transfer = { 0 };
    
    if (dma_pool_allocate(DmaPool, sizeof(UsbPacket_t), &PacketBuffer) != OsSuccess) {
        ERROR("Failed to allocate a transfer buffer");
        return TransferInvalid;
    }

    Packet            = (UsbPacket_t*)PacketBuffer;
    Packet->Direction = USBPACKET_DIRECTION_IN;
    Packet->Type      = USBPACKET_TYPE_GET_DESC;
    Packet->ValueHi   = USB_DESCRIPTOR_STRING;
    Packet->ValueLo   = 0;
    Packet->Index     = 0;
    Packet->Length    = sizeof(UsbStringDescriptor_t);

    if (dma_pool_allocate(DmaPool, sizeof(UsbStringDescriptor_t),
            &DescriptorVirtual) != OsSuccess) {
        ERROR("Failed to allocate a transfer data buffer");
        dma_pool_free(DmaPool, PacketBuffer);
        return TransferInvalid;
    }

    // Setup, In (Data) and Out (ACK)
    UsbTransferInitialize(&Transfer, UsbDevice, 
        Endpoint, ControlTransfer, 0);
    UsbTransferSetup(&Transfer, dma_pool_handle(DmaPool), dma_pool_offset(DmaPool, PacketBuffer),
        dma_pool_handle(DmaPool), dma_pool_offset(DmaPool, DescriptorVirtual),
        sizeof(UsbStringDescriptor_t), InTransaction);

    // Execute the transaction and cleanup the buffer
    if (UsbTransferQueue(Driver, Device, &Transfer, &Result) != OsSuccess) {
        ERROR("Usb transfer returned error");
        Result->Status = TransferNotProcessed;
    }

    // Update the device structure with the queried langauges
    if (Result->Status == TransferFinished) {
        memcpy(StringDescriptor, DescriptorVirtual, sizeof(UsbStringDescriptor_t));
    }

    // Cleanup allocations
    dma_pool_free(DmaPool, DescriptorVirtual);
    dma_pool_free(DmaPool, PacketBuffer);
    return Result->Status;
}

UsbTransferStatus_t
UsbGetStringDescriptor(
    _In_  UUId_t                     Driver,
    _In_  UUId_t                     Device,
    _In_  UsbHcDevice_t*             UsbDevice, 
    _In_  UsbHcEndpointDescriptor_t* Endpoint,
    _In_  size_t                     LanguageId, 
    _In_  size_t                     StringIndex, 
    _Out_ char*                      String)
{
    void*        DescriptorVirtual = NULL;
    void*        PacketBuffer      = NULL;
    const char*  StringBuffer;
    UsbPacket_t* Packet;

    UsbTransferResult_t* Result;
    UsbTransfer_t        Transfer = { 0 };
    
    if (dma_pool_allocate(DmaPool, sizeof(UsbPacket_t), &PacketBuffer) != OsSuccess) {
        ERROR("Failed to allocate a transfer buffer");
        return TransferInvalid;
    }

    Packet              = (UsbPacket_t*)PacketBuffer;
    Packet->Direction   = USBPACKET_DIRECTION_IN;
    Packet->Type        = USBPACKET_TYPE_GET_DESC;
    Packet->ValueHi     = USB_DESCRIPTOR_STRING;
    Packet->ValueLo     = (uint8_t)StringIndex;
    Packet->Index       = (uint16_t)LanguageId;
    Packet->Length      = 64;

    if (dma_pool_allocate(DmaPool, 64, &DescriptorVirtual) != OsSuccess) {
        ERROR("Failed to allocate a transfer data buffer");
        dma_pool_free(DmaPool, PacketBuffer);
        return TransferInvalid;
    }

    // Setup, In (Data) and Out (ACK)
    UsbTransferInitialize(&Transfer, UsbDevice, 
        Endpoint, ControlTransfer, 0);
    UsbTransferSetup(&Transfer, dma_pool_handle(DmaPool), dma_pool_offset(DmaPool, PacketBuffer),
        dma_pool_handle(DmaPool), dma_pool_offset(DmaPool, DescriptorVirtual),
        64, InTransaction);

    // Execute the transaction and cleanup the buffer
    if (UsbTransferQueue(Driver, Device, &Transfer, &Result) != OsSuccess) {
        ERROR("Usb transfer returned error");
        Result->Status = TransferNotProcessed;
    }

    // Update the out variable with the string
    // TODO: convert to utf8
    if (Result->Status == TransferFinished) {
        StringBuffer = (const char*)DescriptorVirtual;
        //size_t StringLength = (*((uint8_t*)TempBuffer + 1) - 2);
        _CRT_UNUSED(StringBuffer);
    }

    // Cleanup allocations
    dma_pool_free(DmaPool, DescriptorVirtual);
    dma_pool_free(DmaPool, PacketBuffer);
    return Result->Status;
}

UsbTransferStatus_t
UsbClearFeature(
    _In_ UUId_t                     Driver,
    _In_ UUId_t                     Device,
    _In_ UsbHcDevice_t*             UsbDevice, 
    _In_ UsbHcEndpointDescriptor_t* Endpoint,
    _In_ uint8_t                    Target, 
    _In_ uint16_t                   Index, 
    _In_ uint16_t                   Feature)
{
    void*        PacketBuffer = NULL;
    UsbPacket_t* Packet;

    UsbTransferResult_t* Result;
    UsbTransfer_t        Transfer = { 0 };

    if (dma_pool_allocate(DmaPool, sizeof(UsbPacket_t), &PacketBuffer) != OsSuccess) {
        ERROR("Failed to allocate a transfer buffer");
        return TransferInvalid;
    }

    Packet            = (UsbPacket_t*)PacketBuffer;
    Packet->Direction = Target;
    Packet->Type      = USBPACKET_TYPE_CLR_FEATURE;
    Packet->ValueHi   = ((Feature >> 8) & 0xFF);
    Packet->ValueLo   = (Feature & 0xFF);
    Packet->Index     = Index;
    Packet->Length    = 0;        // No data for us

    // Initialize transfer
    UsbTransferInitialize(&Transfer, UsbDevice, Endpoint, ControlTransfer, 0);

    // ClearFeature does not have a data-stage
    UsbTransferSetup(&Transfer, dma_pool_handle(DmaPool), dma_pool_offset(DmaPool, PacketBuffer),
        UUID_INVALID, 0, 0, InTransaction);
    
    // Execute the transaction and cleanup the buffer
    if (UsbTransferQueue(Driver, Device, &Transfer, &Result) != OsSuccess) {
        ERROR("Usb transfer returned error");
        Result->Status = TransferNotProcessed;
    }
    dma_pool_free(DmaPool, PacketBuffer);
    return Result->Status;
}

UsbTransferStatus_t
UsbSetFeature(
	_In_ UUId_t                     Driver,
	_In_ UUId_t                     Device,
	_In_ UsbHcDevice_t*             UsbDevice, 
    _In_ UsbHcEndpointDescriptor_t* Endpoint,
	_In_ uint8_t                    Target, 
	_In_ uint16_t                   Index, 
    _In_ uint16_t                   Feature)
{
    void*        PacketBuffer = NULL;
    UsbPacket_t* Packet;
    
    UsbTransferResult_t* Result;
    UsbTransfer_t        Transfer = { 0 };

    if (dma_pool_allocate(DmaPool, sizeof(UsbPacket_t), &PacketBuffer) != OsSuccess) {
        ERROR("Failed to allocate a transfer buffer");
        return TransferInvalid;
    }

    Packet              = (UsbPacket_t*)PacketBuffer;
    Packet->Direction   = Target;
    Packet->Type        = USBPACKET_TYPE_SET_FEATURE;
    Packet->ValueHi     = ((Feature >> 8) & 0xFF);
    Packet->ValueLo     = (Feature & 0xFF);
    Packet->Index       = Index;
    Packet->Length      = 0;        // No data for us

    // Initialize transfer
    UsbTransferInitialize(&Transfer, UsbDevice, Endpoint, ControlTransfer, 0);

    // SetFeature does not have a data-stage
    UsbTransferSetup(&Transfer, dma_pool_handle(DmaPool), dma_pool_offset(DmaPool, PacketBuffer),
        UUID_INVALID, 0, 0, InTransaction);
    
    // Execute the transaction and cleanup the buffer
    if (UsbTransferQueue(Driver, Device, &Transfer, &Result) != OsSuccess) {
        ERROR("Usb transfer returned error");
        Result->Status = TransferNotProcessed;
    }
    dma_pool_free(DmaPool, PacketBuffer);
    return Result->Status;
}

UsbTransferStatus_t
UsbExecutePacket(
    _In_  UUId_t                     Driver,
    _In_  UUId_t                     Device,
    _In_  UsbHcDevice_t*             UsbDevice, 
    _In_  UsbHcEndpointDescriptor_t* Endpoint,
    _In_  uint8_t                    Direction,
    _In_  uint8_t                    Type,
    _In_  uint8_t                    ValueHigh,
    _In_  uint8_t                    ValueLow,
    _In_  uint16_t                   Index,
    _In_  uint16_t                   Length,
    _Out_ void*                      Buffer)
{
    UsbTransactionType_t DataStageType = OutTransaction;
    void*        DescriptorVirtual     = NULL;
    void*        PacketBuffer          = NULL;
    UsbPacket_t* Packet;

    UsbTransferResult_t* Result;
    UsbTransfer_t        Transfer = { 0 };
    
    if (dma_pool_allocate(DmaPool, sizeof(UsbPacket_t), &PacketBuffer) != OsSuccess) {
        ERROR("Failed to allocate a transfer buffer");
        return TransferInvalid;
    }

    Packet              = (UsbPacket_t*)PacketBuffer;
    Packet->Direction   = Direction;
    Packet->Type        = Type;
    Packet->ValueHi     = ValueHigh;
    Packet->ValueLo     = ValueLow;
    Packet->Index       = Index;
    Packet->Length      = Length;

    if (Length != 0 && 
            dma_pool_allocate(DmaPool, Length, &DescriptorVirtual) != OsSuccess) {
        ERROR("Failed to allocate a transfer data buffer");
        dma_pool_free(DmaPool, PacketBuffer);
        return TransferInvalid;
    }

    // Get direction
    if (Direction & USBPACKET_DIRECTION_IN) {
        DataStageType = InTransaction;
    }

    // Copy data if out
    if (DataStageType == OutTransaction && Length != 0 && Buffer != NULL) {
        memcpy(DescriptorVirtual, Buffer, Length);
    }

    // Initialize setup transfer
    UsbTransferInitialize(&Transfer, UsbDevice, Endpoint, ControlTransfer, 0);
    UsbTransferSetup(&Transfer, dma_pool_handle(DmaPool), dma_pool_offset(DmaPool, PacketBuffer),
        dma_pool_handle(DmaPool), dma_pool_offset(DmaPool, DescriptorVirtual), Length, DataStageType);

    // Execute the transaction and cleanup the buffer
    if (UsbTransferQueue(Driver, Device, &Transfer, &Result) != OsSuccess) {
        ERROR("Usb transfer returned error");
        Result->Status = TransferNotProcessed;
    }

    // Update the device structure with the queried langauges
    if (Result->Status == TransferFinished && Length != 0 
        && Buffer != NULL && DataStageType == InTransaction) {
        memcpy(Buffer, DescriptorVirtual, Length);
    }

    // Cleanup allocations
    if (Length != 0) {
        dma_pool_free(DmaPool, DescriptorVirtual);
    }
    dma_pool_free(DmaPool, PacketBuffer);
    return Result->Status;
}
