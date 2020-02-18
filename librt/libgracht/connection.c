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
 * Gracht Connection Type Definitions & Structures
 * - This header describes the base connection-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <assert.h>
#include <inet/socket.h>
#include <io.h>
#include "include/gracht/connection.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#define __TRACE
#include <ddk/utils.h>

typedef struct gracht_connection {
    int                     c_socket;
    struct sockaddr_storage address;
    int                     address_length;
    int                     alive;
    struct gracht_connection*   link;
} gracht_connection_t;

static gracht_connection_t* connections;
static mtx_t            connections_sync;

static gracht_connection_t* gracht_connection_to_struct(int sock)
{
    gracht_connection_t* conn;
    
    mtx_lock(&connections_sync);
    conn = connections;
    while (conn) {
        if (conn->c_socket == sock) {
            mtx_unlock(&connections_sync);
            return conn;
        }
        conn = conn->link;
    }
    mtx_unlock(&connections_sync);
    return NULL;
}

static void gracht_connection_add(gracht_connection_t* connection)
{
    mtx_lock(&connections_sync);
    if (!connections) {
        connections = connection;
    }
    else {
        gracht_connection_t* conn = connections;
        while (conn->link) {
            conn = conn->link;
        }
        conn->link = connection;
    }
    mtx_unlock(&connections_sync);
}

static void gracht_connection_remove(gracht_connection_t* connection)
{
    mtx_lock(&connections_sync);
    if (connections == connection) {
        connections = connection->link;
    }
    else {
        gracht_connection_t* conn = connections;
        while (conn->link != connection) {
            conn = conn->link;
        }
        conn->link = connection->link;
    }
    mtx_unlock(&connections_sync);
}

int gracht_connection_recv_packet(int socket, gracht_message_t* message, void* argument_buffer, struct sockaddr_storage* client_address)
{
    TRACE("[gracht_connection_recv_packet] %i, 0x%" PRIxIN, socket, message);
    struct iovec  iov[2] = { 
        { .iov_base = message,         .iov_len = sizeof(gracht_message_t) },
        { .iov_base = argument_buffer, .iov_len = GRACHT_MAX_MESSAGE_SIZE }
    };
    struct msghdr msg = {
        .msg_name       = client_address,
        .msg_namelen    = sizeof(struct sockaddr_storage),
        .msg_iov        = &iov[0],
        .msg_iovlen     = 2,
        .msg_control    = NULL,
        .msg_controllen = 0,
        .msg_flags      = 0
    };
    size_t message_length;
    
    // Packets are atomic, iether the full packet is there, or none is. So avoid
    // the use of MSG_WAITALL here.
    intmax_t bytes_read = recvmsg(socket, &msg, MSG_DONTWAIT);
    if (bytes_read < sizeof(gracht_message_t)) {
        if (bytes_read == 0) {
            _set_errno(ENODATA);
        }
        else {
            _set_errno(EPIPE);
        }
        return -1;
    }
    
    message_length = message->length;
    if (message_length != bytes_read) {
        _set_errno(EPIPE);
        ERROR("[gracht_connection_recv_packet] or message bytes were invalid %u != %u",
            message_length, bytes_read);
        return -1;
    }
    
    // verify crc TODO
    return 0;
}

int gracht_connection_recv_stream(int socket, gracht_message_t* message, void* argument_buffer)
{
    intmax_t bytes_read;
    size_t   message_length;
    TRACE("[gracht_connection_recv_stream] %i, 0x%" PRIxIN, socket, message);
    
    // Do not perform wait all here
    TRACE("[gracht_connection_recv_stream] reading message header");
    bytes_read = recv(socket, message, sizeof(gracht_message_t), MSG_DONTWAIT);
    if (bytes_read != sizeof(gracht_message_t)) {
        if (bytes_read == 0) {
            _set_errno(ENODATA);
        }
        else {
            _set_errno(EPIPE);
        }
        return -1;
    }
    
    message_length = message->length;
    
    // Verify the data read in the header
    if (message_length < sizeof(gracht_message_t)) {
        ERROR("[gracht_connection_recv_message] or message bytes were invalid %u != %u",
            message_length, sizeof(gracht_message_t));
        _set_errno(EPIPE);
        return -1;
    }
    
    // Read rest of message
    if (message_length > sizeof(gracht_message_t)) {
        assert(message_length <= GRACHT_MAX_MESSAGE_SIZE);
        assert(argument_buffer != NULL);
        
        TRACE("[gracht_connection_recv_stream] reading message payload");
        bytes_read = recv(socket, argument_buffer, 
            message_length - sizeof(gracht_message_t), MSG_WAITALL);
        if (bytes_read != message_length - sizeof(gracht_message_t)) {
            // do not process incomplete requests
            // TODO error code / handling
            ERROR("[gracht_connection_recv_message] did not read full amount of bytes (%" 
                PRIuIN ", expected %" PRIuIN ")",
                bytes_read, message_length - sizeof(gracht_message_t));
            _set_errno(EPIPE);
            return -1; 
        }
    }
    return 0;
}

