/* MollenOS
 *
 * Copyright 2019, Philip Meulengracht
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
 * Process Manager
 * - Contains the implementation of the process-manager which keeps track
 *   of running applications.
 */
#define __TRACE

#include <ds/mstring.h>
#include <os/process.h>
#include <ddk/utils.h>
#include <internal/_ipc.h>
#include <string.h>
#include <stdio.h>
#include "process.h"

#include "svc_library_protocol_server.h"
#include "svc_process_protocol_server.h"

OsStatus_t OnUnload(void)
{
    return OsSuccess;
}

void GetServiceAddress(struct ipmsg_addr* address)
{
    address->type = IPMSG_ADDRESS_PATH;
    address->data.path = SERVICE_PROCESS_PATH;
}

OsStatus_t
OnLoad(void)
{
    // Register supported interfaces
    gracht_server_register_protocol(&svc_library_protocol);
    gracht_server_register_protocol(&svc_process_protocol);
    return InitializeProcessManager();
}
