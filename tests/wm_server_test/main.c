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
#include <gracht/server.h>
#include <gracht/os.h>
#include <os/services/process.h>
#include <stdio.h>
#include "test_utils_protocol_server.h"

static void test_utils_print_callback(int client, struct test_utils_print_args* args, struct test_utils_print_ret* ret)
{
    printf("received message: %s\n", &args->message[0]);
    ret->status = 0;
}

int main(int argc, char **argv)
{
    gracht_server_configuration_t configuration;
    int                           code;
    
    gracht_os_get_server_client_address(&configuration.server_address, &configuration.server_address_length);
    gracht_os_get_server_packet_address(&configuration.dgram_address, &configuration.dgram_address_length);
    code = gracht_server_initialize(&configuration);
    if (code) {
        printf("error initializing server library %i", errno);
        return code;
    }
    
    gracht_server_register_protocol(&test_utils_protocol);
    ProcessSpawn("$bin/wmclient.app", NULL);
    return gracht_server_main_loop();
}
