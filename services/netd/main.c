/**
 * MollenOS
 *
 * Copyright, Philip Meulengracht
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

#include <internal/_utils.h>
#include <ddk/service.h>
#include <ddk/utils.h>
#include <stdlib.h>

// server interfaces
#include <sys_socket_service_server.h>

// client interfaces
#include <sys_device_service_client.h>

#include "adapters/adapters.h"
#include "manager.h"

static void
__SubscribeToDeviceService(void)
{
    struct vali_link_message context = VALI_MSG_INIT_HANDLE(GetDeviceService());
    sys_device_subscribe(GetGrachtClient(), &context.base);
}

static void
__DiscoverAdapters(void)
{
    struct vali_link_message context = VALI_MSG_INIT_HANDLE(GetDeviceService());
    sys_device_get_devices_by_protocol(
        GetGrachtClient(),
        &context.base,
        SERVICE_CTT_NETADAPTER_ID
    );
}

void ServiceInitialize(
    _In_ struct ServiceStartupOptions* startupOptions)
{
    // Register supported server interfaces
    gracht_server_register_protocol(
        startupOptions->Server,
        &sys_socket_server_protocol
    );

    // Register supported client interfaces
    gracht_client_register_protocol(
        GetGrachtClient(),
        &sys_device_client_protocol
    );

    // Initialize the subsystems
    if (NetworkManagerInitialize() != OS_EOK) {
        exit(-1);
    }

    if (NetworkAdaptersInitialize() != OS_EOK) {
        exit(-1);
    }

    // Wait for the device service to be available
    if (WaitForDeviceService(1000) != OS_EOK) {
        ERROR("[netd] device service not available");
        exit(-1);
    }

    // Subscribe to events before we kick off discovery
    __SubscribeToDeviceService();
    __DiscoverAdapters();

#ifdef VALI_NET_ADAPTER_TESTS
    NetworkAdaptersRunIpcTests();
#endif
#ifdef VALI_VIRTIO_NET_TESTS
    NetworkAdaptersRunVirtioTests();
#endif
}

void
sys_device_event_protocol_device_invocation(
        gracht_client_t* client,
        const uuid_t     deviceId,
        const uuid_t     driverId,
        const uint8_t    protocolId)
{
    // We only care about network adapter protocol events
    if (protocolId != SERVICE_CTT_NETADAPTER_ID) {
        return;
    }
    NetworkAdaptersDiscover(deviceId, driverId);
}

void
sys_device_event_device_update_invocation(
        gracht_client_t* client,
        const uuid_t     deviceId,
        const uint8_t    connected)
{
    // We only care about network adapter disconnection events
    if (connected) {
        return;
    }
    NetworkAdaptersRemove(deviceId);
}
