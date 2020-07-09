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

#include <svc_library_protocol_server.h>

extern void svc_library_load_callback(struct gracht_recv_message* message, struct svc_library_load_args*);
extern void svc_library_get_function_callback(struct gracht_recv_message* message, struct svc_library_get_function_args*);
extern void svc_library_unload_callback(struct gracht_recv_message* message, struct svc_library_unload_args*);

static gracht_protocol_function_t svc_library_callbacks[3] = {
    { PROTOCOL_SVC_LIBRARY_LOAD_ID , svc_library_load_callback },
    { PROTOCOL_SVC_LIBRARY_GET_FUNCTION_ID , svc_library_get_function_callback },
    { PROTOCOL_SVC_LIBRARY_UNLOAD_ID , svc_library_unload_callback },
};
DEFINE_SVC_LIBRARY_SERVER_PROTOCOL(svc_library_callbacks, 3);

#include <svc_process_protocol_server.h>

extern void svc_process_spawn_callback(struct gracht_recv_message* message, struct svc_process_spawn_args*);
extern void svc_process_join_callback(struct gracht_recv_message* message, struct svc_process_join_args*);
extern void svc_process_kill_callback(struct gracht_recv_message* message, struct svc_process_kill_args*);
extern void svc_process_get_tick_base_callback(struct gracht_recv_message* message, struct svc_process_get_tick_base_args*);
extern void svc_process_get_startup_information_callback(struct gracht_recv_message* message, struct svc_process_get_startup_information_args*);
extern void svc_process_get_modules_callback(struct gracht_recv_message* message, struct svc_process_get_modules_args*);
extern void svc_process_get_name_callback(struct gracht_recv_message* message, struct svc_process_get_name_args*);
extern void svc_process_get_assembly_directory_callback(struct gracht_recv_message* message, struct svc_process_get_assembly_directory_args*);
extern void svc_process_get_working_directory_callback(struct gracht_recv_message* message, struct svc_process_get_working_directory_args*);
extern void svc_process_set_working_directory_callback(struct gracht_recv_message* message, struct svc_process_set_working_directory_args*);
extern void svc_process_terminate_callback(struct gracht_recv_message* message, struct svc_process_terminate_args*);
extern void svc_process_report_crash_callback(struct gracht_recv_message* message, struct svc_process_report_crash_args*);

static gracht_protocol_function_t svc_process_callbacks[12] = {
    { PROTOCOL_SVC_PROCESS_SPAWN_ID , svc_process_spawn_callback },
    { PROTOCOL_SVC_PROCESS_JOIN_ID , svc_process_join_callback },
    { PROTOCOL_SVC_PROCESS_KILL_ID , svc_process_kill_callback },
    { PROTOCOL_SVC_PROCESS_GET_TICK_BASE_ID , svc_process_get_tick_base_callback },
    { PROTOCOL_SVC_PROCESS_GET_STARTUP_INFORMATION_ID , svc_process_get_startup_information_callback },
    { PROTOCOL_SVC_PROCESS_GET_MODULES_ID , svc_process_get_modules_callback },
    { PROTOCOL_SVC_PROCESS_GET_NAME_ID , svc_process_get_name_callback },
    { PROTOCOL_SVC_PROCESS_GET_ASSEMBLY_DIRECTORY_ID , svc_process_get_assembly_directory_callback },
    { PROTOCOL_SVC_PROCESS_GET_WORKING_DIRECTORY_ID , svc_process_get_working_directory_callback },
    { PROTOCOL_SVC_PROCESS_SET_WORKING_DIRECTORY_ID , svc_process_set_working_directory_callback },
    { PROTOCOL_SVC_PROCESS_TERMINATE_ID , svc_process_terminate_callback },
    { PROTOCOL_SVC_PROCESS_REPORT_CRASH_ID , svc_process_report_crash_callback },
};
DEFINE_SVC_PROCESS_SERVER_PROTOCOL(svc_process_callbacks, 12);

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
    gracht_server_register_protocol(&svc_library_server_protocol);
    gracht_server_register_protocol(&svc_process_server_protocol);
    return InitializeProcessManager();
}
