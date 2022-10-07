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

#define __TRACE

#include <assert.h>
#include <bus.h>
#include "devices.h"
#include "discover.h"
#include <ddk/service.h>
#include <gracht/link/vali.h>
#include <internal/_utils.h>
#include <os/usched/job.h>

#include <sys_device_service_server.h>
#include <ctt_driver_service_client.h>

extern gracht_server_t* __crt_get_service_server(void);

oserr_t
OnUnload(void)
{
    return OsOK;
}

void
GetServiceAddress(
        _In_ struct ipmsg_addr* address)
{
    address->type = IPMSG_ADDRESS_PATH;
    address->data.path = SERVICE_DEVICE_PATH;
}

oserr_t
OnLoad(void)
{
    // Register supported interfaces
    gracht_server_register_protocol(__crt_get_service_server(), &sys_device_server_protocol);

    // Register the client control protocol
    gracht_client_register_protocol(GetGrachtClient(), &ctt_driver_client_protocol);

    // Initialize the subsystems
    DmDevicesInitialize();
    DmDiscoverInitialize();

    // Start the enumeration process in a new thread, so we can quickly return
    // and be ready for requests.
    usched_job_queue((usched_task_fn)BusEnumerate, NULL);
    return OsOK;
}
