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

#include <ddk/usbdevice.h>
#include <ddk/utils.h>
#include "manager.h"
#include <stdlib.h>

#include "sys_usb_service_server.h"

void __HandlePortEvent(
        _In_ uuid_t  hubDeviceId,
        _In_ uint8_t portAddress)
{
    UsbController_t*      controller = NULL;
    USBPortDescriptor_t portDescriptor;
    oserr_t             oserr;
    UsbHub_t*             hub;
    UsbPort_t*            port = NULL;

    TRACE("__HandlePortEvent(deviceId=%u, portAddress=%u)", hubDeviceId, portAddress);

    // Retrieve hub, controller and port instances
    hub = UsbCoreHubsGet(hubDeviceId);
    if (!hub) {
        ERROR("__HandlePortEvent hub device id was invalid");
        return;
    }

    controller = UsbCoreControllerGet(hub->ControllerDeviceId);
    if (!controller) {
        ERROR("__HandlePortEvent controller related to hub was invalid");
        return;
    }

    // Query port status so we know the status of the port
    // Also compare to the current state to see if the change was valid
    if (UsbHubQueryPort(hub->DriverId, hubDeviceId, portAddress, &portDescriptor) != OS_EOK) {
        ERROR("__HandlePortEvent Query port failed");
        return;
    }

    port = UsbCoreHubsGetPort(hub, portAddress);
    if (!port) {
        ERROR("__HandlePortEvent failed to retrieve port instance");
        return;
    }

    // Now handle connection events
    if (portDescriptor.Connected == 1 && port->Connected == 0) {
        // Connected event
        // This function updates port-status after reset
        oserr = UsbCoreDevicesCreate(controller, hub, port);
        if (oserr != OS_EOK) {
            // TODO
        }
    } else if (portDescriptor.Connected == 0 && port->Connected == 1) {
        // Disconnected event, remember that the descriptor pointer
        // becomes unavailable the moment we call the destroy device
        oserr = UsbCoreDevicesDestroy(controller, port);
        if (oserr != OS_EOK) {
            // TODO
        }

        port->Speed     = portDescriptor.Speed;              // TODO: invalid
        port->Enabled   = portDescriptor.Enabled;            // TODO: invalid
        port->Connected = portDescriptor.Connected;          // TODO: invalid
    } else {
        // Ignore
        port->Speed     = portDescriptor.Speed;
        port->Enabled   = portDescriptor.Enabled;
        port->Connected = portDescriptor.Connected;
    }
}

void __HandlePortError(
        _In_ uuid_t  hubDeviceId,
        _In_ uint8_t portAddress)
{
    UsbController_t* controller;
    UsbHub_t*        hub;
    UsbPort_t*       port;

    TRACE("__HandlePortError(deviceId=%u, portAddress=%u)", hubDeviceId, portAddress);

    // Retrieve hub, controller and port instances
    hub = UsbCoreHubsGet(hubDeviceId);
    if (!hub) {
        ERROR("__HandlePortError hub device id was invalid");
        return;
    }

    controller = UsbCoreControllerGet(hub->ControllerDeviceId);
    if (!controller) {
        ERROR("__HandlePortError controller related to hub was invalid");
        return;
    }

    port = UsbCoreHubsGetPort(hub, portAddress);
    if (port && port->Device) {
        UsbCoreDevicesDestroy(controller, port);
    }
}

void sys_usb_port_event_invocation(struct gracht_message* message, const uuid_t hubDeviceId, const uint8_t portAddress)
{
    __HandlePortEvent(hubDeviceId, portAddress);
}

void sys_usb_port_error_invocation(struct gracht_message* message, const uuid_t hubDeviceId, const uint8_t portAddress)
{
    __HandlePortError(hubDeviceId, portAddress);
}
