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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Usb function library
 * - Contains helper functions for interacting with usb devices. 
 */

//#define __TRACE

#include <usb/usb.h>
#include <ddk/convert.h>
#include <ddk/utils.h>
#include <gracht/link/vali.h>
#include <internal/_utils.h>
#include "os/services/process.h"
#include <os/shm.h>
#include <os/handle.h>
#include <stdlib.h>

#include <ctt_usbhost_service_client.h>
#include <ctt_usbhub_service_client.h>

static _Atomic(uuid_t)  TransferIdGenerator      = 1;
static const size_t     LIBUSB_SHAREDBUFFER_SIZE = 0x2000;
static struct dma_pool* g_dmaPool                = NULL;
static OSHandle_t       g_shmHandle;

oserr_t
UsbInitialize(void)
{
    oserr_t oserr;

    oserr = SHMCreate(
            &(SHM_t) {
                .Key = NULL,
                .Flags = SHM_DEVICE,
                .Conformity = OSMEMORYCONFORMITY_LOW,
                .Size = LIBUSB_SHAREDBUFFER_SIZE,
                .Access = SHM_ACCESS_WRITE | SHM_ACCESS_READ
            },
            &g_shmHandle
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = dma_pool_create(&g_shmHandle, &g_dmaPool);
    if (oserr != OS_EOK) {
        OSHandleDestroy(&g_shmHandle);
    }
    return oserr;
}

void
UsbCleanup(void)
{
    if (!g_dmaPool) {
        return;
    }

    dma_pool_destroy(g_dmaPool);
    OSHandleDestroy(&g_shmHandle);
    g_dmaPool = NULL;
}

struct dma_pool*
UsbRetrievePool(void)
{
    return g_dmaPool;
}

void
UsbTransferInitialize(
        _In_ USBTransfer_t*             transfer,
        _In_ usb_device_context_t*      device,
        _In_ usb_endpoint_descriptor_t* endpoint,
        _In_ enum USBTransferType       type,
        _In_ enum USBTransferDirection  direction,
        _In_ unsigned int               flags,
        _In_ uuid_t                     dataBufferHandle,
        _In_ size_t                     dataBufferOffset,
        _In_ size_t                     dataLength)
{
    // Support NULL endpoint to indicate control
    uint8_t  endpointAddress   = endpoint ? USB_ENDPOINT_ADDRESS(endpoint->Address) : 0;
    uint16_t endpointMps       = endpoint ? USB_ENDPOINT_MPS(endpoint) : device->device_mps;
    uint8_t  endpointBandwidth = endpoint ? USB_ENDPOINT_BANDWIDTH(endpoint) : 0;
    uint8_t  endpointInterval  = endpoint ? USB_ENDPOINT_INTERVAL(endpoint) : 0;
    
    TRACE("[usb] [transfer] initialize %u", endpointAddress);
    
    memset(transfer, 0, sizeof(USBTransfer_t));
    transfer->Type                    = type;
    transfer->Direction               = direction;
    transfer->Speed                   = device->speed;
    transfer->Address.HubAddress      = device->hub_address;
    transfer->Address.PortAddress     = device->port_address;
    transfer->Address.DeviceAddress   = device->device_address;
    transfer->Address.EndpointAddress = endpointAddress;
    transfer->MaxPacketSize           = endpointMps;
    transfer->Flags                   = flags;
    transfer->BufferHandle            = dataBufferHandle;
    transfer->BufferOffset            = dataBufferOffset;
    transfer->Length                  = dataLength;
    transfer->PeriodicBandwith        = endpointBandwidth;
    transfer->PeriodicInterval        = endpointInterval;
}

oserr_t
UsbTransferQueue(
        _In_  usb_device_context_t* deviceContext,
        _In_  USBTransfer_t*        transfer,
        _Out_ enum USBTransferCode* transferResultOut,
        _Out_ size_t*               bytesTransferredOut)
{
    struct vali_link_message     msg            = VALI_MSG_INIT_HANDLE(deviceContext->controller_driver_id);
    uuid_t                       transferId     = atomic_fetch_add(&TransferIdGenerator, 1);
    enum ctt_usb_transfer_status transferResult = CTT_USB_TRANSFER_STATUS_NORESPONSE;
    oserr_t                      oserr;

    ctt_usbhost_queue(GetGrachtClient(), &msg.base, OSProcessCurrentID(),
        deviceContext->controller_device_id, transferId, (uint8_t*)transfer, sizeof(USBTransfer_t));
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    ctt_usbhost_queue_result(GetGrachtClient(), &msg.base, &oserr, &transferResult, bytesTransferredOut);

    *transferResultOut = to_usbcode(transferResult);
    return oserr;
}

oserr_t
UsbTransferQueuePeriodic(
        _In_  usb_device_context_t* deviceContext,
        _In_  USBTransfer_t*        transfer,
        _Out_ uuid_t*               transferIdOut)
{
    struct vali_link_message msg        = VALI_MSG_INIT_HANDLE(deviceContext->controller_driver_id);
    uuid_t                   transferId = atomic_fetch_add(&TransferIdGenerator, 1);
    oserr_t                  oserr;

    ctt_usbhost_queue_periodic(GetGrachtClient(), &msg.base, OSProcessCurrentID(),
        deviceContext->controller_device_id, transferId, (uint8_t*)transfer, sizeof(USBTransfer_t));
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    ctt_usbhost_queue_periodic_result(GetGrachtClient(), &msg.base, &oserr);
    
    *transferIdOut = transferId;
    return oserr;
}

oserr_t
UsbTransferResetPeriodic(
        _In_ usb_device_context_t* deviceContext,
        _In_ uuid_t                transferId)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(deviceContext->controller_driver_id);
    oserr_t               status;

    ctt_usbhost_reset_periodic(GetGrachtClient(), &msg.base, OSProcessCurrentID(),
                        deviceContext->controller_device_id, transferId);
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    ctt_usbhost_reset_periodic_result(GetGrachtClient(), &msg.base, &status);
    return status;
}

