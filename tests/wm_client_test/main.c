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

#include <libwm_client.h>
#include <libwm_os.h>
#include "../wm_server_test/test_protocol.h"
#include <stdio.h>
#include "test_protocol_client.h"

int main(int argc, char **argv)
{
    wm_client_configuration_t configuration;
    wm_client_t*              client;
    int                       code, status;
    
    wm_os_get_server_address(&configuration.address, &configuration.address_length);
    
    code = wm_client_initialize(&configuration, &client);
    if (code) {
        return code;
    }
    
    code = test_print(client, "hello from wm_client!", &status);
    return wm_client_shutdown(client);
}
