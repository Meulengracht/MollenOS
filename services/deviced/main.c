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
 * Device Manager
 * - Implementation of the device manager in the operating system.
 *   Keeps track of devices, their loaded drivers and bus management.
 */

//#define __TRACE

#include <assert.h>
#include <bus/pci/discovery.h>
#include "devices.h"
#include "discover.h"
#include <ddk/service.h>
#include <ddk/utils.h>
#include <gracht/link/vali.h>
#include <internal/_utils.h>

#include <sys_device_service_server.h>
#include <ctt_driver_service_client.h>

void
ServiceInitialize(
    _In_ struct ServiceStartupOptions* startupOptions)
{
    int status;
    TRACE("ServiceInitialize()");

    // Register supported interfaces
    status = gracht_server_register_protocol(startupOptions->Server, &sys_device_server_protocol);
    if (status != 0) {
        ERROR("ServiceInitialize: device service protocol registration failed: %i", status);
    }

    // Register the client control protocol
    status = gracht_client_register_protocol(GetGrachtClient(), &ctt_driver_client_protocol);
    if (status != 0) {
        ERROR("ServiceInitialize: driver client protocol registration failed: %i", status);
    }

    // Initialize the subsystems
    DmDevicesInitialize();
    DmDiscoverInitialize();
    
    TRACE("ServiceInitialize: enumerating buses");
    BusEnumerate();
}
