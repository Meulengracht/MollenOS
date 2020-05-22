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

#include <usb/usb.h>
#include <ddk/bufferpool.h>
#include <ddk/usbdevice.h>
#include <ddk/utils.h>
#include <internal/_ipc.h>
#include <os/mollenos.h>
#include <os/process.h>
#include <os/dmabuf.h>
#include <threads.h>
#include <stdlib.h>
#include <string.h>

static _Atomic(UUId_t)       TransferIdGenerator      = ATOMIC_VAR_INIT(1);
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

void
UsbTransferInitialize(
    _In_ UsbTransfer_t*             transfer,
    _In_ usb_device_context_t*      device,
    _In_ usb_endpoint_descriptor_t* endpoint,
    _In_ uint8_t                    type,
    _In_ uint8_t                    flags)
{
    // Support NULL endpoint to indicate control
    uint8_t  endpointAddress   = endpoint ? USB_ENDPOINT_ADDRESS(endpoint->Address) : 0;
    uint16_t endpointMps       = endpoint ? USB_ENDPOINT_MPS(endpoint) : device->device_mps;
    uint8_t  endpointBandwidth = endpoint ? USB_ENDPOINT_BANDWIDTH(endpoint) : 0;
    uint8_t  endpointInterval  = endpoint ? USB_ENDPOINT_INTERVAL(endpoint) : 0;
    
    TRACE("[usb] [transfer] initialize %u", endpointAddress);
    
    memset(transfer, 0, sizeof(UsbTransfer_t));
    transfer->Type                    = type;
    transfer->Speed                   = device->speed;
    transfer->Address.HubAddress      = device->hub_address;
    transfer->Address.PortAddress     = device->port_address;
    transfer->Address.DeviceAddress   = device->device_address;
    transfer->Address.EndpointAddress = endpointAddress;
    transfer->MaxPacketSize           = endpointMps;
    transfer->Flags                   = flags;
    transfer->PeriodicBandwith        = endpointBandwidth;
    transfer->PeriodicInterval        = endpointInterval;
}

void
UsbTransferSetup(
    _In_ UsbTransfer_t* transfer,
    _In_ UUId_t         setupBufferHandle,
    _In_ size_t         setupBufferOffset,
    _In_ UUId_t         dataBufferHandle,
    _In_ size_t         dataBufferOffset,
    _In_ size_t         dataLength,
    _In_ uint8_t        type)
{
    uint8_t ackType  = USB_TRANSACTION_IN;
    int     ackIndex = 1;

    TRACE("[usb] [transfer] setup");

    // Initialize the setup stage
    transfer->Transactions[0].Type         = USB_TRANSACTION_SETUP;
    transfer->Transactions[0].BufferHandle = setupBufferHandle;
    transfer->Transactions[0].BufferOffset = setupBufferOffset;
    transfer->Transactions[0].Length       = sizeof(usb_packet_t);

    // Is there a data-stage?
    if (dataBufferHandle != UUID_INVALID) {
        ackIndex++;
        transfer->Transactions[1].BufferHandle = dataBufferHandle;
        transfer->Transactions[1].BufferOffset = dataBufferOffset;
        transfer->Transactions[1].Length       = dataLength;
        transfer->Transactions[1].Type         = type;
        if (type == USB_TRANSACTION_IN) {
            ackType = USB_TRANSACTION_OUT;
        }
    }

    // Ack-stage
    transfer->Transactions[ackIndex].Flags        = USB_TRANSACTION_ZLP | USB_TRANSACTION_HANDSHAKE;
    transfer->Transactions[ackIndex].BufferHandle = UUID_INVALID;
    transfer->Transactions[ackIndex].Type         = ackType;
    transfer->TransactionCount                    = ackIndex + 1;
}

void
UsbTransferPeriodic(
    _In_ UsbTransfer_t* Transfer,
    _In_ UUId_t         BufferHandle,
    _In_ size_t         BufferOffset,
    _In_ size_t         BufferLength,
    _In_ size_t         DataLength,
    _In_ uint8_t        DataDirection,
    _In_ const void*    NotifificationData)
{
    // Initialize the data stage
    Transfer->Transactions[0].Type         = DataDirection;
    Transfer->Transactions[0].BufferHandle = BufferHandle;
    Transfer->Transactions[0].BufferOffset = BufferOffset;
    Transfer->Transactions[0].Length       = DataLength;

    // Initialize the transfer for interrupt
    Transfer->PeriodicData       = NotifificationData;
    Transfer->PeriodicBufferSize = BufferLength;
    Transfer->TransactionCount   = 1;
}

