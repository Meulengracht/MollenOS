/* MollenOS
 *
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS C Library - Server Entry 
 */

#include <gracht/link/vali.h>
#include <gracht/server.h>
#include <internal/_utils.h>
#include "../libc/threads/tls.h"
#include <stdlib.h>
#include <ioset.h>

extern void       GetServiceAddress(struct ipmsg_addr*);
extern OsStatus_t OnLoad(void);
extern OsStatus_t OnUnload(void);

extern char**
__crt_init(
    _In_  thread_storage_t* threadStorage,
    _In_  int               isModule,
    _Out_ int*              argumentCount);

_Noreturn static void __crt_service_main(int setIod)
{
    struct ioset_event events[32];

    while (1) {
        int num_events = ioset_wait(setIod, &events[0], 32, 0);
        for (int i = 0; i < num_events; i++) {
            if (events[i].data.iod == gracht_client_iod(GetGrachtClient())) {
                gracht_client_wait_message(GetGrachtClient(), NULL, GetGrachtBuffer(), 0);
            }
            else {
                gracht_server_handle_event(events[i].data.iod, events[i].events);
            }
        }
    }
}

void __CrtServiceEntry(void)
{
    thread_storage_t              threadStorage;
    gracht_server_configuration_t config;
    struct ipmsg_addr             addr = { .type = IPMSG_ADDRESS_HANDLE };
    int                           status;

    __crt_init(&threadStorage, 1, NULL);

    GetServiceAddress(&addr);
    status = gracht_link_vali_server_create(&config.link, &addr);
    if (status) {
        exit(status);
    }

    config.set_descriptor          = ioset(0);
    config.set_descriptor_provided = 1;

    status = gracht_server_initialize(&config);
    if (status) {
        exit(status);
    }

    // listen to client events as well
    ioset_ctrl(config.set_descriptor, IOSET_ADD,
               gracht_client_iod(GetGrachtClient()),
               &(struct ioset_event) {
                       .data.iod = gracht_client_iod(GetGrachtClient()),
                       .events   = IOSETIN | IOSETCTL | IOSETLVT
               });

    // Call the driver load function
    // - This will be run once, before loop
    if (OnLoad() != OsSuccess) {
        exit(-1);
    }

    atexit((void (*)(void))OnUnload);
    at_quick_exit((void (*)(void))OnUnload);
    __crt_service_main(config.set_descriptor);
}
