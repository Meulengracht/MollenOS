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

#include <ddk/services/service.h>
#include <ddk/threadpool.h>
#include <ddk/utils.h>
#include <gracht/link/vali.h>
#include <gracht/server.h>
#include <os/mollenos.h>
#include "../libc/threads/tls.h"
#include <stdlib.h>

extern void       GetServiceAddress(struct ipmsg_addr*);
extern OsStatus_t OnLoad(void);
extern OsStatus_t OnUnload(void);

extern char**
__CrtInitialize(
    _In_  thread_storage_t* Tls,
    _In_  int               IsModule,
    _Out_ int*              ArgumentCount);

void __CrtServiceEntry(void)
{
    thread_storage_t              tls;
    gracht_server_configuration_t config;
    struct ipmsg_addr             addr = { .type = IPMSG_ADDRESS_HANDLE };
    int                           status;

    // Initialize environment
    __CrtInitialize(&tls, 1, NULL);

    GetServiceAddress(&addr);
    status = gracht_link_vali_server_create(&config.link, &addr);
    if (status) {
        exit(status);
    }
    
    status = gracht_server_initialize(&config);
    if (status) {
        exit(status);
    }

    // Call the driver load function 
    // - This will be run once, before loop
    if (OnLoad() != OsSuccess) {
        exit(-1);
    }
    
    status = gracht_server_main_loop();
    OnUnload();
    exit(-1);
}
