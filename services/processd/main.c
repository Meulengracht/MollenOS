/**
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Process Manager
 * - Contains the implementation of the process-manager which keeps track
 *   of running applications.
 */
#define __TRACE

#include <ds/mstring.h>
#include <internal/_ipc.h>
#include "process.h"
#include <os/usched/usched.h>

#include <sys_library_service_server.h>
#include <sys_process_service_server.h>

extern gracht_server_t* __crt_get_service_server(void);

oserr_t
OnUnload(void)
{
    PmBootstrapCleanup();
    return OsOK;
}

void GetServiceAddress(struct ipmsg_addr* address)
{
    address->type = IPMSG_ADDRESS_PATH;
    address->data.path = SERVICE_PROCESS_PATH;
}

oserr_t
OnLoad(void)
{
    // Register supported interfaces
    gracht_server_register_protocol(__crt_get_service_server(), &sys_library_server_protocol);
    gracht_server_register_protocol(__crt_get_service_server(), &sys_process_server_protocol);

    // Initialize all our subsystems for Phoenix
    PmInitialize();
    PmDebuggerInitialize();

    // Queue up a task that bootstraps the system, it must run in scheduler
    // context as it uses scheduler primitives.
    usched_job_queue((usched_task_fn)PmBootstrap, NULL);
    return OsOK;
}
