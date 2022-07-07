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
 * Mass Storage Device Driver (Generic)
 */
//#define __TRACE

#include <ddk/convert.h>
#include <ddk/storage.h>
#include <ddk/utils.h>
#include <ioset.h>
#include "msd.h"
#include <string.h>
#include <stdlib.h>

#include <ctt_driver_service_server.h>
#include <ctt_storage_service_server.h>

extern gracht_server_t* __crt_get_module_server(void);

static list_t g_devices = LIST_INIT;

MsdDevice_t*
MsdDeviceGet(
        _In_ uuid_t deviceId)
{
    return list_find_value(&g_devices, (void*)(uintptr_t)deviceId);
}

oscode_t
OnLoad(void)
{
    // Register supported protocols
    gracht_server_register_protocol(__crt_get_module_server(), &ctt_driver_server_protocol);
    gracht_server_register_protocol(__crt_get_module_server(), &ctt_storage_server_protocol);
    return UsbInitialize();
}

static void
DestroyElement(
    _In_ element_t* Element,
    _In_ void*      Context)
{
    MsdDeviceDestroy(Element->value);
}

oscode_t
OnUnload(void)
{
    list_clear(&g_devices, DestroyElement, NULL);
    return UsbCleanup();
}

oscode_t OnEvent(struct ioset_event* event)
{
    return OsNotSupported;
}

oscode_t
OnRegister(
    _In_ Device_t *Device)
{
    MsdDevice_t* MsdDevice;
    
    MsdDevice = MsdDeviceCreate((UsbDevice_t*)Device);
    if (MsdDevice == NULL) {
        return OsError;
    }

    list_append(&g_devices, &MsdDevice->Header);
    return OsOK;
}

void ctt_driver_register_device_invocation(struct gracht_message* message, const uint8_t* device, const uint32_t device_count)
{
    OnRegister((Device_t*)device);
}

oscode_t
OnUnregister(
    _In_ Device_t *Device)
{
    MsdDevice_t* MsdDevice = MsdDeviceGet(Device->Id);
    if (MsdDevice == NULL) {
        return OsError;
    }

    list_remove(&g_devices, &MsdDevice->Header);
    return MsdDeviceDestroy(MsdDevice);
}

void ctt_storage_stat_invocation(struct gracht_message* message, const uuid_t deviceId)
{
    struct sys_disk_descriptor gdescriptor = { 0 };
    oscode_t                 status     = OsNotExists;
    MsdDevice_t*               device     = MsdDeviceGet(deviceId);
    TRACE("[msd] [stat]");
    
    if (device) {
        TRACE("[msd] [stat] sectorCount %llu, sectorSize %u",
            device->Descriptor.SectorCount,
            device->Descriptor.SectorSize);
        to_sys_disk_descriptor_dkk(&device->Descriptor, &gdescriptor);
        status = OsOK;
    }
    
    ctt_storage_stat_response(message, status, &gdescriptor);
}

// lazyness
void ctt_driver_get_device_protocols_invocation(struct gracht_message* message, const uuid_t deviceId) { }
void sys_device_event_protocol_device_invocation(void) { }
void sys_device_event_device_update_invocation(void) { }
void ctt_usbhost_event_transfer_status_invocation(void) { }
void ctt_usbhub_event_port_status_invocation(void) { }