OsStatus_t
UsbTransferIn(
	_In_ UsbTransfer_t* Transfer,
    _In_ UUId_t         BufferHandle,
    _In_ size_t         BufferOffset,
	_In_ size_t         Length,
    _In_ int            Handshake)
{
    usb_transaction_t* Transaction;

    // Sanitize count
    if (Transfer->TransactionCount >= 3) {
        return OsError;
    }

    Transaction               = &Transfer->Transactions[Transfer->TransactionCount++];
    Transaction->Type         = USB_TRANSACTION_IN;
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
    usb_transaction_t* Transaction;

    // Sanitize count
    if (Transfer->TransactionCount >= 3) {
        return OsError;
    }

    Transaction               = &Transfer->Transactions[Transfer->TransactionCount++];
    Transaction->Type         = USB_TRANSACTION_OUT;
    Transaction->BufferHandle = BufferHandle;
    Transaction->BufferOffset = BufferOffset;
    Transaction->Length       = Length;
    Transaction->Flags        = Handshake ? USB_TRANSACTION_ZLP : 0; 
    
    if (Length == 0) { // Zero-length?
        Transaction->Flags |= USB_TRANSACTION_ZLP;
    }
    return OsSuccess;
}

UsbTransferStatus_t
UsbTransferQueue(
    _In_  usb_device_context_t* deviceContext,
	_In_  UsbTransfer_t*        transfer,
	_Out_ size_t*               bytesTransferred)
{
    struct vali_link_message msg        = VALI_MSG_INIT_HANDLE(deviceContext->driver_id);
    UUId_t                   transferId = atomic_fetch_add(&TransferIdGenerator, 1);
    UsbTransferStatus_t      status;
    
    ctt_usbhost_queue(GetGrachtClient(), &msg.base, ProcessGetCurrentId(),
        deviceContext->device_id, transferId, transfer);
    ctt_usbhost_queue_result(GetGrachtClient(), &msg.base, &status, bytesTransferred);
    return status;
}

UsbTransferStatus_t
UsbTransferQueuePeriodic(
    _In_  usb_device_context_t* deviceContext,
	_In_  UsbTransfer_t*        transfer,
	_Out_ UUId_t*               transferIdOut)
{
    struct vali_link_message msg        = VALI_MSG_INIT_HANDLE(deviceContext->driver_id);
    UUId_t                   transferId = atomic_fetch_add(&TransferIdGenerator, 1);
    UsbTransferStatus_t      status;
    
    ctt_usbhost_queue_periodic(GetGrachtClient(), &msg.base, ProcessGetCurrentId(),
        deviceContext->device_id, transferId, transfer);
    ctt_usbhost_queue_periodic_result(GetGrachtClient(), &msg.base, &status);
    
    *transferIdOut = transferId;
    return status;
}

OsStatus_t
UsbTransferDequeuePeriodic(
    _In_ usb_device_context_t* deviceContext,
	_In_ UUId_t                transferId)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(deviceContext->driver_id);
    OsStatus_t               status;
    
    ctt_usbhost_dequeue(GetGrachtClient(), &msg.base, ProcessGetCurrentId(),
        deviceContext->device_id, transferId);
    ctt_usbhost_dequeue_result(GetGrachtClient(), &msg.base, &status);
    return status;
}

OsStatus_t
UsbHubResetPort(
	_In_ UUId_t                 InterfaceId,
	_In_ UUId_t                 DeviceId,
	_In_ uint8_t                PortAddress,
	_In_ UsbHcPortDescriptor_t* Descriptor)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(InterfaceId);
    OsStatus_t               status;
    
    ctt_usbhost_reset_port(GetGrachtClient(), &msg.base, DeviceId, PortAddress);
    ctt_usbhost_reset_port_result(GetGrachtClient(), &msg.base, &status, Descriptor);
    return status;
}

OsStatus_t
UsbHubQueryPort(
	_In_ UUId_t                 InterfaceId,
	_In_ UUId_t                 DeviceId,
	_In_ uint8_t                PortAddress,
	_In_ UsbHcPortDescriptor_t* Descriptor)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(InterfaceId);
    OsStatus_t               status;
    
    ctt_usbhost_query_port(GetGrachtClient(), &msg.base, DeviceId, PortAddress);
    ctt_usbhost_query_port_result(GetGrachtClient(), &msg.base, &status, Descriptor);
    return status;
}

