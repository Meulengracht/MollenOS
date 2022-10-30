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
#include <ddk/utils.h>
#include <ioset.h>
#include "msd.h"

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

oserr_t
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

void
OnUnload(void)
{
    list_clear(&g_devices, DestroyElement, NULL);
    UsbCleanup();
}

oserr_t OnEvent(struct ioset_event* event)
{
    return OS_ENOTSUPPORTED;
}

oserr_t
OnRegister(
    _In_ Device_t *Device)
{
    MsdDevice_t* MsdDevice;
    
    MsdDevice = MsdDeviceCreate((UsbDevice_t*)Device);
    if (MsdDevice == NULL) {
        return OS_EUNKNOWN;
    }

    list_append(&g_devices, &MsdDevice->Header);
    return OS_EOK;
}

void ctt_driver_register_device_invocation(struct gracht_message* message, const struct sys_device* device)
{
    OnRegister(from_sys_device(device));
}

oserr_t
OnUnregister(
    _In_ Device_t *Device)
{
    MsdDevice_t* MsdDevice = MsdDeviceGet(Device->Id);
    if (MsdDevice == NULL) {
        return OS_EUNKNOWN;
    }

    list_remove(&g_devices, &MsdDevice->Header);
    return MsdDeviceDestroy(MsdDevice);
}

void ctt_storage_stat_invocation(struct gracht_message* message, const uuid_t deviceId)
{
    struct sys_disk_descriptor gdescriptor = { 0 };
    oserr_t                    oserr      = OS_ENOENT;
    MsdDevice_t*               device     = MsdDeviceGet(deviceId);
    TRACE("[msd] [stat]");
    
    if (device) {
        TRACE("[msd] [stat] sectorCount %llu, sectorSize %u",
            device->Descriptor.SectorCount,
            device->Descriptor.SectorSize);
        to_sys_disk_descriptor_dkk(&device->Descriptor, &gdescriptor);
        oserr = OS_EOK;
    }
    
    ctt_storage_stat_response(message, oserr, &gdescriptor);
}

// lazyness
void ctt_driver_get_device_protocols_invocation(struct gracht_message* message, const uuid_t deviceId) { }
void sys_device_event_protocol_device_invocation(void) { }
void sys_device_event_device_update_invocation(void) { }
void ctt_usbhost_event_transfer_status_invocation(void) { }
void ctt_usbhub_event_port_status_invocation(void) { }
