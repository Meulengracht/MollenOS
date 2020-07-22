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
 * Gracht OS Type Definitions & Structures
 * - This header describes the base os-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <os/mollenos.h>
#include "gracht/link/vali.h"
#include <inet/socket.h>
#include <inet/local.h>
#include <string.h>

int gracht_os_get_server_client_address(struct sockaddr_storage* address, int* address_length_out)
{
    struct sockaddr_lc* local_address = sstolc(address);
    *address_length_out               = sizeof(struct sockaddr_lc);

    // Prepare the server address.
    memset(local_address, 0, sizeof(struct sockaddr_lc));
    memcpy(&local_address->slc_addr[0], LCADDR_WM0, strlen(LCADDR_WM0));
    local_address->slc_len    = sizeof(struct sockaddr_lc);
    local_address->slc_family = AF_LOCAL;
    return 0;
}

int gracht_os_get_server_packet_address(struct sockaddr_storage* address, int* address_length_out)
{
    struct sockaddr_lc* local_address = sstolc(address);
    *address_length_out               = sizeof(struct sockaddr_lc);

    // Prepare the server address. 
    memset(local_address, 0, sizeof(struct sockaddr_lc));
    memcpy(&local_address->slc_addr[0], LCADDR_WM1, strlen(LCADDR_WM1));
    local_address->slc_len    = sizeof(struct sockaddr_lc);
    local_address->slc_family = AF_LOCAL;
    return 0;
}

int gracht_os_thread_set_name(const char* thread_name)
{
    if (SetCurrentThreadName(thread_name) != OsSuccess) {
        return -1;
    }
    return 0;
}
