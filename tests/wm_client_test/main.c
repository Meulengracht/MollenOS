/**
 * MollenOS
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
 * WM Server test
 *  - Spawns a minimal implementation of a wm server to test libwm and the
 *    communication protocols in the os
 */

#include <gracht/link/socket.h>
#include <gracht/client.h>
#include <gracht/os.h>
#include "test_utils_protocol_client.h"
#include <stdio.h>

int main(int argc, char **argv)
{
    struct socket_client_configuration linkConfiguration;
    struct gracht_client_configuration clientConfiguration;
    gracht_client_t*                   client;
    int                                code, status;
    
    linkConfiguration.type = gracht_link_packet_based;
    gracht_os_get_server_packet_address(&linkConfiguration.address, &linkConfiguration.address_length);
    
    //linkConfiguration.type = gracht_link_stream_based;
    //gracht_os_get_server_client_address(&linkConfiguration.address, &linkConfiguration.address_length);
    gracht_link_socket_client_create(&clientConfiguration.link, &linkConfiguration);
    
    code = gracht_client_create(&clientConfiguration, &client);
    if (code) {
        return code;
    }
    
    code = test_utils_print_sync(client, NULL, "hello from wm_client!", &status);
    return gracht_client_shutdown(client);
}
