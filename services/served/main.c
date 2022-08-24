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

#include <os/process.h>
#include <internal/_ipc.h>

#include <chef_served_service_server.h>

extern gracht_server_t* __crt_get_service_server(void);

oserr_t OnUnload(void)
{
    return OsOK;
}

void GetServiceAddress(struct ipmsg_addr* address)
{
    address->type = IPMSG_ADDRESS_PATH;
    address->data.path = SERVICE_SERVED_PATH;
}

oserr_t
OnLoad(void)
{
    // Register supported interfaces
    gracht_server_register_protocol(__crt_get_service_server(), &chef_served_server_protocol);
    return OsOK;
}

void chef_served_install_invocation(struct gracht_message* message, const char* publisher, const char* path)
{

}

void chef_served_remove_invocation(struct gracht_message* message, const char* packageName)
{

}

void chef_served_info_invocation(struct gracht_message* message, const char* packageName)
{

}

void chef_served_listcount_invocation(struct gracht_message* message)
{

}

void chef_served_list_invocation(struct gracht_message* message)
{

}

void chef_served_get_command_invocation(struct gracht_message* message, const char* mountPath)
{

}
