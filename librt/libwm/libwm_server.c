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
#include "libwm_connection.h"
#include "libwm_os.h"
#include "libwm_server.h"
#include <threads.h>
#include <signal.h>
#include <string.h>

static wm_server_input_handler_t   wm_input_handler;
static wm_server_message_handler_t wm_server_handler;
static thrd_t                      wm_listener_thread;
static thrd_t                      wm_input_thread;
static int                         wm_initialized = 0;

// connection thread
static int wm_listener(void* param)
{
    struct sockaddr_storage wm_address;
    socklen_t               wm_address_length;
    int                     wm_socket;
    int                     status;
    wm_os_thread_set_name("wm_listener");
    
    // Create the listener for local pc, we are not internet active
    // at this moment. Use asserts for error checking as we have no direct
    // output except for debug consoles
    wm_socket = socket(AF_LOCAL, SOCK_STREAM, 0);
    assert(wm_socket >= 0);
    
    // Prepare the server address. 
    wm_os_get_server_address(&wm_address, &wm_address_length);
    status = bind(wm_socket, sstosa(&wm_address), wm_address_length);
    assert(status >= 0);
    
    // Enable listening for connections, with a maximum of 2 on backlog
    status = listen(wm_socket, 2);
    assert(status >= 0);
    
    while (wm_initialized) {
        struct sockaddr_storage client_address;
        int                     client_socket;
        int                     client_socket_length;
        
        client_socket = accept(wm_socket, sstosa(&client_address), &client_socket_length);
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
static int wm_input_worker(void* param)
{
    struct sockaddr_storage input_address;
    socklen_t               input_address_length;
    int                     input_socket;
    int                     status;
    wm_os_thread_set_name("wm_input");
    
    // Create a new socket for listening to input events. They are all
    // delivered to fixed sockets on the local system.
    input_socket = socket(AF_LOCAL, SOCK_STREAM, 0);
    assert(input_socket >= 0);
    
    // Connect to the input pipe
    wm_os_get_input_address(&input_address, &input_address_length);
    status = connect(input_socket, sstosa(&input_address), input_address_length);
    assert(status >= 0);
    
    while (wm_initialized) {
        wm_input_event_t input_data;
        intmax_t         bytes_read;
        
        bytes_read = recv(input_socket, &input_data, sizeof(wm_input_event_t), MSG_WAITALL);
        if (bytes_read != sizeof(wm_input_event_t)) {
            continue; // do not process incomplete requests
        }
        
        // elevate key press
        wm_input_handler(&input_data);
    }
    return shutdown(input_socket, SHUT_RDWR);
}

static void wm_connection_handler(int connection, wm_request_header_t* request)
{
    // handle internal events
    switch (request->type) {
        case wm_request_window_create: {
            
        } break;
        case wm_request_window_destroy: {
            
        } break;
        case wm_request_window_set_title: {
            
        } break;
        case wm_request_window_redraw: {
            
        } break;
        case wm_request_window_resize: {
            
        } break;
        case wm_request_surface_register: {
            
        } break;
        case wm_request_surface_unregister: {
            
        } break;
        case wm_request_surface_set_active: {
            
        } break;
        
        default:
            return;
    }

    // let the user code handle it
    wm_server_handler(connection, request);
}

int wm_server_initialize(wm_server_message_handler_t handler, wm_server_input_handler_t input_handler)
{
    // store handler
    assert(wm_initialized == 0);
    wm_initialized    = 1;
    wm_server_handler = handler;
    wm_input_handler  = input_handler;
    
    // initialize connection library
    wm_connection_initialize(wm_connection_handler);

    // create threads
    thrd_create(&wm_listener_thread, wm_listener, NULL);
    thrd_create(&wm_input_thread, wm_input_worker, NULL);
    return 0;
}

int wm_server_shutdown(void)
{
    wm_initialized = 0;
    thrd_signal(wm_listener_thread, SIGTERM);
    thrd_signal(wm_input_thread, SIGTERM);
    return 0;
}