oserr_t
UsbTransferDequeuePeriodic(
        _In_ usb_device_context_t* deviceContext,
        _In_ uuid_t                transferId)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(deviceContext->controller_driver_id);
    oserr_t                  status;
    
    ctt_usbhost_dequeue(GetGrachtClient(), &msg.base, OSProcessCurrentID(),
        deviceContext->controller_device_id, transferId);
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    ctt_usbhost_dequeue_result(GetGrachtClient(), &msg.base, &status);
    return status;
}

oserr_t
UsbHubResetPort(
        _In_ uuid_t                 hubDriverId,
        _In_ uuid_t                 deviceId,
        _In_ uint8_t                portAddress,
        _In_ USBPortDescriptor_t* portDescriptor)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(hubDriverId);
    oserr_t                  status;
    
    ctt_usbhub_reset_port(GetGrachtClient(), &msg.base, deviceId, portAddress);
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    ctt_usbhub_reset_port_result(GetGrachtClient(), &msg.base, &status, (uint8_t*)portDescriptor, sizeof(USBPortDescriptor_t));
    return status;
}

oserr_t
UsbHubQueryPort(
        _In_ uuid_t                 hubDriverId,
        _In_ uuid_t                 deviceId,
        _In_ uint8_t                portAddress,
        _In_ USBPortDescriptor_t* portDescriptor)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(hubDriverId);
    oserr_t                  status;
    
    ctt_usbhub_query_port(GetGrachtClient(), &msg.base, deviceId, portAddress);
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    ctt_usbhub_query_port_result(GetGrachtClient(), &msg.base, &status, (uint8_t*)portDescriptor, sizeof(USBPortDescriptor_t));
    return status;
}

oserr_t
UsbEndpointReset(
	_In_ usb_device_context_t* deviceContext, 
    _In_ uint8_t               endpointAddress)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(deviceContext->controller_driver_id);
    oserr_t                  status;
    
    ctt_usbhost_reset_endpoint(GetGrachtClient(), &msg.base, deviceContext->controller_device_id,
        deviceContext->hub_address, deviceContext->port_address,
        deviceContext->device_address, endpointAddress);
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    ctt_usbhost_reset_endpoint_result(GetGrachtClient(), &msg.base, &status);
    return status;
}

