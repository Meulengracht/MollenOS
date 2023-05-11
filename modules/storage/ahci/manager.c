/**
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
 * Advanced Host Controller Interface Driver
 * TODO:
 *    - Port Multiplier Support
 *    - Power Management
 */

//#define __TRACE

#include <assert.h>
#include <ddk/convert.h>
#include <ddk/utils.h>
#include <internal/_utils.h>
#include <os/memory.h>
#include <os/shm.h>
#include "manager.h"

#include <ctt_storage_service_server.h>
#include <sys_storage_service_client.h>

extern int __crt_get_server_iod(void);

static list_t devices        = LIST_INIT;
static uuid_t g_nextDeviceId = 0;
static size_t g_frameSize;

static void
__flipbuffer(
    _In_ uint8_t* buffer,
    _In_ size_t   length)
{
    size_t pairs = length / 2;
    size_t i;

    // Iterate pairs in string, and swap
    for (i = 0; i < pairs; i++) {
        uint8_t temp      = buffer[i * 2];
        buffer[i * 2]     = buffer[i * 2 + 1];
        buffer[i * 2 + 1] = temp;
    }

    // Zero terminate by trimming trailing spaces
    for (i = (length - 1); i > 0; i--) {
        if (buffer[i] != ' ' && buffer[i] != '\0') {
            i += 1;
            if (i < length) {
                buffer[i] = '\0';
            }
            break;
        }
    }
}

oserr_t
AhciManagerInitialize(void)
{
    TRACE("AhciManagerInitialize()");
    g_frameSize = MemoryPageSize();
    return OS_EOK;
}

static void
AhciManagerDestroyDevice(
        _In_ element_t* element,
        _In_ void*      context)
{

}

void
AhciManagerDestroy(void)
{
    list_clear(&devices, AhciManagerDestroyDevice, NULL);
}

size_t
AhciManagerGetFrameSize(void)
{
    return g_frameSize;
}

AhciDevice_t*
AhciManagerGetDevice(
        _In_ uuid_t deviceId)
{
    return list_find_value(&devices, (void*)(uintptr_t)deviceId);
}

static AhciDevice_t* __CreateInitialDevice(
    _In_ AhciController_t* controller,
    _In_ AhciPort_t*       port,
    _In_ DeviceType_t      deviceType)
{
    AhciDevice_t* device;
    uuid_t        deviceId;

    TRACE("__CreateInitialDevice(controller=0x%" PRIxIN ", port=0x%" PRIxIN ", deviceType=%u)",
          controller, port, deviceType);

    device = (AhciDevice_t*)malloc(sizeof(AhciDevice_t));
    if (!device) {
        return NULL;
    }

    deviceId = g_nextDeviceId++;

    memset(device, 0, sizeof(AhciDevice_t));
    ELEMENT_INIT(&device->header, (uintptr_t)deviceId, device);
    device->Descriptor.DeviceID = deviceId;
    device->Controller = controller;
    device->Port       = port;
    device->Type       = deviceType;

    // Set initial addressing mode + initial values
    device->AddressingMode = 1;
    device->SectorSize     = 512;
    return device;
}

oserr_t
AhciManagerRegisterDevice(
    _In_ AhciController_t* controller,
    _In_ AhciPort_t*       port,
    _In_ uint32_t          signature)
{
    AhciDevice_t* ahciDevice;
    DeviceType_t  deviceType;
    oserr_t    osStatus;

    TRACE("AhciManagerRegisterDevice(controller=0x%" PRIxIN ", port=0x%" PRIxIN ", signature=0x%x)",
          controller, port, signature);
    
    switch (signature) {
        case SATA_SIGNATURE_ATA: {
            deviceType = DeviceATA;
        } break;
        case SATA_SIGNATURE_ATAPI: {
            deviceType = DeviceATAPI;
        } break;
        
        // We don't support these device types yet
        case SATA_SIGNATURE_SEMB:
        case SATA_SIGNATURE_PM:
        default: {
            WARNING("AhciManagerRegisterDevice unsupported device type 0x%x on port %i",
                    signature, port->Id);
            return OS_ENOTSUPPORTED;
        };
    }

    ahciDevice = __CreateInitialDevice(controller, port, deviceType);
    if (!ahciDevice) {
        ERROR("AhciManagerRegisterDevice ahciDevice was null");
        return OS_EOOM;
    }

    osStatus = AhciTransactionControlCreate(ahciDevice, AtaPIOIdentifyDevice,
                                            sizeof(ATAIdentify_t), __STORAGE_OPERATION_READ);
    if (osStatus != OS_EOK) {
        ERROR("AhciManagerRegisterDevice osStatus=%u", osStatus);
        free(ahciDevice);
        return osStatus;
    }

    list_append(&devices, &ahciDevice->header);
    return OS_EOK;
}

