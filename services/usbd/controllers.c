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
#include <ddk/utils.h>
#include <stdlib.h>
#include "manager.h"


static list_t g_controllers = LIST_INIT;

static void
__FreeControllerDevice(
    _In_ Device_t* device)
{
    free(device->Identification.Description);
    free(device->Identification.Manufacturer);
    free(device->Identification.Product);
    free(device->Identification.Revision);
    free(device->Identification.Serial);
    free(device);
}

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
        __FreeControllerDevice(device);
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
    osStatus = UsbCoreHubsRegister(device->Id, device->Id, driverId, rootPorts, 0);
    if (osStatus != OS_EOK) {
        free(controller);
        __FreeControllerDevice(device);
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
    // The controller must stay in the list while hub cleanup detaches devices
    // and releases their addresses, so cleanup can look it up.
    UsbCoreHubsUnregister(deviceId);
    list_remove(&g_controllers, &controller->Header);
    __FreeControllerDevice(controller->Device);
    free(controller);
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
            if (!(controller->AddressMap[i] & (1U << j))) {
                controller->AddressMap[i] |= (1U << j);
                *address = (i * 32) + j;
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

    controller->AddressMap[segment] &= ~(1U << offset);
}

void
UsbCoreControllersCleanup(void)
{
    UsbController_t* controller;

    // The list lock must be released before unregistering a controller. Detach
    // RPCs can yield, and unregister removes the entry from this list.
    // list_clear holds the lock while calling its cleanup callback.
    while (g_controllers.head != NULL) {
        controller = g_controllers.head->value;
        UsbCoreControllerUnregister(controller->Device->Id);
    }
}

int
UsbCoreControllerCount(void)
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

void
UsbCoreControllerSnapshot(
    _In_  int                    index,
    _Out_ USBControllerDevice_t* snapshot)
{
    UsbController_t* controller;

    // Run in the worker so the controller cannot be removed while we copy it.
    memset(snapshot, 0, sizeof(*snapshot));
    controller = __GetControllerIndex(index);
    if (controller != NULL) {
        memcpy(&snapshot->Device, controller->Device, sizeof(Device_t));
        snapshot->Kind = controller->Type;
    }
}
