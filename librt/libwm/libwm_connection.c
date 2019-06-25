/* MollenOS
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
#include "libwm_connection.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

typedef struct __wm_connection {
    int                     c_socket;
    struct sockaddr_storage address;
    int                     address_length;
    int                     alive;
    int                     ping_attemps;
    struct __wm_connection* link;
} wm_connection_t;

static wm_connection_message_handler_t connection_event_handler;
static wm_connection_t*                connections;
static mtx_t                           connections_sync;

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

static int wm_connection_ping(wm_connection_t* connection)
{
    connection->ping_attemps++;
    
    // was this the fourth ? then mark as unresponsive
    if (connection->ping_attemps > 3) {
        
    }
    else {
        // send ping
    }
    return 0;
}

static int wm_connection_pong(wm_connection_t* connection)
{
    // update the last response time
    connection->ping_attemps = 0;
    
    // if we were in a non-responsive state before then send a control
    // event to the server
    return 0;
}

static int wm_connection_handler(void* param)
{
    wm_connection_t*     connection = (wm_connection_t*)param;
    int                  ping_ms    = 1000;
    int                  status;
    
    char                 buffer[256];
    wm_request_header_t* header = (wm_request_header_t*)&buffer[0];
    void*                body   = (void*)&buffer[sizeof(wm_request_header_t)];
    
    // Set a timeout on recv so we can use ping the client at regular intervals
    status = setsockopt(connection->c_socket, SOL_SOCKET, SO_RCVTIMEO, 
        &ping_ms, sizeof(ping_ms));
    assert(status >= 0);
    
    // listen for messages
    while (connection->alive) {
        intmax_t bytes_read = recv(connection->c_socket, header, 
            sizeof(wm_request_header_t), MSG_WAITALL);
        if (bytes_read != sizeof(wm_request_header_t)) {
            wm_connection_ping(connection);
            continue;
        }
        
        // Verify the data read in the header
        if (header->magic != WM_HEADER_MAGIC ||
            header->length < sizeof(wm_request_header_t)) {
            continue;
        }
        
        // Read rest of message
        if (header->length > sizeof(wm_request_header_t)) {
            assert(header->length < 256);
            bytes_read = recv(connection->c_socket, body, 
                header->length - sizeof(wm_request_header_t), MSG_WAITALL);
            if (bytes_read != header->length - sizeof(wm_request_header_t)) {
                continue; // do not process incomplete requests
            }
        }

        // handle ping/pong messages at connection level, otherwise
        // elevate message to handler
        if (header->event == wm_request_pong) {
            wm_connection_pong(connection);
        }
        else {
            connection_event_handler(connection->c_socket, header);
        }
    }
    
    // Cleanup connection
    wm_connection_remove(connection);
    shutdown(connection->c_socket, SHUT_RDWR);
    free(connection);
    return 0;
}

int wm_connection_initialize(wm_connection_message_handler_t handler)
{
    mtx_init(&connections_sync, mtx_plain);
    connection_event_handler = handler;
    connections              = NULL;
    return 0;
}

int wm_connection_create(int client_socket, struct sockaddr_storage* address, int address_length)
{
    wm_connection_t* connection;
    thrd_t           thread_id;
    
    // Create a new connection object
    connection = (wm_connection_t*)malloc(sizeof(wm_connection_t));
    if (!connection) {
        return ENOMEM;
    }
    memset(connection, 0, sizeof(wm_connection_t));
    memcpy(&connection->address, address, address_length);
    connection->c_socket       = client_socket;
    connection->address_length = address_length;
    connection->alive          = 1;
    connection->ping_attemps   = 0;
    connection->link           = NULL;
    wm_connection_add(connection);
    
    // Spawn a new thread to handle the connection
    thrd_create(&thread_id, wm_connection_handler, connection);
    return EOK;
}

int wm_connection_shutdown(int sock)
{
    // get connetion from int
    wm_connection_t* connection = wm_connection_to_struct(sock);
    if (!connection) {
        _set_errno(EBADF);
        return -1;
    }
    
    // set alive to 0, and then let the timeout trigger
    connection->alive = 0;
    return EOK;
}
