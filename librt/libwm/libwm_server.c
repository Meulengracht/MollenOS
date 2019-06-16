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
 * Wm Server Type Definitions & Structures
 * - This header describes the base server-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <assert.h>
#include <inet/socket.h>
#include "libwm_server.h"
#include <threads.h>
#include <string.h>

static wm_server_message_handler_t wm_server_handler;
static thrd_t                      wm_listener_thread;
static thrd_t                      wm_input_thread;
static int                         wm_initialized = 0;

// connection thread
static int wm_listener(void* param)
{
    struct sockaddr wm_address;
    int             wm_socket;
    int             wm_port;
    int             status;
    
    // Create the listener for local pc, we are not internet active
    // at this moment. Use asserts for error checking as we have no direct
    // output except for debug consoles
    wm_socket = socket(AF_LOCAL, SOCK_STREAM, 0);
    assert(wm_socket >= 0);
    
    // Prepare the server address. 
    memset(&wm_address, 0, sizeof(struct sockaddr));
    wm_address.sin_family      = AF_LOCAL;
    wm_address.sin_addr.s_addr = INADDR_ANY;
    wm_address.sin_port        = htons(wm_port);
    
    status = bind(wm_socket, &wm_address, sizeof(wm_address));
    assert(status >= 0);
    
    // Enable listening for connections, with a maximum of 2 on backlog
    status = listen(wm_socket, 2);
    assert(status >= 0);
    
    while (wm_initialized) {
        struct sockaddr client_address;
        int             client_socket;
        int             client_socket_length;
        
        client_socket = accept(wm_socket, &client_address, &client_socket_length);
        if (client_socket < 0) {
            // log accept failure
            continue;
        }
        
        // Ok, create a new connection for the client
        status = wm_connection_create(client_socket, &client_address, 
            client_socket_length);
        assert(status >= 0);
    }
    return shutdown(wm_socket, SHUT_RDWR);
}

// input thread
static int wm_input_handler(void* param)
{
    struct sockaddr input_address;
    int             input_port;
    int             input_socket;
    int             status;
    
    // Create a new socket for listening to input events. They are all
    // delivered to fixed sockets on the local system.
    input_socket = socket(AF_LOCAL, SOCK_STREAM, 0);
    assert(input_socket >= 0);
    
    // Prepare the server address. 
    memset(&input_address, 0, sizeof(struct sockaddr));
    input_address.sin_family      = AF_LOCAL;
    input_address.sin_addr.s_addr = INADDR_ANY;
    input_address.sin_port        = htons(input_port);
    
    // Connect to the input pipe
    status = connect(input_socket, &input_address, sizeof(input_address));
    assert(status >= 0);
    
    while (wm_initialized) {
        wm_input_event_t input_data;
        ssize_t          bytes_read;
        
        bytes_read = recv(input_socket, &input_data, sizeof(wm_input_event_t), MSG_WAITALL);
        if (bytes_read != sizeof(wm_input_event_t)) {
            continue; // do not process incomplete requests
        }
        
        // elevate key press
        user_input_handler(&input_data);
    }
    return shutdown(input_socket, SHUT_RDWR);
}

static void wm_connection_handler(int connection, wm_request_header_t* request)
{
    // handle internal events

    // let the user code handle it
    wm_server_handler(connection, request);
}

int wm_server_initialize(wm_server_message_handler_t handler)
{
    // store handler
    assert(wm_initialized == 0);
    wm_initialized    = 1;
    wm_server_handler = handler;

    // create threads
    thrd_create(&wm_listener_thread, wm_listener, NULL);
    thrd_create(&wm_input_thread, wm_input_handler, NULL);
}

int wm_server_shutdown(void)
{
    wm_initialized = 0;
    thrd_signal(wm_listener_thread, SIGTERM);
    thrd_signal(wm_input_thread, SIGTERM);
}
