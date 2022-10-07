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
 */

#include <ddk/service.h>
#include <ddk/utils.h>
#include <gracht/link/vali.h>
#include <gracht/server.h>
#include <internal/_tls.h>
#include <internal/_utils.h>
#include <stdlib.h>
#include <string.h>
#include <ioset.h>

#include <sys_device_service_client.h>

// Module Interface
extern oserr_t OnLoad(void);
extern oserr_t OnUnload(void);
extern oserr_t OnEvent(struct ioset_event* event);

static gracht_server_t*         g_server     = NULL;
static struct gracht_link_vali* g_serverLink = NULL;

extern void   __crt_initialize(thread_storage_t* threadStorage, int isPhoenix);
extern char** __crt_argv(int* argcOut);

int __crt_get_server_iod(void)
{
    return gracht_link_get_handle((struct gracht_link*)g_serverLink);
}

gracht_server_t* __crt_get_module_server(void)
{
    return g_server;
}

void __crt_module_load(
        _In_ uuid_t moduleId)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetDeviceService());

    // Call the driver load function 
    // - This will be run once, before loop
    if (OnLoad() != OsOK) {
        exit(-2001);
    }

    // Notify the devicemanager of our successful startup
    sys_device_notify(
            GetGrachtClient(),
            &msg.base,
            moduleId,
            GetNativeHandle(__crt_get_server_iod())
    );
}

_Noreturn static void
__crt_module_main(
        _In_ int setIod)
{
    struct ioset_event events[32];

    while (1) {
        int num_events = ioset_wait(setIod, &events[0], 32, 0);
        for (int i = 0; i < num_events; i++) {
            if (events[i].data.iod == gracht_client_iod(GetGrachtClient())) {
                gracht_client_wait_message(GetGrachtClient(), NULL, 0);
            }
            else {
                // Check if the driver had any IRQs registered
                if (OnEvent(&events[i]) == OsOK) {
                    continue;
                }
                gracht_server_handle_event(g_server, events[i].data.iod, events[i].events);
            }
        }
    }
}

static oserr_t
__ParseModuleOptions(
        _In_  char**  argv,
        _In_  int     argc,
        _Out_ uuid_t* moduleIdOut)
{
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--id") && (i + 1) < argc) {
            long moduleId = strtol(argv[i + 1], NULL, 10);
            if (moduleId != 0) {
                *moduleIdOut = (uuid_t)moduleId;
                return OsOK;
            }
        }
    }
    return OsNotExists;
}

void __CrtModuleEntry(void)
{
    thread_storage_t              threadStorage;
    gracht_server_configuration_t config;
    IPCAddress_t                  addr = { .Type = IPC_ADDRESS_HANDLE };
    oserr_t                       oserr;
    uuid_t                        moduleId;
    int                           status;
    char**                        argv;
    int                           argc;

    // initialize runtime environment
    __crt_initialize(&threadStorage, 0);
    argv = __crt_argv(&argc);
    if (!argv || argc < 3) {
        ERROR("__CrtModuleEntry invalid argument count for module");
        exit(-1);
    }

    // parse the options provided for this module
    oserr = __ParseModuleOptions(argv, argc, &moduleId);
    if (oserr != OsOK) {
        ERROR("__CrtModuleEntry missing --id parameter for module");
        exit(-1);
    }

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

    // Wait for the device-manager service, as all modules require the device-manager
    // service to perform requests.
    if (WaitForDeviceService(2000) != OsOK) {
        exit(-2002);
    }

    __crt_module_load(moduleId);
    atexit((void (*)(void))OnUnload);
    at_quick_exit((void (*)(void))OnUnload);
    __crt_module_main(config.set_descriptor);
}
