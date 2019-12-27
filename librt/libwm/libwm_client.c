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
#include <io.h>
#include "include/libwm_client.h"
#include "include/libwm_os.h"
#include <string.h>
#include <stdlib.h>

typedef struct wm_client {
    int initialized;
    int socket;
} wm_client_t;

static int send_message(wm_client_t* client, wm_message_t* message, 
    void* arguments, size_t argument_length)
{
    intmax_t bytes_written = send(client->socket, (const void*)message, 
        sizeof(wm_message_t), MSG_WAITALL);
    if (bytes_written == sizeof(wm_message_t) && message->has_arg) {
        bytes_written += send(client->socket, (const void*)arguments,
            argument_length, MSG_WAITALL);
    }
    return bytes_written != (message->length + 1);
}

static int get_message_reply(wm_client_t* client, void* return_buffer, 
    size_t return_length)
{
    intmax_t bytes_read = recv(client->socket, return_buffer, 
        return_length, MSG_WAITALL);
    return bytes_read != return_length;
}

int wm_client_invoke(wm_client_t* client, uint8_t protocol, uint8_t action, 
    void* arguments, size_t argument_length, void* return_buffer, 
    size_t return_length)
{
    int          status;
    wm_message_t message = { 
        .magic    = WM_HEADER_MAGIC,
        .length   = (sizeof(wm_message_t) + argument_length) - 1,
        .ret_length = return_length - 1,
        .has_arg  = (argument_length != 0) ? 1 : 0,
        .has_ret  = (return_length != 0) ? 1 : 0,
        .unused   = 0,
        .crc      = 0,
        .protocol = protocol,
        .action   = action
    };
    
    // TODO calc crc
    
    status = send_message(client, &message, arguments, argument_length);
    if (!status && message.has_ret) {
        status = get_message_reply(client, return_buffer, return_length);
    }
    return status;
}

int wm_client_initialize(wm_client_configuration_t* config, wm_client_t** client_out)
{
    wm_client_t* client;
    int          status;
    
    client = (wm_client_t*)malloc(sizeof(wm_client_t));
    if (!client) {
        _set_errno(ENOMEM);
        return -1;
    }
    
    client->socket = socket(AF_LOCAL, SOCK_STREAM, 0);
    if (client->socket == -1) {
        free(client);
        return -1;
    }
    
    status = connect(client->socket, sstosa(&config->address), config->address_length);
    if (status) {
        close(client->socket);
        free(client);
        return status;
    }
    
    *client_out = client;
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

