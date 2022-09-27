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

#include <ddk/service.h>
#include <ddk/utils.h>
#include <gracht/link/vali.h>
#include <internal/_utils.h>
#include <os/usched/job.h>
#include <served/setup.h>

#include <chef_served_service_server.h>
#include <sys_file_service_client.h>

extern gracht_server_t* __crt_get_service_server(void);

oserr_t OnUnload(void)
{
    return OsOK;
}

void GetServiceAddress(struct ipmsg_addr* address)
{
    address->type = IPMSG_ADDRESS_PATH;
    address->data.path = SERVICE_SERVED_PATH;
}

static int __SubscribeToFileEvents(void)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetFileService());
    return sys_file_subscribe(GetGrachtClient(), &msg.base);
}

oserr_t
OnLoad(void)
{
    // Wait for the file server to present itself, if it doesn't come online after 2 seconds
    // of waiting, then assume that the file server is dead or not included. In this case
    // served will kill itself as well.
    if (WaitForFileService(2000) == OsTimeout) {
        ERROR("OnLoad filed never started up, gave up after 2 seconds");
        exit(-ENOTSUP);
    }

    // Register supported interfaces
    gracht_server_register_protocol(__crt_get_service_server(), &chef_served_server_protocol);

    // Connect to the file server and listen for events
    if (__SubscribeToFileEvents()) {
        WARNING("OnLoad failed to subscribe to events from file-server.");
    }
    return OsOK;
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
