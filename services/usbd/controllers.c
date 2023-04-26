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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Service - Usb Manager
 * - Contains the implementation of the usb-manager which keeps track
 *   of all usb-controllers and their devices
 */

#define __TRACE

#include <usb/usb.h>
#include <ddk/convert.h>
#include <ddk/utils.h>
#include "manager.h"

#include "sys_usb_service_server.h"

static list_t g_controllers = LIST_INIT;

oserr_t
UsbCoreControllerRegister(
        _In_ uuid_t              driverId,
        _In_ Device_t*           device,
        _In_ enum USBControllerKind controllerType,
        _In_ int                 rootPorts)
{
    UsbController_t* controller;
    oserr_t          osStatus;
    TRACE("UsbCoreControllerRegister(driverId=%u, device=0x%" PRIxIN ", controllerType=%u, rootPorts=%i)",
          driverId, device, controllerType, rootPorts);

    // Allocate a new instance and reset all members
    controller = (UsbController_t*)malloc(sizeof(UsbController_t));
    if (!controller) {
        return OS_EOOM;
    }
    memset(controller, 0, sizeof(UsbController_t));

    ELEMENT_INIT(&controller->Header, 0, controller);
    controller->Device    = device;
    controller->DriverId  = driverId;
    controller->Type      = controllerType;

    // Reserve address 0, it's setup address
    controller->AddressMap[0] |= 0x1;

    // Register root hub
    osStatus = UsbCoreHubsRegister(device->Id, device->Id, driverId, rootPorts);
    if (osStatus != OS_EOK) {
        free(controller);
        return osStatus;
    }

    list_append(&g_controllers, &controller->Header);
    return OS_EOK;
}

oserr_t
UsbCoreControllerUnregister(
        _In_ uuid_t deviceId)
{
    UsbController_t* controller;
    TRACE("UsbCoreControllerUnregister(deviceId=%u)", deviceId);

    controller = UsbCoreControllerGet(deviceId);
    if (controller == NULL) {
        return OS_EINVALPARAMS;
    }
    list_remove(&g_controllers, &controller->Header);
    free(controller->Device);
    free(controller);

    UsbCoreHubsUnregister(deviceId);
    return OS_EOK;
}

UsbController_t*
UsbCoreControllerGet(
        _In_ uuid_t deviceId)
{
    foreach(element, &g_controllers) {
        UsbController_t* controller = (UsbController_t*)element->value;
        if (controller->Device->Id == deviceId) {
            return controller;
        }
    }
    return NULL;
}

oserr_t
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
                return OS_EOK;
            }
        }
    }
    return OS_ENOENT;
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
    UsbCoreControllerUnregister(controller->Device->Id);
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

void sys_usb_register_controller_invocation(struct gracht_message* message, const uuid_t driverId,
        const struct sys_device* device, const int type, const int portCount)
{
    UsbCoreControllerRegister(
            driverId, from_sys_device(device),
            (enum USBControllerKind)type, portCount
    );
}

void sys_usb_unregister_controller_invocation(struct gracht_message* message, const uuid_t deviceId)
{
    UsbCoreControllerUnregister(deviceId);
}

void sys_usb_get_controller_count_invocation(struct gracht_message* message)
{
    sys_usb_get_controller_count_response(message, __GetControllerCount());
}

void sys_usb_get_controller_invocation(struct gracht_message* message, const int index)
{
    USBControllerDevice_t hcController = {{{0 } }, 0 };
    UsbController_t*  controller;

    controller = __GetControllerIndex(index);
    if (controller != NULL) {
        memcpy(&hcController.Device, &controller->Device, sizeof(Device_t));
        hcController.Kind = controller->Type;
    }
    sys_usb_get_controller_response(message, (uint8_t*)&hcController, sizeof(USBControllerDevice_t));
}
