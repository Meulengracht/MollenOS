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

#include <errno.h>
#include <gracht/link/socket.h>
#include <gracht/server.h>
#include <stdio.h>
#include <string.h>
#include <sys/un.h>

#include <test_utils_protocol_client.h>

static const char* dgramPath = "/tmp/g_dgram";
static const char* clientsPath = "/tmp/g_clients";
static char        messageBuffer[GRACHT_MAX_MESSAGE_SIZE];

int main(int argc, char **argv)
{
    struct socket_client_configuration linkConfiguration = { 0 };
    struct gracht_client_configuration clientConfiguration;
    gracht_client_t*                   client;
    int                                code, status = -1337;
    struct gracht_message_context      context;

    struct sockaddr_un* addr = (struct sockaddr_un*)&linkConfiguration.address;
    linkConfiguration.address_length = sizeof(struct sockaddr_un);

    //linkConfiguration.type = gracht_link_packet_based;
    linkConfiguration.type = gracht_link_stream_based;

    addr->sun_family = AF_LOCAL;
    //strncpy (addr->sun_path, dgramPath, sizeof(addr->sun_path));
    strncpy (addr->sun_path, clientsPath, sizeof(addr->sun_path));
    addr->sun_path[sizeof(addr->sun_path) - 1] = '\0';

    gracht_link_socket_client_create(&clientConfiguration.link, &linkConfiguration);
    code = gracht_client_create(&clientConfiguration, &client);
    if (code) {
        printf("gracht_client: error initializing client library %i, %i\n", errno, code);
        return code;
    }

    if (argc > 1) {
        code = test_utils_print(client, &context, argv[1]);
    }
    else {
        code = test_utils_print(client, &context, "hello from wm_client!");
    }

    gracht_client_wait_message(client, &context, &messageBuffer[0], GRACHT_WAIT_BLOCK);
    test_utils_print_result(client, &context, &status);
    
    printf("gracht_client: recieved status %i\n", status);
    return gracht_client_shutdown(client);
}
