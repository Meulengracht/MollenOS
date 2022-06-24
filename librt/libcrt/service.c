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
#include <internal/_utils.h>
#include <os/usched/usched.h>
#include "../libc/threads/tls.h"
#include <stdlib.h>
#include <ioset.h>

extern void       GetServiceAddress(struct ipmsg_addr*);
extern OsStatus_t OnLoad(void);
extern OsStatus_t OnUnload(void);

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

_Noreturn static void
__crt_service_main(int setIod)
{
    struct ioset_event events[32];
    int                timeout;
    int                num_events;

    while (1) {
        // handle tasks before going to sleep
        do {
            timeout = usched_yield();
        } while (timeout == 0);

        // convert INT_MAX result to infinite result
        if (timeout == INT_MAX) {
            timeout = 0;
        }

        // handle events
        num_events = ioset_wait(setIod, &events[0], 32, timeout);
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

static void
__crt_service_init(void)
{
    gracht_server_configuration_t config;
    struct ipmsg_addr             addr = { .type = IPMSG_ADDRESS_PATH };
    int                           status;
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
    ioset_ctrl(config.set_descriptor, IOSET_ADD,
               gracht_client_iod(GetGrachtClient()),
               &(struct ioset_event) {
                       .data.iod = gracht_client_iod(GetGrachtClient()),
                       .events   = IOSETIN | IOSETCTL | IOSETLVT
               });

    // Initialize the userspace scheduler to support request based
    // services in an async matter, and do this before we call OnLoad
    // as the service might queue tasks up on load.
    usched_init();

    // Call the driver load function
    // - This will be run once, before loop
    if (OnLoad() != OsOK) {
        exit(-2001);
    }

    atexit((void (*)(void))OnUnload);
    at_quick_exit((void (*)(void))OnUnload);
    __crt_service_main(config.set_descriptor);
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