OsStatus_t
UsbEndpointReset(
	_In_ usb_device_context_t* deviceContext, 
    _In_ uint8_t               endpointAddress)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(deviceContext->driver_id);
    OsStatus_t               status;
    
    ctt_usbhost_reset_endpoint(GetGrachtClient(), &msg.base, deviceContext->device_id,
        deviceContext->hub_address, deviceContext->port_address,
        deviceContext->device_address, endpointAddress);
    ctt_usbhost_reset_endpoint_result(GetGrachtClient(), &msg.base, &status);
    return status;
}

UsbTransferStatus_t
UsbExecutePacket(
	_In_ usb_device_context_t* deviceContext,
    _In_ uint8_t               direction,
    _In_ uint8_t               type,
    _In_ uint8_t               valueLow,
    _In_ uint8_t               valueHigh,
    _In_ uint16_t              index,
    _In_ uint16_t              length,
    _In_ void*                 buffer)
{
    UsbTransferStatus_t status;
    size_t              bytesTransferred;
    uint8_t             dataDirection;
    void*               dmaStorage = NULL;
    void*               dmaPacketStorage;
    usb_packet_t*       packet;
    UsbTransfer_t       transfer;
    
    if (dma_pool_allocate(DmaPool, sizeof(usb_packet_t), &dmaPacketStorage) != OsSuccess) {
        ERROR("Failed to allocate a transfer buffer");
        return TransferInvalid;
    }

    if (length != 0) {
        OsStatus_t status = dma_pool_allocate(DmaPool, length, &dmaStorage);
        if (status  != OsSuccess) {
            ERROR("Failed to allocate a transfer data buffer");
            dma_pool_free(DmaPool, dmaPacketStorage);
            return TransferInvalid;
        }
    }
    
    packet              = (usb_packet_t*)dmaPacketStorage;
    packet->Direction   = direction;
    packet->Type        = type;
    packet->ValueHi     = valueHigh;
    packet->ValueLo     = valueLow;
    packet->Index       = index;
    packet->Length      = length;

    if (direction & USBPACKET_DIRECTION_IN) {
        dataDirection = USB_TRANSACTION_IN;
    }
    else {
        dataDirection = USB_TRANSACTION_OUT;
        if (length != 0 && buffer != NULL) {
            memcpy(dmaStorage, buffer, length);
        }
    }

    // Initialize setup transfer
    UsbTransferInitialize(&transfer, deviceContext, USB_TRANSFER_ENDPOINT_CONTROL,
        USB_TRANSFER_CONTROL, 0);
    UsbTransferSetup(&transfer, dma_pool_handle(DmaPool), dma_pool_offset(DmaPool, dmaPacketStorage),
        dma_pool_handle(DmaPool), dma_pool_offset(DmaPool, dmaStorage), length, dataDirection);

    // Execute the transaction and cleanup the buffer
    status = UsbTransferQueue(deviceContext, &transfer, &bytesTransferred);
    if (status != TransferFinished) {
        ERROR("Usb transfer returned error %u", status);
    }

    if (status == TransferFinished && length != 0 && 
        buffer != NULL && dataDirection == USB_TRANSACTION_IN) {
        memcpy(buffer, dmaStorage, length);
    }

    if (length != 0) {
        dma_pool_free(DmaPool, dmaStorage);
    }
    dma_pool_free(DmaPool, dmaPacketStorage);
    return status;
}

UsbTransferStatus_t
UsbSetAddress(
	_In_ usb_device_context_t* deviceContext,
    _In_ int                   address)
{
    UsbTransferStatus_t status;

    // Debug
    TRACE("UsbSetAddress()");

    // Sanitize current address
    if (deviceContext->device_address != 0) {
        return TransferNotProcessed;
    }

    status = UsbExecutePacket(deviceContext, USBPACKET_DIRECTION_OUT,
        USBPACKET_TYPE_SET_ADDRESS, (uint8_t)(address & 0xFF), 0, 0, 0, NULL);
    return status;
}

