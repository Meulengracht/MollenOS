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

extern void     GetServiceAddress(struct ipmsg_addr*);
extern oserr_t OnLoad(void);
extern oserr_t OnUnload(void);

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

struct __gracht_job_context {
    int set_iod;
};

static void __gracht_job(void* argument, void* cancellationToken)
{
    struct __gracht_job_context* context = argument;
    struct ioset_event           events[32];
    int                          num_events;

    while (usched_is_cancelled(cancellationToken) == false) {
        num_events = ioset_wait(context->set_iod, &events[0], 32, 0);
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
    struct __gracht_job_context   context;
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
    usched_xunit_init();

    // Queue up the __gracht_job as a detached job, as it does not run in
    // green-thread context. So any blocking that makes will stall the execution
    // unit. To counter this we run it isolated
    struct usched_job_parameters jobParameters;
    usched_job_parameters_init(&jobParameters);
    usched_job_parameters_set_detached(&jobParameters, true);

    context.set_iod = config.set_descriptor;

    usched_job_queue3(__gracht_job, &context, &jobParameters);

    // Now queue up the on-unload functions that should be called before exit of service.
    atexit((void (*)(void))OnUnload);
    at_quick_exit((void (*)(void))OnUnload);

    // Enter the eternal program loop. This will execute jobs untill exit() is called.
    // We use OnLoad as the programs main function, we expect it to return relatively quick
    // and should be only used to initialize service subsystems.
    usched_xunit_main_loop((usched_task_fn)OnLoad, NULL);
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
