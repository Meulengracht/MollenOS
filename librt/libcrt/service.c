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
 * Service Initialization procedure. Contains entry point and environmental
 * setup sequence for all OS services.
 */

#include <gracht/link/vali.h>
#include <gracht/server.h>
#include <internal/_tls.h>
#include <internal/_utils.h>
#include <ioset.h>
#include <os/usched/xunit.h>
#include <os/usched/job.h>
#include <stdlib.h>

extern void GetServiceAddress(IPCAddress_t*);
extern void ServiceInitialize(void);

static gracht_server_t*         g_server     = NULL;
static struct gracht_link_vali* g_serverLink = NULL;

extern void __crt_initialize(thread_storage_t* threadStorage, int isPhoenix);

int
__crt_get_server_iod(void)
{
    return gracht_link_get_handle((struct gracht_link*)g_serverLink);
}

gracht_server_t*
__crt_get_service_server(void)
{
    return g_server;
}

static void
__gracht_job(void* argument, void* cancellationToken)
{
    struct ioset_event            events[32];
    int                           num_events;
    gracht_server_configuration_t config;
    IPCAddress_t                  addr = { .Type = IPC_ADDRESS_PATH };
    int                           status;

    _CRT_UNUSED(argument);
    GetServiceAddress(&addr);

    // initialize the link
    status = gracht_link_vali_create(&g_serverLink);
    if (status) {
        exit(-2010);
    }
    gracht_link_vali_set_listen(g_serverLink, 1);
    gracht_link_vali_set_address(g_serverLink, &addr);

    // initialize configurations for server
    gracht_server_configuration_init(&config);
    gracht_server_configuration_set_aio_descriptor(&config, ioset(0));

    status = gracht_server_create(&config, &g_server);
    if (status) {
        exit(-2010 - status);
    }

    // after initializing the server we need to add the links we want to listen on
    status = gracht_server_add_link(g_server, (struct gracht_link*)g_serverLink);
    if (status) {
        exit(-2020 - status);
    }

    // listen to client events as well
    ioset_ctrl(
            config.set_descriptor, IOSET_ADD,
            gracht_client_iod(GetGrachtClient()),
            &(struct ioset_event) {
                .data.iod = gracht_client_iod(GetGrachtClient()),
                .events   = IOSETIN | IOSETCTL | IOSETLVT
            }
    );

    while (usched_is_cancelled(cancellationToken) == false) {
        num_events = ioset_wait(config.set_descriptor, &events[0], 32, 0);
        for (int i = 0; i < num_events; i++) {
            if (events[i].data.iod == gracht_client_iod(GetGrachtClient())) {
                gracht_client_wait_message(GetGrachtClient(), NULL, 0);
            }
            else {
                gracht_server_handle_event(g_server, events[i].data.iod, events[i].events);
            }
        }
    }
}

static void __crt_service_init(void)
{
    // Initialize the userspace scheduler to support request based
    // services in an async matter, and do this before we call ServiceInitialize
    // as the service might queue tasks up on load.
    usched_xunit_init();

    // Queue the gracht job up ot allow for server initialization first. This should be
    // queued before the main initializer, as that will most likely register gracht protocols,
    // and that needs gracht server and client to be setup
    usched_job_queue(__gracht_job, NULL);

    // Enter the eternal program loop. This will execute jobs untill exit() is called.
    // We use ServiceInitialize as the programs main function, we expect it to return relatively quick
    // and should be only used to initialize service subsystems.
    usched_xunit_main_loop((usched_task_fn)ServiceInitialize, NULL);
}

void __CrtServiceEntry(void)
{
    thread_storage_t threadStorage;
    __crt_initialize(&threadStorage, 0);
    __crt_service_init();
}

void __phoenix_main(void)
{
    thread_storage_t threadStorage;
    __crt_initialize(&threadStorage, 1);
    __crt_service_init();
}