enum USBTransferCode
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
    enum USBTransferCode transferStatus;
    size_t               bytesTransferred;
    uint8_t              dataDirection;
    void*                dmaStorage;
    usb_packet_t*        packet;
    USBTransfer_t        transfer;
    oserr_t              oserr;

    oserr = dma_pool_allocate(g_dmaPool, sizeof(usb_packet_t) + length, &dmaStorage);
    if (oserr != OS_EOK) {
        ERROR("UsbExecutePacket: failed to allocate a transfer buffer");
        return USBTRANSFERCODE_INVALID;
    }

    packet              = (usb_packet_t*)dmaStorage;
    packet->Direction   = direction;
    packet->Type        = type;
    packet->ValueHi     = valueHigh;
    packet->ValueLo     = valueLow;
    packet->Index       = index;
    packet->Length      = length;

    if (direction & USBPACKET_DIRECTION_IN) {
        dataDirection = USBTRANSFER_DIRECTION_IN;
    } else {
        dataDirection = USBTRANSFER_DIRECTION_OUT;
        if (length != 0 && buffer != NULL) {
            memcpy(
                    ((char*)dmaStorage + sizeof(usb_packet_t)),
                    buffer,
                    length
            );
        }
    }

    // When initializing control transfers the first 9 bytes must be the
    // setup packet, and the remaining bytes must be the data that is to
    // be sent or read
    UsbTransferInitialize(
            &transfer,
            deviceContext,
            USB_TRANSFER_ENDPOINT_CONTROL,
            USBTRANSFER_TYPE_CONTROL,
            dataDirection,
            0,
            dma_pool_handle(g_dmaPool),
            dma_pool_offset(g_dmaPool, dmaStorage),
            sizeof(usb_packet_t) + length
    );

    // Execute the transaction and cleanup the buffer
    oserr = UsbTransferQueue(deviceContext, &transfer, &transferStatus, &bytesTransferred);
    if (oserr != OS_EOK || transferStatus != USBTRANSFERCODE_SUCCESS) {
        ERROR("Usb transfer returned error %u/%u", oserr, transferStatus);
    }

    if (transferStatus == USBTRANSFERCODE_SUCCESS && length != 0 &&
        buffer != NULL && dataDirection == USBTRANSFER_DIRECTION_IN) {
        memcpy(buffer, ((char*)dmaStorage + sizeof(usb_packet_t)), length);
    }

    dma_pool_free(g_dmaPool, dmaStorage);
    return transferStatus;
}

enum USBTransferCode
UsbSetAddress(
	_In_ usb_device_context_t* deviceContext,
    _In_ int                   address)
{
    enum USBTransferCode status;

    TRACE("UsbSetAddress()");

    if (deviceContext->device_address != 0) {
        return USBTRANSFERCODE_INVALID;
    }

    status = UsbExecutePacket(deviceContext, USBPACKET_DIRECTION_OUT,
        USBPACKET_TYPE_SET_ADDRESS, (uint8_t)(address & 0xFF), 0, 0, 0, NULL);
    return status;
}

