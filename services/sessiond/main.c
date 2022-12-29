/**
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Session Manager
 * - Contains the implementation of the session-manager which keeps track
 *   of all users and their running applications.
 */
#define __TRACE

#include <ddk/service.h>
#include <ddk/utils.h>
#include <gracht/link/vali.h>
#include <internal/_utils.h>

#include <sys_session_service_server.h>

void ServiceInitialize(
        _In_ struct ServiceStartupOptions* startupOptions)
{
    // Register supported interfaces
    gracht_server_register_protocol(startupOptions->Server, &sys_session_server_protocol);
}

void sys_session_login_invocation(struct gracht_message* message, const char* user, const char* password)
{
    // if error give a fake delay of 1 << min(attempt_num, 31) if the first 5 attempts are wrong
    // reset on login_success
    // int svc_session_login_response(struct gracht_recv_message* message, oserr_t status, char* session_id);
}

void sys_session_logout_invocation(struct gracht_message* message, const char* sessionId)
{
    // int svc_session_logout_response(struct gracht_recv_message* message, oserr_t status);
}

void sys_session_info_invocation(struct gracht_message* message, const char* sessionId)
{

}
