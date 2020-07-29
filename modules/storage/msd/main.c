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
 * Mass Storage Device Driver (Generic)
 */
//#define __TRACE

#include <ddk/storage.h>
#include <ddk/utils.h>
#include <ioset.h>
#include "msd.h"
#include <string.h>
#include <stdlib.h>

#include <ctt_driver_protocol_server.h>

static void ctt_driver_register_device_callback(struct gracht_recv_message* message, struct ctt_driver_register_device_args*);

static gracht_protocol_function_t ctt_driver_callbacks[1] = {
        { PROTOCOL_CTT_DRIVER_REGISTER_DEVICE_ID , ctt_driver_register_device_callback },
};
DEFINE_CTT_DRIVER_SERVER_PROTOCOL(ctt_driver_callbacks, 1);

#include <ctt_storage_protocol_server.h>

extern void ctt_storage_stat_callback(struct gracht_recv_message* message, struct ctt_storage_stat_args*);
extern void ctt_storage_transfer_async_callback(struct gracht_recv_message* message, struct ctt_storage_transfer_async_args*);
extern void ctt_storage_transfer_callback(struct gracht_recv_message* message, struct ctt_storage_transfer_args*);

static gracht_protocol_function_t ctt_storage_callbacks[3] = {
    { PROTOCOL_CTT_STORAGE_STAT_ID , ctt_storage_stat_callback },
    { PROTOCOL_CTT_STORAGE_TRANSFER_ASYNC_ID , ctt_storage_transfer_async_callback },
    { PROTOCOL_CTT_STORAGE_TRANSFER_ID , ctt_storage_transfer_callback },
};
DEFINE_CTT_STORAGE_SERVER_PROTOCOL(ctt_storage_callbacks, 3);

static list_t Devices = LIST_INIT;

MsdDevice_t*
MsdDeviceGet(
    _In_ UUId_t deviceId)
{
    return list_find_value(&Devices, (void*)(uintptr_t)deviceId);
}

void GetModuleIdentifiers(unsigned int* vendorId, unsigned int* deviceId,
    unsigned int* class, unsigned int* subClass)
{
    *vendorId = 0;
    *deviceId = 0;
    *class    = 0xCABB;
    *subClass = 0x80000;
}

OsStatus_t
OnLoad(void)
{
    // Register supported protocols
    gracht_server_register_protocol(&ctt_driver_server_protocol);
    gracht_server_register_protocol(&ctt_storage_server_protocol);
    return UsbInitialize();
}

static void
DestroyElement(
    _In_ element_t* Element,
    _In_ void*      Context)
{
    MsdDeviceDestroy(Element->value);
}

OsStatus_t
OnUnload(void)
{
    list_clear(&Devices, DestroyElement, NULL);
    return UsbCleanup();
}

OsStatus_t OnEvent(struct ioset_event* event)
{
    return OsNotSupported;
}

OsStatus_t
OnRegister(
    _In_ Device_t *Device)
{
    MsdDevice_t* MsdDevice;
    
    MsdDevice = MsdDeviceCreate((UsbDevice_t*)Device);
    if (MsdDevice == NULL) {
        return OsError;
    }

    list_append(&Devices, &MsdDevice->Header);
    return OsSuccess;
}

void ctt_driver_register_device_callback(struct gracht_recv_message* message, struct ctt_driver_register_device_args* args)
{
    OnRegister(args->device);
}

OsStatus_t
OnUnregister(
    _In_ Device_t *Device)
{
    MsdDevice_t* MsdDevice = MsdDeviceGet(Device->Id);
    if (MsdDevice == NULL) {
        return OsError;
    }

    list_remove(&Devices, &MsdDevice->Header);
    return MsdDeviceDestroy(MsdDevice);
}

void ctt_storage_stat_callback(struct gracht_recv_message* message, struct ctt_storage_stat_args* args)
{
    StorageDescriptor_t descriptor = { 0 };
    OsStatus_t          status     = OsDoesNotExist;
    MsdDevice_t*        device     = MsdDeviceGet(args->device_id);
    TRACE("[msd] [stat]");
    
    if (device) {
        TRACE("[msd] [stat] sectorCount %llu, sectorSize %u",
            device->Descriptor.SectorCount,
            device->Descriptor.SectorSize);
        memcpy(&descriptor, &device->Descriptor, sizeof(StorageDescriptor_t));
        status = OsSuccess;
    }
    
    ctt_storage_stat_response(message, status, &descriptor);
}