enum USBTransferCode
UsbGetDeviceDescriptor(
	_In_ usb_device_context_t*    deviceContext,
    _In_ usb_device_descriptor_t* deviceDescriptor)
{
    const size_t DESCRIPTOR_SIZE = 0x12; // Max Descriptor Length is 18 bytes
    
    enum USBTransferCode status;
    
    status = UsbExecutePacket(
            deviceContext,
            USBPACKET_DIRECTION_IN,
            USBPACKET_TYPE_GET_DESC,
            0,
            USB_DESCRIPTOR_DEVICE,
            0,
            DESCRIPTOR_SIZE,
            deviceDescriptor
    );
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

static enum USBTransferCode
UsbGetInitialConfigDescriptor(
	_In_ usb_device_context_t*       deviceContext,
    _In_ usb_device_configuration_t* configuration)
{
    enum USBTransferCode status;
    
    status = UsbExecutePacket(deviceContext, USBPACKET_DIRECTION_IN,
        USBPACKET_TYPE_GET_DESC, 0, USB_DESCRIPTOR_CONFIG, 0, 
        sizeof(usb_config_descriptor_t), &configuration->base);
    return status;
}

enum USBTransferCode
UsbGetConfigDescriptor(
	_In_ usb_device_context_t*       deviceContext,
	_In_ int                         configurationIndex,
    _In_ usb_device_configuration_t* configuration)
{
    void*               descriptorStorage;
    enum USBTransferCode status;
    
    // Are we requesting the initial descriptor?
    if (deviceContext->configuration_length <= sizeof(usb_config_descriptor_t)) {
        status = UsbGetInitialConfigDescriptor(deviceContext, configuration);
        if (status != USBTRANSFERCODE_SUCCESS) {
            return status;
        }
        deviceContext->configuration_length = configuration->base.TotalLength;
    }

    descriptorStorage = malloc(deviceContext->configuration_length);
    if (!descriptorStorage) {
        return USBTRANSFERCODE_BUFFERERROR;
    }
    
    status = UsbExecutePacket(deviceContext, USBPACKET_DIRECTION_IN,
        USBPACKET_TYPE_GET_DESC, 0, USB_DESCRIPTOR_CONFIG,
        (uint16_t)(configurationIndex & 0xFFFF), deviceContext->configuration_length,
        descriptorStorage);
    if (status == USBTRANSFERCODE_SUCCESS) {
        ParseConfigurationDescriptor(descriptorStorage, configuration);
    }

    free(descriptorStorage);
    return status;
}

enum USBTransferCode
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

enum USBTransferCode
UsbSetConfiguration(
	_In_ usb_device_context_t* deviceContext,
    _In_ int                   configurationIndex)
{
    enum USBTransferCode status;
    
    status = UsbExecutePacket(deviceContext, USBPACKET_DIRECTION_OUT,
        USBPACKET_TYPE_SET_CONFIG, (uint8_t)(configurationIndex & 0xFF),
        0, 0, 0, NULL);
    return status;
}

enum USBTransferCode
UsbGetStringLanguages(
	_In_ usb_device_context_t*    deviceContext,
    _In_ usb_string_descriptor_t* descriptor)
{
    enum USBTransferCode status;
    
    status = UsbExecutePacket(deviceContext, USBPACKET_DIRECTION_IN,
        USBPACKET_TYPE_GET_DESC, 0, USB_DESCRIPTOR_STRING, 0,
        sizeof(usb_string_descriptor_t), descriptor);
    return status;
}

enum USBTransferCode
UsbGetStringDescriptor(
	_In_  usb_device_context_t* deviceContext,
    _In_  size_t                languageId,
    _In_  size_t                stringIndex,
    _Out_ mstring_t**           stringOut)
{
    usb_unicode_string_descriptor_t* desc;
    enum USBTransferCode              status;
    
    desc = malloc(sizeof(usb_unicode_string_descriptor_t) + 2);
    if (desc == NULL) {
        return USBTRANSFERCODE_INVALID;
    }

    status = UsbExecutePacket(
            deviceContext,
            USBPACKET_DIRECTION_IN,
            USBPACKET_TYPE_GET_DESC, (uint8_t)
            stringIndex, USB_DESCRIPTOR_STRING,
            (uint16_t)languageId,
            sizeof(usb_unicode_string_descriptor_t) + 2, desc);

    if (status == USBTRANSFERCODE_SUCCESS) {
        // zero terminate the UTF-16 string, we allocated an extra two bytes
        desc->string[desc->length - 2] = 0;
        desc->string[desc->length - 1] = 0;
        *stringOut = mstr_new_u16((const short*)&desc->string[0]);
    }

    free(desc);
    return status;
}

enum USBTransferCode
UsbClearFeature(
	_In_ usb_device_context_t* deviceContext,
    _In_ uint8_t               Target, 
    _In_ uint16_t              Index, 
    _In_ uint16_t              Feature)
{
    enum USBTransferCode status;
    
    status = UsbExecutePacket(deviceContext, Target,
        USBPACKET_TYPE_CLR_FEATURE, (uint8_t)Feature & 0xFF,
        (Feature >> 8) & 0xFF, Index, 0, NULL);
    return status;
}

enum USBTransferCode
UsbSetFeature(
	_In_ usb_device_context_t* deviceContext,
	_In_ uint8_t               Target, 
	_In_ uint16_t              Index, 
    _In_ uint16_t              Feature)
{
    enum USBTransferCode status;

    status = UsbExecutePacket(deviceContext, Target,
        USBPACKET_TYPE_SET_FEATURE, (uint8_t)Feature & 0xFF,
        (Feature >> 8) & 0xFF, Index, 0, NULL);
    return status;
}
