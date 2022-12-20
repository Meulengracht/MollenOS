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

#include <internal/_ipc.h>
#include <os/usched/job.h>
#include <process.h>

#include <sys_library_service_server.h>
#include <sys_process_service_server.h>

extern gracht_server_t* __crt_get_service_server(void);

void GetServiceAddress(IPCAddress_t* address)
{
    address->Type = IPC_ADDRESS_PATH;
    address->Data.Path = SERVICE_PROCESS_PATH;
}

void ServiceInitialize(void)
{
    // Register supported interfaces
    gracht_server_register_protocol(__crt_get_service_server(), &sys_library_server_protocol);
    gracht_server_register_protocol(__crt_get_service_server(), &sys_process_server_protocol);

    // Initialize all our subsystems for Phoenix
    PmInitialize();
    PmDebuggerInitialize();
    PECacheInitialize();

    // Queue up the bootstrap function
    usched_job_queue(PmBootstrap, NULL);
}
