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
#include <gracht/link/vali.h>
#include <gracht/server.h>
#include <os/process.h>
#include <stdio.h>
#include <string.h>

#include <test_utils_protocol_server.h>

static void test_utils_print_callback(struct gracht_recv_message* message, struct test_utils_print_args* args)
{
    printf("received message: %s\n", args->message);
    test_utils_print_response(message, strlen(args->message));
}

static gracht_protocol_function_t test_utils_callbacks[1] = {
    { PROTOCOL_TEST_UTILS_PRINT_ID , test_utils_print_callback },
};
DEFINE_TEST_UTILS_SERVER_PROTOCOL(test_utils_callbacks, 1);

int main(int argc, char **argv)
{
    struct socket_server_configuration linkConfiguration;
    struct gracht_server_configuration serverConfiguration = { 0 };
    int                                code;
    UUId_t                             processId;
    
    gracht_os_get_server_client_address(&linkConfiguration.server_address, &linkConfiguration.server_address_length);
    gracht_os_get_server_packet_address(&linkConfiguration.dgram_address, &linkConfiguration.dgram_address_length);
    gracht_link_socket_server_create(&serverConfiguration.link, &linkConfiguration);
    
    code = gracht_server_initialize(&serverConfiguration);
    if (code) {
        printf("error initializing server library %i", errno);
        return code;
    }
    
    gracht_server_register_protocol(&test_utils_server_protocol);
    ProcessSpawn("$bin/wmclient.app", NULL, &processId);
    return gracht_server_main_loop();
}
