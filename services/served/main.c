/**
 * Copyright 2022, Philip Meulengracht
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
 */

#define __TRACE

#include <errno.h>
#include <ddk/service.h>
#include <ddk/utils.h>
#include <gracht/link/vali.h>
#include <internal/_utils.h>
#include <os/usched/job.h>
#include <served/setup.h>
#include <stdlib.h>
#include <string.h>

#include <chef_served_service_server.h>
#include <sys_file_service_client.h>

static int __SubscribeToFileEvents(void)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetFileService());
    return sys_file_subscribe(GetGrachtClient(), &msg.base);
}

void ServiceInitialize(
        _In_ struct ServiceStartupOptions* startupOptions)
{
    // Wait for the file server to present itself, if it doesn't come online after 2 seconds
    // of waiting, then assume that the file server is dead or not included. In this case
    // served will kill itself as well.
    if (WaitForFileService(2000) == OS_ETIMEOUT) {
        ERROR("ServiceInitialize filed never started up, gave up after 2 seconds");
        exit(-ENOTSUP);
    }

    // Register supported client interfaces. These are the interfaces where we
    // listen to events from other servers.
    gracht_client_register_protocol(GetGrachtClient(), &sys_file_client_protocol);

    // Register supported server interfaces.
    gracht_server_register_protocol(startupOptions->Server, &chef_served_server_protocol);

    // Connect to the file server and listen for events
    if (__SubscribeToFileEvents()) {
        WARNING("ServiceInitialize failed to subscribe to events from file-server.");
    }
}

void sys_file_event_storage_ready_invocation(gracht_client_t* client, const char* path)
{
    TRACE("sys_file_event_storage_ready_invocation(path=%s)", path);
    _CRT_UNUSED(client);

    // wait for /data to attach
    if (strstr(path, "/data")) {
        usched_job_queue(served_server_setup_job, NULL);
    }
}