UsbTransferStatus_t
UsbGetDeviceDescriptor(
	_In_ usb_device_context_t*    deviceContext,
    _In_ usb_device_descriptor_t* deviceDescriptor)
{
    const size_t DESCRIPTOR_SIZE = 0x12; // Max Descriptor Length is 18 bytes
    
    UsbTransferStatus_t status;
    
    status = UsbExecutePacket(deviceContext, USBPACKET_DIRECTION_IN,
        USBPACKET_TYPE_GET_DESC, 0, USB_DESCRIPTOR_DEVICE, 0, DESCRIPTOR_SIZE,
        deviceDescriptor);
    return status;
}

static void
ParseConfigurationDescriptor(
    _In_ usb_config_descriptor_t*    descriptor,
    _In_ usb_device_configuration_t* configuration)
{
    usb_device_interface_setting_t* currentSetting;
    usb_device_interface_t*         currentInterface;
    uint8_t*                        pointer   = (uint8_t*)descriptor;
    uint16_t                        bytesLeft = descriptor->TotalLength;
    int                             i;
    
    // copy base data to the configuration
    memcpy(&configuration->base, descriptor, sizeof(usb_config_descriptor_t));
    
    // allocate space for interfaces
    configuration->interfaces = (usb_device_interface_t*)malloc(
        descriptor->NumInterfaces * sizeof(usb_device_interface_t));
    if (!configuration->interfaces) {
        return;
    }
    
    for (i = 0; i < (int)descriptor->NumInterfaces; i++) {
        configuration->interfaces[i].settings_count = 1;
        configuration->interfaces[i].settings = (usb_device_interface_setting_t*)
            malloc(sizeof(usb_device_interface_setting_t));
        if (!configuration->interfaces[i].settings) {
            return;
        }
    }
    
    while (bytesLeft) {
        uint8_t length = *pointer;
        uint8_t type   = *(pointer + 1);
        
        if (length == sizeof(usb_interface_descriptor_t) && type == USB_DESCRIPTOR_INTERFACE) {
            usb_interface_descriptor_t* interface = (usb_interface_descriptor_t*)pointer;
            currentInterface = &configuration->interfaces[interface->NumInterface];
            
            // if there happen to be more settings than allocated for - resize buffer
            if ((interface->AlternativeSetting + 1) > currentInterface->settings_count) {
                int newCount = interface->AlternativeSetting + 1;
                configuration->interfaces[i].settings_count = newCount;
                configuration->interfaces[i].settings = realloc(
                    configuration->interfaces[i].settings, newCount * 
                        sizeof(usb_device_interface_setting_t));
            }
            
            currentSetting = &currentInterface->settings[interface->AlternativeSetting];
            memcpy(&currentSetting->base, interface, sizeof(usb_interface_descriptor_t));
            if (interface->NumEndpoints) {
                currentSetting->endpoints = malloc(interface->NumEndpoints *
                    sizeof(usb_endpoint_descriptor_t));
                if (!currentSetting->endpoints) {
                    return;
                }
            }
            else {
                currentSetting->endpoints = NULL;
            }
            
            // reset endpoint index
            i = 0;
        }
        else if ((length == 7 || length == 9) && type == USB_DESCRIPTOR_ENDPOINT) {
            usb_endpoint_descriptor_t* endpoint = (usb_endpoint_descriptor_t*)pointer;
            memcpy(&currentSetting->endpoints[i++], endpoint, length);
        }
        
        pointer   += length;
        bytesLeft -= length;
    }
}

static UsbTransferStatus_t
UsbGetInitialConfigDescriptor(
	_In_ usb_device_context_t*       deviceContext,
    _In_ usb_device_configuration_t* configuration)
{
    UsbTransferStatus_t status;
    
    status = UsbExecutePacket(deviceContext, USBPACKET_DIRECTION_IN,
        USBPACKET_TYPE_GET_DESC, 0, USB_DESCRIPTOR_CONFIG, 0, 
        sizeof(usb_config_descriptor_t), &configuration->base);
    return status;
}

UsbTransferStatus_t
UsbGetConfigDescriptor(
	_In_ usb_device_context_t*       deviceContext,
	_In_ int                         configurationIndex,
    _In_ usb_device_configuration_t* configuration)
{
    void*               descriptorStorage;
    UsbTransferStatus_t status;
    
    // Are we requesting the initial descriptor?
    if (deviceContext->configuration_length <= sizeof(usb_config_descriptor_t)) {
        status = UsbGetInitialConfigDescriptor(deviceContext, configuration);
        if (status != TransferFinished) {
            return status;
        }
        deviceContext->configuration_length = configuration->base.TotalLength;
    }

    descriptorStorage = malloc(deviceContext->configuration_length);
    if (!descriptorStorage) {
        return TransferBufferError;
    }
    
    status = UsbExecutePacket(deviceContext, USBPACKET_DIRECTION_IN,
        USBPACKET_TYPE_GET_DESC, 0, USB_DESCRIPTOR_CONFIG,
        (uint16_t)(configurationIndex & 0xFFFF), deviceContext->configuration_length,
        descriptorStorage);
    if (status == TransferFinished) {
        ParseConfigurationDescriptor(descriptorStorage, configuration);
    }

    free(descriptorStorage);
    return status;
}

