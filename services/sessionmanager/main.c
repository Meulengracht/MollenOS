/* MollenOS
 *
 * Copyright 2018, Philip Meulengracht
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
 * Session Manager
 * - Contains the implementation of the session-manager which keeps track
 *   of all users and their running applications.
 */
#define __TRACE

#include <os/process.h>
#include <ddk/utils.h>
#include <internal/_ipc.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <svc_session_protocol_server.h>

extern void svc_session_login_callback(struct gracht_recv_message* message, struct svc_session_login_args*);
extern void svc_session_logout_callback(struct gracht_recv_message* message, struct svc_session_logout_args*);
extern void svc_session_new_device_callback(struct gracht_recv_message* message, struct svc_session_new_device_args*);

static gracht_protocol_function_t svc_session_callbacks[3] = {
    { PROTOCOL_SVC_SESSION_LOGIN_ID , svc_session_login_callback },
    { PROTOCOL_SVC_SESSION_LOGOUT_ID , svc_session_logout_callback },
    { PROTOCOL_SVC_SESSION_NEW_DEVICE_ID , svc_session_new_device_callback },
};
DEFINE_SVC_SESSION_SERVER_PROTOCOL(svc_session_callbacks, 3);

static UUId_t WindowingSystemId = UUID_INVALID;

OsStatus_t OnUnload(void)
{
    return OsSuccess;
}

void GetServiceAddress(struct ipmsg_addr* address)
{
    address->type = IPMSG_ADDRESS_PATH;
    address->data.path = SERVICE_SESSION_PATH;
}

OsStatus_t
OnLoad(void)
{
    // Register supported interfaces
    gracht_server_register_protocol(&svc_session_server_protocol);
    return OsSuccess;
}

void svc_session_login_callback(struct gracht_recv_message* message, struct svc_session_login_args* args)
{
    // if error give a fake delay of 1 << min(attempt_num, 31) if the first 5 attempts are wrong
    // reset on login_success
    // int svc_session_login_response(struct gracht_recv_message* message, OsStatus_t status, char* session_id);
}

void svc_session_logout_callback(struct gracht_recv_message* message, struct svc_session_logout_args* args)
{
    // int svc_session_logout_response(struct gracht_recv_message* message, OsStatus_t status);
}

void svc_session_new_device_callback(struct gracht_recv_message* message, struct svc_session_new_device_args* args)
{
    ProcessConfiguration_t config;
    char pathBuffer[64];
    
    if (WindowingSystemId == UUID_INVALID) {
        // Clear up buffer and spawn app
        memset(&pathBuffer[0], 0, sizeof(pathBuffer));
        sprintf(&pathBuffer[0], "%s:/shared/bin/" __OSCONFIG_INIT_APP, args->identifier);
        TRACE("Spawning %s", &pathBuffer[0]);
        ProcessConfigurationInitialize(&config);
        config.InheritFlags = PROCESS_INHERIT_STDOUT | PROCESS_INHERIT_STDERR;
        ProcessSpawnEx(&pathBuffer[0], NULL, &config, &WindowingSystemId);
    }
}
