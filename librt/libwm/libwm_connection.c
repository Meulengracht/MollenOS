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
 * Wm Connection Type Definitions & Structures
 * - This header describes the base connection-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <assert.h>
#include <inet/socket.h>
#include <io.h>
#include "include/libwm_connection.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

typedef struct __wm_connection {
    int                     c_socket;
    struct sockaddr_storage address;
    int                     address_length;
    int                     alive;
    struct __wm_connection* link;
} wm_connection_t;

static wm_connection_t* connections;
static mtx_t            connections_sync;

static wm_connection_t* wm_connection_to_struct(int sock)
{
    wm_connection_t* conn;
    
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

static void wm_connection_add(wm_connection_t* connection)
{
    mtx_lock(&connections_sync);
    if (!connections) {
        connections = connection;
    }
    else {
        wm_connection_t* conn = connections;
        while (conn->link) {
            conn = conn->link;
        }
        conn->link = connection;
    }
    mtx_unlock(&connections_sync);
}

static void wm_connection_remove(wm_connection_t* connection)
{
    mtx_lock(&connections_sync);
    if (connections == connection) {
        connections = connection->link;
    }
    else {
        wm_connection_t* conn = connections;
        while (conn->link != connection) {
            conn = conn->link;
        }
        conn->link = connection->link;
    }
    mtx_unlock(&connections_sync);
}

int wm_connection_recv_message(int socket, wm_message_t* message, void* argument_buffer)
{
    intmax_t bytes_read;
    size_t   message_length;
    
    bytes_read = recv(socket, message, sizeof(wm_message_t), 0);
    if (bytes_read != sizeof(wm_message_t)) {
        // no bytes available, why??
        // TODO error code / handling
        return -1;
    }
    
    message_length = WM_MESSAGE_GET_LENGTH(message->length);
    
    // Verify the data read in the header
    if (message->magic != WM_HEADER_MAGIC ||
        message_length < sizeof(wm_message_t)) {
        // TODO error code / handling
        return -1;
    }
    
    // Read rest of message
    if (message_length > sizeof(wm_message_t)) {
        assert(message_length < WM_MAX_MESSAGE_SIZE);
        bytes_read = recv(socket, argument_buffer, 
            message_length - sizeof(wm_message_t), 0);
        if (bytes_read != message_length - sizeof(wm_message_t)) {
            // do not process incomplete requests
            // TODO error code / handling
            return -1; 
        }
    }
    return 0;
}

int wm_connection_initialize(void)
{
    mtx_init(&connections_sync, mtx_plain);
    connections = NULL;
    return 0;
}

int wm_connection_create(int socket, struct sockaddr_storage* address, int address_length)
{
    wm_connection_t* connection;
    
    connection = (wm_connection_t*)malloc(sizeof(wm_connection_t));
    if (!connection) {
        _set_errno(ENOMEM);
        return -1;
    }
    
    memset(connection, 0, sizeof(wm_connection_t));
    memcpy(&connection->address, address, address_length);
    connection->c_socket       = socket;
    connection->address_length = address_length;
    connection->alive          = 1;
    connection->link           = NULL;
    wm_connection_add(connection);
    return 0;
}

int wm_connection_shutdown(int socket)
{
    // get connetion from int
    wm_connection_t* connection = wm_connection_to_struct(socket);
    if (!connection) {
        _set_errno(EBADF);
        return -1;
    }
    
    wm_connection_remove(connection);
    close(connection->c_socket);
    free(connection);
    return EOK;
}