static void
__RegisterStorage(
        _In_ uuid_t       ProtocolServerId,
        _In_ uuid_t       DeviceId,
        _In_ unsigned int Flags)
{
    int                      status;
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetFileService());
    
    status = sys_storage_register(GetGrachtClient(), &msg.base, ProtocolServerId, DeviceId, Flags);
    if (status) {
        // @todo log message
    }
}

static void
__UnregisterStorage(
        _In_ uuid_t  deviceId,
        _In_ uint8_t forced)
{
    int                      status;
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetFileService());
    
    status = sys_storage_unregister(GetGrachtClient(), &msg.base, deviceId, forced);
    if (status) {
        // @todo log message
    }
}

void
AhciManagerUnregisterDevice(
    _In_ AhciController_t* controller,
    _In_ AhciPort_t*       port)
{
    AhciDevice_t* device = NULL;
    
    // Lookup device based on controller/port
    foreach(element, &devices) {
        AhciDevice_t* ahciDevice = (AhciDevice_t*)element->value;
        if (ahciDevice->Port == port) {
            device = ahciDevice;
            break;
        }
    }
    assert(device != NULL);

    __UnregisterStorage(device->Descriptor.DeviceID, 1);
    list_remove(&devices, &device->header);
}

static void
HandleIdentifyCommand(
    _In_ AhciDevice_t* device)
{
    ATAIdentify_t* deviceInformation =
        (ATAIdentify_t*)SHMBuffer(&device->Port->InternalBuffer);

    // Flip the data in the strings as it's inverted
    __flipbuffer(deviceInformation->SerialNo, 20);
    __flipbuffer(deviceInformation->ModelNo, 40);
    __flipbuffer(deviceInformation->FWRevision, 8);

    TRACE("HandleIdentifyCommand(%s)", &deviceInformation->ModelNo[0]);

    // Set capabilities
    if (deviceInformation->Capabilities0 & (1 << 0)) {
        device->HasDMAEngine = 1;
    }

    // Check addressing mode supported
    // Check that LBA is supported
    if (deviceInformation->Capabilities0 & (1 << 1)) {
        device->AddressingMode = 1; // LBA28
        if (deviceInformation->CommandSetSupport1 & (1 << 10)) {
            device->AddressingMode = 2; // LBA48
        }
    }
    else {
        device->AddressingMode = 0; // CHS
    }

    // Calculate sector size if neccessary
    if (deviceInformation->SectorSize & (1 << 12)) {
        device->SectorSize = deviceInformation->WordsPerLogicalSector * 2;
    }
    else {
        device->SectorSize = 512;
    }

    // Calculate sector count per physical sector
    if (deviceInformation->SectorSize & (1 << 13)) {
        device->SectorSize *= (deviceInformation->SectorSize & 0xF);
    }

    // Now, get the number of sectors for this particular disk
    if (deviceInformation->SectorCountLBA48 != 0) {
        device->SectorCount = deviceInformation->SectorCountLBA48;
    }
    else {
        device->SectorCount = deviceInformation->SectorCountLBA28;
    }

    // At this point the ahcidisk structure is filled,
    // and we can continue to fill out the descriptor
    memset(&device->Descriptor, 0, sizeof(StorageDescriptor_t));
    device->Descriptor.DriverID    = UUID_INVALID;
    device->Descriptor.Flags       = 0;
    device->Descriptor.SectorCount = device->SectorCount;
    device->Descriptor.SectorSize  = device->SectorSize;

    // Copy string data
    memcpy(&device->Descriptor.Model[0], (const void*)&deviceInformation->ModelNo[0], 40);
    memcpy(&device->Descriptor.Serial[0], (const void*)&deviceInformation->SerialNo[0], 20);
    __RegisterStorage(GetNativeHandle(__crt_get_server_iod()), device->Descriptor.DeviceID, device->Descriptor.Flags);
}

void
AhciManagerHandleControlResponse(
    _In_ AhciPort_t*        port,
    _In_ AhciTransaction_t* transaction)
{
    AhciDevice_t* device = NULL;
    
    // Lookup device based on controller/port
    foreach(element, &devices) {
        AhciDevice_t* i = (AhciDevice_t*)element->value;
        if (i->Port == port) {
            device = i;
            break;
        }
    }
    assert(device != NULL);
    
    switch (transaction->Command) {
        case AtaPIOIdentifyDevice: {
            HandleIdentifyCommand(device);
        } break;
        
        default: {
            WARNING("Unsupported ATA command 0x%x", transaction->Command);
        } break;
    }
}

void ctt_storage_stat_invocation(struct gracht_message* message, const uuid_t deviceId)
{
    struct sys_disk_descriptor gdescriptor = { 0 };
    oserr_t                 status = OS_ENOENT;
    AhciDevice_t*              device = AhciManagerGetDevice(deviceId);
    if (device) {
        to_sys_disk_descriptor_dkk(&device->Descriptor, &gdescriptor);
        status = OS_EOK;
    }
    
    ctt_storage_stat_response(message, status, &gdescriptor);
}