UsbTransferStatus_t
UsbGetActiveConfigDescriptor(
	_In_ usb_device_context_t*       deviceContext,
    _In_ usb_device_configuration_t* configuration)
{
    return UsbGetConfigDescriptor(deviceContext, 0, configuration);
}

void
UsbFreeConfigDescriptor(
    _In_ usb_device_configuration_t* configuration)
{
    int i, j;
    
    if (!configuration) {
        return;
    }
    
    for (i = 0; i < configuration->base.NumInterfaces; i++) {
        usb_device_interface_t* interface = &configuration->interfaces[i];
        for (j = 0; j < interface->settings_count; j++) {
            if (interface->settings[j].endpoints) {
                free(interface->settings[j].endpoints);
            }
        }
        free(interface->settings);
    }
    
    free(configuration->interfaces);
    configuration->interfaces = NULL;
}

UsbTransferStatus_t
UsbSetConfiguration(
	_In_ usb_device_context_t* deviceContext,
    _In_ int                   configurationIndex)
{
    UsbTransferStatus_t status;
    
    status = UsbExecutePacket(deviceContext, USBPACKET_DIRECTION_OUT,
        USBPACKET_TYPE_SET_CONFIG, (uint8_t)(configurationIndex & 0xFF),
        0, 0, 0, NULL);
    return status;
}

UsbTransferStatus_t
UsbGetStringLanguages(
	_In_ usb_device_context_t*    deviceContext,
    _In_ usb_string_descriptor_t* descriptor)
{
    UsbTransferStatus_t status;
    
    status = UsbExecutePacket(deviceContext, USBPACKET_DIRECTION_IN,
        USBPACKET_TYPE_GET_DESC, 0, USB_DESCRIPTOR_STRING, 0,
        sizeof(usb_string_descriptor_t), descriptor);
    return status;
}

UsbTransferStatus_t
UsbGetStringDescriptor(
	_In_ usb_device_context_t* deviceContext,
    _In_  size_t               LanguageId, 
    _In_  size_t               StringIndex, 
    _Out_ char*                String)
{
    void*               descriptorStorage;
    UsbTransferStatus_t status;
    
    descriptorStorage = malloc(64);
    status = UsbExecutePacket(deviceContext, USBPACKET_DIRECTION_IN,
        USBPACKET_TYPE_GET_DESC, (uint8_t)StringIndex, USB_DESCRIPTOR_STRING,
        (uint16_t)LanguageId, 64, descriptorStorage);

    // Update the out variable with the string
    // TODO: convert to utf8
    if (status == TransferFinished) {
        const char* stringBuffer = (const char*)descriptorStorage;
        //size_t StringLength = (*((uint8_t*)descriptorStorage + 1) - 2);
        _CRT_UNUSED(stringBuffer);
    }

    free(descriptorStorage);
    return status;
}

UsbTransferStatus_t
UsbClearFeature(
	_In_ usb_device_context_t* deviceContext,
    _In_ uint8_t               Target, 
    _In_ uint16_t              Index, 
    _In_ uint16_t              Feature)
{
    UsbTransferStatus_t status;
    
    status = UsbExecutePacket(deviceContext, Target,
        USBPACKET_TYPE_CLR_FEATURE, (uint8_t)Feature & 0xFF,
        (Feature >> 8) & 0xFF, Index, 0, NULL);
    return status;
}

UsbTransferStatus_t
UsbSetFeature(
	_In_ usb_device_context_t* deviceContext,
	_In_ uint8_t               Target, 
	_In_ uint16_t              Index, 
    _In_ uint16_t              Feature)
{
    UsbTransferStatus_t status;

    status = UsbExecutePacket(deviceContext, Target,
        USBPACKET_TYPE_SET_FEATURE, (uint8_t)Feature & 0xFF,
        (Feature >> 8) & 0xFF, Index, 0, NULL);
    return status;
}
