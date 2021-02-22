/**
 * MollenOS
 *
 * Copyright 2021, Philip Meulengracht
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
 * Service - Usb Manager
 * - Contains the implementation of the usb-manager which keeps track
 *   of all usb-controllers and their devices
 */

#define __TRACE

#include <ddk/usbdevice.h>
#include <usb/usb.h>
#include <ddk/device.h>
#include <ddk/utils.h>
#include <internal/_ipc.h>
#include "manager.h"
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include "svc_usb_protocol_server.h"

static list_t g_controllers = LIST_INIT;

OsStatus_t
UsbCoreControllerRegister(
        _In_ UUId_t              driverId,
        _In_ Device_t*           device,
        _In_ UsbControllerType_t controllerType,
        _In_ int                 rootPorts)
{
    UsbController_t* controller;
    OsStatus_t       osStatus;
    TRACE("UsbCoreControllerRegister(driverId=%u, device=0x%" PRIxIN ", controllerType=%u, rootPorts=%i)",
          driverId, device, controllerType, rootPorts);

    // Allocate a new instance and reset all members
    controller = (UsbController_t*)malloc(sizeof(UsbController_t));
    if (!controller) {
        return OsOutOfMemory;
    }
    memset(controller, 0, sizeof(UsbController_t));

    ELEMENT_INIT(&controller->Header, 0, controller);
    memcpy(&controller->Device, device, sizeof(Device_t));
    controller->DriverId  = driverId;
    controller->Type      = controllerType;

    // Reserve address 0, it's setup address
    controller->AddressMap[0] |= 0x1;

    // Register root hub
    osStatus = UsbCoreHubsRegister(device->Id, device->Id, driverId, rootPorts);
    if (osStatus != OsSuccess) {
        free(controller);
        return osStatus;
    }

    list_append(&g_controllers, &controller->Header);
    return OsSuccess;
}

OsStatus_t
UsbCoreControllerUnregister(
        _In_ UUId_t deviceId)
{
    UsbController_t* controller;
    TRACE("UsbCoreControllerUnregister(deviceId=%u)", deviceId);

    controller = UsbCoreControllerGet(deviceId);
    if (controller == NULL) {
        return OsInvalidParameters;
    }
    list_remove(&g_controllers, &controller->Header);
    free(controller);

    UsbCoreHubsUnregister(deviceId);
    return OsSuccess;
}

UsbController_t*
UsbCoreControllerGet(
        _In_ UUId_t deviceId)
{
    foreach(element, &g_controllers) {
        UsbController_t* controller = (UsbController_t*)element->value;
        if (controller->Device.Id == deviceId) {
            return controller;
        }
    }
    return NULL;
}

OsStatus_t
UsbCoreControllerReserveAddress(
        _In_  UsbController_t* controller,
        _Out_ int*             address)
{
    int i = 0, j;

    for (; i < 4; i++) {
        for (j = 0; j < 32; j++) {
            if (!(controller->AddressMap[i] & (1 << j))) {
                controller->AddressMap[i] |= (1 << j);
                *address = (i * 4) + j;
                return OsSuccess;
            }
        }
    }
    return OsDoesNotExist;
}

void
UsbCoreControllerReleaseAddress(
        _In_ UsbController_t* controller,
        _In_ int              address)
{
    int segment = (address / 32);
    int offset  = (address % 32);

    // Sanitize bounds of address
    if (address <= 0 || address > 127) {
        return;
    }

    controller->AddressMap[segment] &= ~(1 << offset);
}

static void __CleanupControllerEntry(
        _In_ element_t* element,
        _In_ void*      context)
{
    UsbController_t* controller = (UsbController_t*)element->value;
    UsbCoreControllerUnregister(controller->Device.Id);
}

void UsbCoreControllersCleanup(void)
{
    list_clear(&g_controllers, __CleanupControllerEntry, NULL);
}

static int __GetControllerCount(void)
{
    return list_count(&g_controllers);
}

static UsbController_t* __GetControllerIndex(
        _In_ int index)
{
    element_t* item;
    int        i = 0;

    _foreach(item, &g_controllers) {
        if (i == index) {
            return (UsbController_t*)item->value;
        } i++;
    }
    return NULL;
}

void svc_usb_register_controller_callback(
        _In_ struct gracht_recv_message*              message,
        _In_ struct svc_usb_register_controller_args* args)
{
    UsbCoreControllerRegister(args->driver_id, args->device, (UsbControllerType_t)args->type, args->port_count);
}

void svc_usb_unregister_controller_callback(
        _In_ struct gracht_recv_message*                message,
        _In_ struct svc_usb_unregister_controller_args* args)
{
    UsbCoreControllerUnregister(args->device_id);
}

void svc_usb_get_controller_count_callback(struct gracht_recv_message* message)
{
    svc_usb_get_controller_count_response(message, __GetControllerCount());
}

void svc_usb_get_controller_callback(struct gracht_recv_message* message, struct svc_usb_get_controller_args* args)
{
    UsbHcController_t hcController = { { { 0 } }, 0 };
    UsbController_t*  controller;

    controller = __GetControllerIndex(args->index);
    if (controller != NULL) {
        memcpy(&hcController.Device, &controller->Device, sizeof(Device_t));
        hcController.Type = controller->Type;
    }
    svc_usb_get_controller_response(message, &hcController);
}
