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
#include <threads.h>

typedef struct {
    int             c_socket;
    struct sockaddr address;
    int             address_length;
    int             alive;
} wm_connection_t;

static wm_connection_event_handler_t connection_event_handler;

static int wm_connection_handler(void* param)
{
    wm_connection_t*     connection = (wm_connection_t*)param;
    int                  ping_ms    = 1000;
    int                  status;
    
    char                 buffer[256];
    wm_request_header_t* header = &buffer[0];
    void*                body   = &buffer[sizeof(wm_request_header_t)];
    
    // Set a timeout on recv so we can use ping the client at regular intervals
    status = setsockopt(connection->c_socket, SOL_SOCKET, SO_RCVTIMEO, 
        &ping_ms, sizeof(ping_ms));
    assert(status >= 0);
    
    // listen for messages
    while (connection->alive) {
        bytes_read = recv(connection->c_socket, header, 
            sizeof(wm_request_header_t), MSG_WAITALL);
        if (bytes_read != sizeof(wm_request_header_t)) {
            // timeout, send ping
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

        // elevate message to handler
        connection_event_handler(connection->c_socket, header);
    }
    
    // Cleanup connection
    shutdown(connection->c_socket, SHUT_RDRW);
    free(connection);
    return 0;
}

int wm_connection_initialize(wm_connection_event_handler_t handler)
{
    connection_event_handler = handler;
    return 0;
}

int wm_connection_create(int client_socket, struct sockaddr* address, int address_length)
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
    
    // Spawn a new thread to handle the connection
    thrd_create(&thread_id, wm_connection_handler, connection);
    return 0;
}

int wm_connection_shutdown(int connection)
{

}
