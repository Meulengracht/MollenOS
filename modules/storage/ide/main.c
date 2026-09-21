/**
 * MollenOS
 *
 * Copyright 2026, Philip Meulengracht
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
 * Integrated Drive Electronics Driver
 */

#include <ddk/convert.h>
#include <ddk/utils.h>
#include <ioset.h>
#include <os/types/device.h>
#include "ide.h"

#include <ctt_driver_service_server.h>
#include <ctt_storage_service_server.h>

extern gracht_server_t* __crt_get_module_server(void);

static list_t g_controllers = LIST_INIT;

static void
DestroyController(
    _In_ element_t* element,
    _In_ void*      context)
{
    IdeControllerDestroy((IdeController_t*)element->value);
}

oserr_t
OnLoad(void)
{
    gracht_server_register_protocol(__crt_get_module_server(), &ctt_driver_server_protocol);
    gracht_server_register_protocol(__crt_get_module_server(), &ctt_storage_server_protocol);
    return OS_EOK;
}

void
OnUnload(void)
{
    list_clear(&g_controllers, DestroyController, NULL);
}

oserr_t
OnEvent(struct ioset_event* event)
{
    (void)event;
    return OS_ENOTSUPPORTED;
}

void
ctt_driver_register_device_invocation(struct gracht_message* message, const struct sys_device* device)
{
    BusDevice_t* busDevice = (BusDevice_t*)from_sys_device(device);
    IdeController_t* controller;

    (void)message;
    controller = IdeControllerCreate(busDevice);
    if (!controller) {
        ERROR("ctt_driver_register_device_invocation failed to create ide controller");
        return;
    }

    list_append(&g_controllers, &controller->Header);
}

oserr_t
OnUnregister(
    _In_ Device_t* device)
{
    IdeController_t* controller = list_find_value(&g_controllers, (void*)(uintptr_t)device->Id);
    if (!controller) {
        return OS_ENOENT;
    }

    list_remove(&g_controllers, &controller->Header);
    IdeControllerDestroy(controller);
    return OS_EOK;
}

void
ctt_driver_ioctl_invocation(
    _In_ struct gracht_message* message,
    _In_ const uuid_t           deviceId,
    _In_ const unsigned int     request,
    _In_ const uint8_t*         out,
    _In_ const uint32_t         out_count)
{
    enum OSIOCtlRequest req = (enum OSIOCtlRequest)request;
    IdeDevice_t*         device = IdeDeviceGet(deviceId);

    (void)message;
    (void)out;
    (void)out_count;

    if (!device) {
        ctt_driver_ioctl_response(message, NULL, 0, OS_ENOENT);
        return;
    }

    switch (req) {
        case OSIOCTLREQUEST_IO_REQUIREMENTS: {
            struct OSIOCtlRequestRequirements ioRequirements;
            ioRequirements.BufferAlignment = 0;
            ioRequirements.Conformity = OSMEMORYCONFORMITY_NONE;
            ctt_driver_ioctl_response(message, (uint8_t*)&ioRequirements,
                                      sizeof(struct OSIOCtlRequestRequirements), OS_EOK);
            return;
        }

        default:
            break;
    }

    ctt_driver_ioctl_response(message, NULL, 0, OS_ENOTSUPPORTED);
}

void
ctt_storage_transfer_invocation(struct gracht_message* message,
                               const uuid_t deviceId,
                               const enum sys_transfer_direction direction,
                               const unsigned int sectorLow,
                               const unsigned int sectorHigh,
                               const uuid_t bufferId,
                               const size_t offset,
                               const size_t sectorCount)
{
    IdeDevice_t* device = IdeDeviceGet(deviceId);
    UInteger64_t sector;
    oserr_t      status;

    if (!device) {
        ctt_storage_transfer_response(message, OS_ENOENT, 0);
        return;
    }

    sector.u.LowPart = sectorLow;
    sector.u.HighPart = sectorHigh;

    if (direction != SYS_TRANSFER_DIRECTION_READ &&
        direction != SYS_TRANSFER_DIRECTION_WRITE) {
        ctt_storage_transfer_response(message, OS_EINVALPARAMS, 0);
        return;
    }

    if (direction == SYS_TRANSFER_DIRECTION_READ) {
        status = IdeDeviceRead(device, sector.QuadPart, sectorCount, bufferId, offset);
    }
    else {
        status = IdeDeviceWrite(device, sector.QuadPart, sectorCount, bufferId, offset);
    }

    if (status != OS_EOK) {
        ctt_storage_transfer_response(message, status, 0);
    }
}

void
ctt_storage_stat_invocation(struct gracht_message* message, const uuid_t deviceId)
{
    struct sys_disk_descriptor gdescriptor = { 0 };
    IdeDevice_t*              device = IdeDeviceGet(deviceId);
    oserr_t                   status = OS_ENOENT;

    if (device) {
        to_sys_disk_descriptor_dkk(&device->Descriptor, &gdescriptor);
        status = OS_EOK;
    }

    ctt_storage_stat_response(message, status, &gdescriptor);
}

void ctt_driver_get_device_protocols_invocation(struct gracht_message* message, const uuid_t deviceId) { }
void sys_device_event_protocol_device_invocation(void) { }
void sys_device_event_device_update_invocation(void) { }
