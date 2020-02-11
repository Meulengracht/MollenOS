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
 * WM Protocol Test
 *  - Spawns a minimal implementation of a wm server to test libwm and the
 *    communication protocols in the os
 */

#include "test_protocol.h"
#include "libwm_client.h"
#include <string.h>

int test_print_sync(wm_client_t* client, char* message, int* status)
{
    struct test_print_arg args;
    struct test_print_ret rets;
    int                   wm_status;
    
    memcpy(&args.message[0], message, strlen(message) + 1);
    wm_status = wm_client_invoke(client, /* config, */
        PROTOCOL_TEST_ID, PROTOCOL_TEST_PRINT_ID,
        &args, sizeof(struct test_print_arg),  // arguments
        &rets, sizeof(struct test_print_ret)); // return
    if (!wm_status) {
        *status = rets.status;
    }
    return wm_status;
}