int gracht_connection_send_stream(int socket, gracht_message_t* message, 
    void* arguments, size_t argument_length)
{
    intmax_t bytes_written = send(socket, (const void*)message, 
        sizeof(gracht_message_t), MSG_WAITALL);
    if (bytes_written == sizeof(gracht_message_t) && message->length > sizeof(gracht_message_t)) {
        bytes_written += send(socket, (const void*)arguments,
            argument_length, MSG_WAITALL);
    }
    return bytes_written != (sizeof(gracht_message_t) + argument_length);
}

int gracht_connection_send_packet(int socket, gracht_message_t* message, 
    void* arguments, size_t argument_length, struct sockaddr_storage* client_address)
{
    struct iovec  iov[2] = { 
        { .iov_base = message,   .iov_len = sizeof(gracht_message_t) },
        { .iov_base = arguments, .iov_len = argument_length }
    };
    struct msghdr msg = {
        .msg_name       = (struct sockaddr*)client_address,
        .msg_namelen    = client_address != NULL ? client_address->__ss_len : 0,
        .msg_iov        = &iov[0],
        .msg_iovlen     = 2,
        .msg_control    = NULL,
        .msg_controllen = 0,
        .msg_flags      = 0
    };
    intmax_t bytes_written = sendmsg(socket, &msg, MSG_WAITALL);
    return bytes_written != (sizeof(gracht_message_t) + argument_length);
}

int gracht_connection_send_reply(int socket, void* argument_buffer,
    size_t length, struct sockaddr_storage* client_address)
{
    TRACE("[gracht_connection_send_reply] %i, %" PRIuIN, socket, length);
    socklen_t address_length = client_address != NULL ? client_address->__ss_len : 0;
    intmax_t  bytes_written  = sendto(socket, argument_buffer, length, MSG_WAITALL,
        (const struct sockaddr*)sstosa(client_address), address_length);
    if (bytes_written <= 0) {
        ERROR("[gracht_connection_send_reply] send returned -1, error %i", errno);
    }
    TRACE("[gracht_connection_send_reply] bytes sent = %i", bytes_written);
    return (bytes_written != length); // return 0 on ok
}

int gracht_connection_broadcast_message(gracht_message_t* message,
    void* arguments, size_t argument_length)
{
    gracht_connection_t* conn;
    
    mtx_lock(&connections_sync);
    conn = connections;
    while (conn) {
        gracht_connection_send_stream(conn->c_socket, message, arguments, argument_length);
        conn = conn->link;
    }
    mtx_unlock(&connections_sync);
    return 0;
}

int gracht_connection_initialize(void)
{
    mtx_init(&connections_sync, mtx_plain);
    connections = NULL;
    return 0;
}

int gracht_connection_create(int socket, struct sockaddr_storage* address, int address_length)
{
    gracht_connection_t* connection;
    
    connection = (gracht_connection_t*)malloc(sizeof(gracht_connection_t));
    if (!connection) {
        _set_errno(ENOMEM);
        return -1;
    }
    
    memset(connection, 0, sizeof(gracht_connection_t));
    memcpy(&connection->address, address, address_length);
    connection->c_socket       = socket;
    connection->address_length = address_length;
    connection->alive          = 1;
    connection->link           = NULL;
    gracht_connection_add(connection);
    return 0;
}

int gracht_connection_shutdown(int socket)
{
    // get connetion from int
    gracht_connection_t* connection = gracht_connection_to_struct(socket);
    if (!connection) {
        _set_errno(EBADF);
        return -1;
    }
    
    gracht_connection_remove(connection);
    close(connection->c_socket);
    free(connection);
    return EOK;
}
