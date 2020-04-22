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
#include <gracht/os.h>
#include <stdio.h>
#include <string.h>
#include "../test_utils_protocol_server.h"
#include <sys/un.h>

static const char* dgramPath = "/dev/gracht/dgram";
static const char* clientsPath = "/dev/gracht/clients";

void test_utils_print_callback(struct gracht_recv_message* message, struct test_utils_print_args* args)
{
    printf("received message: %s\n", args->message);
    test_utils_print_response(message, strlen(args->message));
}

int main(int argc, char **argv)
{
    struct socket_server_configuration linkConfiguration;
    struct gracht_server_configuration serverConfiguration;
    int                                code;
    
    struct sockaddr_un* dgramAddr = (struct sockaddr_un*)&linkConfiguration.dgram_address;
    struct sockaddr_un* serverAddr = (struct sockaddr_un*)&linkConfiguration.server_address;
    
    linkConfiguration.dgram_address_length = sizeof(struct sockaddr_un);
    linkConfiguration.server_address_length = sizeof(struct sockaddr_un);
    
    // Setup path for dgram
    dgramAddr->sun_family = AF_LOCAL;
    strncpy (dgramAddr->sun_path, dgramPath, sizeof(dgramAddr->sun_path));
    dgramAddr->sun_path[sizeof(dgramAddr->sun_path) - 1] = '\0';
    
    // Setup path for serverAddr
    serverAddr->sun_family = AF_LOCAL;
    strncpy (serverAddr->sun_path, clientsPath, sizeof(serverAddr->sun_path));
    serverAddr->sun_path[sizeof(serverAddr->sun_path) - 1] = '\0';
    
    gracht_link_socket_server_create(&serverConfiguration.link, &linkConfiguration);
    code = gracht_server_initialize(&serverConfiguration);
    if (code) {
        printf("error initializing server library %i", errno);
        return code;
    }
    
    gracht_server_register_protocol(&test_utils_protocol);
    return gracht_server_main_loop();
}
