/**
 * MollenOS
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
 * C Library - Driver Entry 
 */

#include "../libc/threads/tls.h"
#include <ddk/service.h>
#include <gracht/link/vali.h>
#include <gracht/server.h>
#include <internal/_ipc.h>
#include <internal/_utils.h>
#include <stdlib.h>
#include <ioset.h>

// Module Interface
extern void       GetModuleIdentifiers(unsigned int*, unsigned int*, unsigned int*, unsigned int*);
extern OsStatus_t OnLoad(void);
extern OsStatus_t OnUnload(void);
extern OsStatus_t OnEvent(struct ioset_event* event);

extern char**
__CrtInitialize(
    _In_  thread_storage_t* Tls,
    _In_  int               IsModule,
    _Out_ int*              ArgumentCount);

void __CrtModuleLoad(void)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetDeviceService());
    unsigned int             vendorId;
    unsigned int             deviceId;
    unsigned int             class;
    unsigned int             subClass;
    
    GetModuleIdentifiers(&vendorId, &deviceId, &class, &subClass);
    
    // Call the driver load function 
    // - This will be run once, before loop
    if (OnLoad() != OsSuccess) {
        exit(-1);
    }
    
    if (vendorId != 0 || deviceId != 0 || class != 0 || subClass != 0) {
        svc_device_notify(GetGrachtClient(), &msg.base,
            GetNativeHandle(gracht_server_get_dgram_iod()),
            vendorId, deviceId, class, subClass);
    }
}

static void __CrtModuleMainLoop(int setIod)
{
    struct ioset_event events[32];

    while (1) {
        int num_events = ioset_wait(setIod, &events[0], 32, 0);
        for (int i = 0; i < num_events; i++) {
            // Check if the driver had any IRQs registered
            if (OnEvent(&events[i]) == OsSuccess) {
                continue;
            }

            if (events[i].data.iod == gracht_client_iod(GetGrachtClient())) {
                gracht_client_wait_message(GetGrachtClient(), NULL, GetGrachtBuffer(), 0);
            }
            else {
                gracht_server_handle_event(events[i].data.iod, events[i].events);
            }
        }
    }
}

void __CrtModuleEntry(void)
{
    thread_storage_t              tls;
    gracht_server_configuration_t config;
    struct ipmsg_addr             addr = { .type = IPMSG_ADDRESS_HANDLE };
    int                           status;

    // Initialize environment
    __CrtInitialize(&tls, 1, NULL);

    // Wait for the device-manager service, as all modules require the device-manager
    // service to perform requests.
    if (WaitForDeviceService(2000) != OsSuccess) {
        exit(-1);
    }
    
    status = gracht_link_vali_server_create(&config.link, &addr);
    if (status) {
        exit(-1);
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

    __CrtModuleLoad();
    __CrtModuleMainLoop(config.set_descriptor);
    OnUnload();
    exit(0);
}
