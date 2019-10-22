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
 * Wm Client Type Definitions & Structures
 * - This header describes the base client-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <assert.h>
#include <inet/socket.h>
#include "include/libwm_client.h"
#include "include/libwm_os.h"
#include <string.h>

typedef struct wm_client {
    int initialized;
    int socket;
} wm_client_t;

static int wm_execute_command(wm_request_header_t* command)
{
    intmax_t bytes_written;
    assert(wm_initialized == 1);
    
    bytes_written = send(wm_socket, (const void*)command, 
        command->length, MSG_WAITALL);
    return bytes_written != command->length;
}

int wm_client_invoke(wm_client_t* client, uint8_t protocol, uint8_t action, 
    void* arguments, size_t argument_length, void* returns, size_t return_length)
{
    wm_message_t message = { 
        .magic    = WM_HEADER_MAGIC,
        .length   = (sizeof(wm_message_t) + argument_length) - 1,
        .has_arg  = (argument_length != 0) ? 1 : 0,
        .has_ret  = (return_length != 0) ? 1 : 0,
        .unused   = 0,
        .crc      = 0,
        .protocol = protocol,
        .action   = action
    };
    
    // send_message
    
    if (message.has_ret) {
        // get_reply
    }
    return 0;
}

int wm_client_initialize(wm_client_t** client_out)
{
    struct sockaddr_storage wm_address;
    socklen_t               wm_address_length;
    int                     status;
    
    // Create a new socket for listening to wm events. They are all
    // delivered to fixed sockets on the local system.
    wm_socket = socket(AF_LOCAL, SOCK_STREAM, 0);
    assert(wm_socket >= 0);
    
    // Connect to the compositor
    wm_os_get_server_address(&wm_address, &wm_address_length);
    status = connect(wm_socket, sstosa(&wm_address), wm_address_length);
    assert(status >= 0);
    
    return status;
}

int wm_client_shutdown(wm_client_t* client)
{
    if (!client) {
        _set_errno(EINVAL);
        return -1;
    }
    close(client->socket);
    free(client);
    return 0;
}

