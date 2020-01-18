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
#include "include/libwm_crc.h"
#include <string.h>
#include <stdlib.h>

#define __TRACE
#include <ddk/utils.h>

typedef struct wm_client {
    enum wm_client_type type;
    int                 initialized;
    int                 socket;
} wm_client_t;

static int send_stream(wm_client_t* client, wm_message_t* message, 
    void* arguments, size_t argument_length)
{
    intmax_t bytes_written = send(client->socket, (const void*)message, 
        sizeof(wm_message_t), MSG_WAITALL);
    if (bytes_written == sizeof(wm_message_t) && message->has_arg) {
        bytes_written += send(client->socket, (const void*)arguments,
            argument_length, MSG_WAITALL);
    }
    return bytes_written != (sizeof(wm_message_t) + argument_length);
}

static int send_packet(wm_client_t* client, wm_message_t* message, 
    void* arguments, size_t argument_length)
{
    struct iovec  iov[2] = { 
        { .iov_base = message,   .iov_len = sizeof(wm_message_t) },
        { .iov_base = arguments, .iov_len = argument_length }
    };
    struct msghdr msg = {
        .msg_name       = NULL,
        .msg_namelen    = 0,
        .msg_iov        = &iov[0],
        .msg_iovlen     = 2,
        .msg_control    = NULL,
        .msg_controllen = 0,
        .msg_flags      = 0
    };
    intmax_t bytes_written = sendmsg(client->socket, &msg, MSG_WAITALL);
    return bytes_written != (sizeof(wm_message_t) + argument_length);
}

static int get_message_reply(wm_client_t* client, void* return_buffer, 
    size_t return_length)
{
    TRACE("[get_message_reply]");
    intmax_t bytes_read = recv(client->socket, return_buffer, 
        return_length, MSG_WAITALL);
    TRACE("[get_message_reply] bytes read: %i == %" PRIuIN, bytes_read, return_length);
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
    
    if (argument_length) {
        message.crc = crc16_generate((const unsigned char*)arguments, argument_length);
    }
    
    switch (client->type) {
        case wm_client_stream_based: {
            status = send_stream(client, &message, arguments, argument_length);
        } break;
        case wm_client_packet_based: {
            status = send_packet(client, &message, arguments, argument_length);
        } break;
    }
    
    if (!status && message.has_ret) {
        status = get_message_reply(client, return_buffer, return_length);
    }
    return status;
}

static int create_stream_socket(wm_client_configuration_t* config)
{
    int fd = socket(AF_LOCAL, SOCK_STREAM, 0);
    if (fd == -1) {
        return -1;
    }
    
    int status = connect(fd, sstosa(&config->address), config->address_length);
    if (status) {
        close(fd);
        return status;
    }
    return fd;
}

static int create_packet_socket(wm_client_configuration_t* config)
{
    int fd = socket(AF_LOCAL, SOCK_DGRAM, 0);
    if (fd == -1) {
        return -1;
    }
    
    int status = connect(fd, sstosa(&config->address), config->address_length);
    if (status) {
        close(fd);
        return status;
    }
    return fd;
}

int wm_client_initialize(wm_client_configuration_t* config, wm_client_t** client_out)
{
    wm_client_t* client;
    
    client = (wm_client_t*)malloc(sizeof(wm_client_t));
    if (!client) {
        _set_errno(ENOMEM);
        return -1;
    }
    
    client->type = config->type;
    switch (config->type) {
        case wm_client_stream_based: {
            client->socket = create_stream_socket(config);
        } break;
        case wm_client_packet_based: {
            client->socket = create_packet_socket(config);
        } break;
    }
    
    if (client->socket == -1) {
        return -1;
    }
    
    *client_out = client;
    return 0;
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

