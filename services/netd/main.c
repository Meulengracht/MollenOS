/**
 * MollenOS
 *
 * Copyright 2019, Philip Meulengracht
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
 * Network Manager
 * - Contains the implementation of the network-manager which keeps track
 *   of sockets, network interfaces and connectivity status
 */

//#define __TRACE

#include <ddk/service.h>
#include "manager.h"
#include <stdlib.h>

#include <sys_socket_service_server.h>

void ServiceInitialize(
        _In_ struct ServiceStartupOptions* startupOptions)
{
    // Register supported interfaces
    gracht_server_register_protocol(startupOptions->Server, &sys_socket_server_protocol);

    // Initialize the subsystems
    if (NetworkManagerInitialize() != OS_EOK) {
        exit(-1);
    }
}
