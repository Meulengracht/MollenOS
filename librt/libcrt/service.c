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
 * Service Initialization procedure. Contains entry point and environmental
 * setup sequence for all OS services.
 */

#define __TRACE

#include <ddk/service.h>
#include <ddk/utils.h>
#include <gracht/link/vali.h>
#include <gracht/server.h>
#include <internal/_tls.h>
#include <internal/_utils.h>
#include <ioset.h>
#include <os/usched/xunit.h>
#include <os/usched/job.h>
#include <stdlib.h>
#include <string.h>

struct __ServiceOptions {
    const char* Token;
    const char* APIPath;
};

extern void ServiceInitialize(struct ServiceStartupOptions*);

static gracht_server_t*         g_server     = NULL;
static struct gracht_link_vali* g_serverLink = NULL;

extern void   __crt_initialize(thread_storage_t* threadStorage, int isPhoenix);
extern char** __crt_argv(int* argcOut);

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
__ParseServiceOptions(
        _In_ char**                   argv,
        _In_ int                      argc,
        _In_ struct __ServiceOptions* options)
{
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--api-path") && (i + 1) < argc) {
            options->APIPath = argv[i + 1];
        } else if (!strcmp(argv[i], "--token") && (i + 1) < argc) {
            options->Token = argv[i + 1];
        }
    }
}

static void
__ParseStartupOptions(
        _In_ struct __ServiceOptions* options)
{
    char** argv;
    int    argc;

    argv = __crt_argv(&argc);
    if (argv == NULL) {
        ERROR("__ParseStartupOptions failed to parse arguments");
        exit(-1);
    }

    // parse the options provided for this module
    __ParseServiceOptions(argv, argc, options);

    // validate what has been passed
    if (options->APIPath == NULL) {
        ERROR("__ParseStartupOptions --api-path was not supplied, aborting");
        exit(-1);
    }
}

static void
__StartGrachtServer(
        _In_ void* argument,
        _In_ void* cancellationToken)
{
    struct __ServiceOptions       serviceOptions = { 0 };
    struct ioset_event            events[32];
    int                           num_events;
    gracht_server_configuration_t config;
    int                           status;
    bool                          phoenix = argument == (void*)0xDEADBEEF;

    // parse the startup options first, it is required for services to provide
    // the API path and security Token on boot.
    if (!phoenix) {
        __ParseStartupOptions(&serviceOptions);
    } else {
        serviceOptions.APIPath = SERVICE_PROCESS_PATH;
    }

    // initialize the link
    status = gracht_link_vali_create(&g_serverLink);
    if (status) {
        exit(-2010);
    }
    gracht_link_vali_set_listen(g_serverLink, 1);
    gracht_link_vali_set_address(
            g_serverLink,
            &(IPCAddress_t) {
                .Type = IPC_ADDRESS_PATH,
                .Data.Path = serviceOptions.APIPath
            }
    );

    // initialize configurations for server
    gracht_server_configuration_init(&config);
    gracht_server_configuration_set_aio_descriptor(&config, ioset(0));
    gracht_server_configuration_set_num_workers(&config, 2); // doesn't matter, just enables it

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
        num_events = ioset_wait(config.set_descriptor, &events[0], 32, NULL);
        for (int i = 0; i < num_events; i++) {
            if (events[i].data.iod == gracht_client_iod(GetGrachtClient())) {
                gracht_client_wait_message(GetGrachtClient(), NULL, 0);
            } else {
                gracht_server_handle_event(g_server, events[i].data.iod, events[i].events);
            }
        }
    }
}

static void
__ServiceMain(
        _In_ void* argument,
        _In_ void* cancellationToken)
{
    struct ServiceStartupOptions startupOptions;
    TRACE("__ServiceMain()");

    _CRT_UNUSED(argument);
    _CRT_UNUSED(cancellationToken);

    startupOptions.Server = g_server;
    startupOptions.ServerHandle = GetNativeHandle( gracht_link_get_handle((struct gracht_link*)g_serverLink));

    ServiceInitialize(&startupOptions);
}

static void __crt_service_init(bool isPhoenix)
{
    // Queue the gracht job up ot allow for server initialization first. This should be
    // queued before the main initializer, as that will most likely register gracht protocols,
    // and that needs gracht server and client to be setup
    usched_job_queue(__StartGrachtServer, isPhoenix ? (void*)0xDEADBEEF : NULL);

    // Enter the eternal program loop. This will execute jobs until exit() is called.
    // We use ServiceInitialize as the programs main function, we expect it to return relatively quick
    // and should be only used to initialize service subsystems.
    usched_xunit_main_loop(__ServiceMain, NULL);
}

void __CrtServiceEntry(void)
{
    thread_storage_t threadStorage;
    __crt_initialize(&threadStorage, false);
    __crt_service_init(false);
}

void __phoenix_main(void)
{
    thread_storage_t threadStorage;
    __crt_initialize(&threadStorage, true);
    __crt_service_init(true);
}
