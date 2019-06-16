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
 * Wm Client Type Definitions & Structures
 * - This header describes the base client-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <assert.h>
#include <inet/socket.h>
#include "libwm_client.h"

static int wm_initialized = 0;
static int wm_socket      = -1;

static int wm_execute_command(wm_request_header_t* command)
{
    assert(wm_initialized == 1);
}

static int wm_listener(void* param)
{
    char                 buffer[256];
    wm_request_header_t* header = &buffer[0];
    void*                body   = &buffer[sizeof(wm_request_header_t)];
    int                  status;
    
    while (wm_initialized) {
        bytes_read = recv(wm_socket, header, 
            sizeof(wm_request_header_t), MSG_WAITALL);
        if (bytes_read != sizeof(wm_request_header_t)) {
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
            bytes_read = recv(wm_socket, body, 
                header->length - sizeof(wm_request_header_t), MSG_WAITALL);
            if (bytes_read != header->length - sizeof(wm_request_header_t)) {
                continue; // do not process incomplete requests
            }
        }
        
        // elevate message to handler
        wm_event_handler(header);
    }
    return shutdown(wm_socket, SHUT_RDWR);
}

int wm_client_initialize(wm_client_message_handler_t handler)
{
    struct sockaddr wm_address;
    int             wm_port;
    
    // Create a new socket for listening to wm events. They are all
    // delivered to fixed sockets on the local system.
    wm_socket = socket(AF_LOCAL, SOCK_STREAM, 0);
    assert(wm_socket >= 0);
    
    // Prepare the server address. 
    memset(&wm_address, 0, sizeof(struct sockaddr));
    wm_address.sin_family      = AF_LOCAL;
    wm_address.sin_addr.s_addr = INADDR_ANY;
    wm_address.sin_port        = htons(wm_port);
    
    // Connect to the compositor
    status = connect(wm_socket, &wm_address, sizeof(wm_address));
    assert(status >= 0);
}

int wm_client_create_window(void)
{
    wm_request_window_create_t request;

    wm_execute_command(&request.header);
}

int wm_client_destroy_Window(void)
{
    wm_request_window_create_t request;

    wm_execute_command(&request.header);
}

int wm_client_redraw_window(void)
{
    wm_request_window_create_t request;

    wm_execute_command(&request.header);
}

int wm_client_window_set_title(void)
{
    wm_request_window_create_t request;

    wm_execute_command(&request.header);
}

int wm_client_request_buffer(void)
{
    wm_request_window_create_t request;

    wm_execute_command(&request.header);
}

int wm_client_release_buffer(void)
{
    wm_request_window_create_t request;

    wm_execute_command(&request.header);
}

int wm_client_resize_buffer(void)
{
    wm_request_window_create_t request;

    wm_execute_command(&request.header);
}

int wm_client_set_active_buffer(void)
{
    wm_request_window_create_t request;

    wm_execute_command(&request.header);
}

int wm_client_shutdown(void)
{
    wm_request_window_create_t request;

    wm_execute_command(&request.header);
}
