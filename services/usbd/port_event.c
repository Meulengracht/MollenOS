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
 * Service - USB Manager
 * Handles USB port connection changes and port errors.
 */

#define __TRACE

#include <ddk/devices/usb.h>
#include <ddk/utils.h>
#include "manager.h"
#include <stdlib.h>


void
UsbCorePortEvent(
    _In_ uuid_t  hubDeviceId,
    _In_ uint8_t portAddress)
{
    UsbController_t*      controller = NULL;
    USBPortDescriptor_t portDescriptor;
    oserr_t             oserr;
    UsbHub_t*             hub;
    UsbPort_t*            port = NULL;

    TRACE("UsbCorePortEvent(deviceId=%u, portAddress=%u)", hubDeviceId, portAddress);

    // The hub and its controller must still be registered.
    hub = UsbCoreHubsGet(hubDeviceId);
    if (!hub) {
        ERROR("UsbCorePortEvent hub device id was invalid");
        return;
    }

    controller = UsbCoreControllerGet(hub->ControllerDeviceId);
    if (!controller) {
        ERROR("UsbCorePortEvent controller related to hub was invalid");
        return;
    }

    // Read the port status before comparing it with the saved connection state.
    if (UsbHubQueryPort(hub->DriverId, hubDeviceId, portAddress, &portDescriptor) != OS_EOK) {
        ERROR("UsbCorePortEvent Query port failed");
        return;
    }

    port = UsbCoreHubsGetPort(hub, portAddress);
    if (!port) {
        ERROR("UsbCorePortEvent failed to retrieve port instance");
        return;
    }

    // Create or remove a device only when the connection state changes.
    if (portDescriptor.Connected == 1 && port->Connected == 0) {
        // Device setup updates the port status after reset.
        oserr = UsbCoreDevicesCreate(controller, hub, port);
        if (oserr != OS_EOK) {
            WARNING("USB: port %u event failed (%u)", portAddress, oserr);
        }
    } else if (portDescriptor.Connected == 0 && port->Connected == 1) {
        // The device pointer is no longer valid after device removal.
        oserr = UsbCoreDevicesDestroy(controller, port);
        if (oserr != OS_EOK) {
            WARNING("USB: port %u event failed (%u)", portAddress, oserr);
        }

        port->Speed     = portDescriptor.Speed;              // TODO: invalid
        port->Enabled   = portDescriptor.Enabled;            // TODO: invalid
        port->Connected = portDescriptor.Connected;          // TODO: invalid
    } else {
        // Refresh the port status without changing the attached device.
        port->Speed     = portDescriptor.Speed;
        port->Enabled   = portDescriptor.Enabled;
        port->Connected = portDescriptor.Connected;
    }
}

void
UsbCorePortError(
    _In_ uuid_t  hubDeviceId,
    _In_ uint8_t portAddress)
{
    UsbController_t* controller;
    UsbHub_t*        hub;
    UsbPort_t*       port;

    TRACE("UsbCorePortError(deviceId=%u, portAddress=%u)", hubDeviceId, portAddress);

    // The hub and its controller must still be registered.
    hub = UsbCoreHubsGet(hubDeviceId);
    if (!hub) {
        ERROR("UsbCorePortError hub device id was invalid");
        return;
    }

    controller = UsbCoreControllerGet(hub->ControllerDeviceId);
    if (!controller) {
        ERROR("UsbCorePortError controller related to hub was invalid");
        return;
    }

    port = UsbCoreHubsGetPort(hub, portAddress);
    if (port && port->Device) {
        UsbCoreDevicesDestroy(controller, port);
    }
}
