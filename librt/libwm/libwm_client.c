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
#include "libwm_os.h"
#include <string.h>
#include <threads.h>

static wm_client_message_handler_t wm_message_handler;
static int                         wm_initialized = 0;
static int                         wm_socket      = -1;
static int                         wm_window_id   = 0;
static thrd_t                      wm_listener_thread;

static int wm_execute_command(wm_request_header_t* command)
{
    intmax_t bytes_written;
    assert(wm_initialized == 1);
    
    bytes_written = send(wm_socket, (const void*)command, 
        command->length, MSG_WAITALL);
    return bytes_written != command->length;
}

static void wm_client_event_handler(wm_request_header_t* event)
{
    
    
    // elevate event
    wm_message_handler(event);
}

static int wm_listener(void* param)
{
    char                 buffer[256];
    wm_request_header_t* header = (wm_request_header_t*)&buffer[0];
    void*                body   = (void*)&buffer[sizeof(wm_request_header_t)];
    wm_os_thread_set_name("wm_client");

    while (wm_initialized) {
        intmax_t bytes_read = recv(wm_socket, header, 
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
        wm_client_event_handler(header);
    }
    return shutdown(wm_socket, SHUT_RDWR);
}

int wm_client_initialize(wm_client_message_handler_t handler)
{
    struct sockaddr_storage wm_address;
    socklen_t               wm_address_length;
    int                     status;
    
    // store settings
    wm_message_handler = handler;
    
    // Create a new socket for listening to wm events. They are all
    // delivered to fixed sockets on the local system.
    wm_socket = socket(AF_LOCAL, SOCK_STREAM, 0);
    assert(wm_socket >= 0);
    
    // Connect to the compositor
    wm_os_get_server_address(&wm_address, &wm_address_length);
    status = connect(wm_socket, sstosa(&wm_address), wm_address_length);
    assert(status >= 0);
    
    // start the listener thread
    thrd_create(&wm_listener_thread, wm_listener, NULL);
    
    return status;
}

int wm_client_create_window(const char* title, int x, int y, int w, int h, unsigned int flags)
{
    wm_request_window_create_t request;
    
    request.header.magic  = WM_HEADER_MAGIC;
    request.header.event  = wm_request_window_create;
    request.header.length = sizeof(wm_request_window_create_t);

    request.dimensions.x = x;
    request.dimensions.y = y;
    request.dimensions.w = w;
    request.dimensions.h = h;
    
    request.handle = wm_window_id++;
    request.flags  = flags;
    strncpy(&request.title[0], title, sizeof(request.title) - 1);
    
    if (wm_execute_command(&request.header)) {
        return -1;
    }
    return request.handle;
}

int wm_client_destroy_window(int handle)
{
    wm_request_window_destroy_t request;
    
    request.header.magic  = WM_HEADER_MAGIC;
    request.header.event  = wm_request_window_destroy;
    request.header.length = sizeof(wm_request_window_destroy_t);

    
    
    return wm_execute_command(&request.header);
}

int wm_client_redraw_window(int handle, wm_rect_t* damage_area)
{
    wm_request_window_redraw_t request;

    request.header.magic  = WM_HEADER_MAGIC;
    request.header.event  = wm_request_window_redraw;
    request.header.length = sizeof(wm_request_window_redraw_t);
    
    
    
    return wm_execute_command(&request.header);
}

int wm_client_window_set_title(int handle, const char* title)
{
    wm_request_window_set_title_t request;


    return wm_execute_command(&request.header);
}

int wm_client_resize_window(int handle, wm_rect_t* area)
{
    wm_request_window_resize_t request;


    return wm_execute_command(&request.header);
}

int wm_client_register_buffer(wm_handle_t handle, wm_surface_descriptor_t* surface)
{
    wm_request_surface_register_t request;


    return wm_execute_command(&request.header);
}

int wm_client_unregister_buffer(wm_handle_t handle)
{
    wm_request_surface_unregister_t request;


    return wm_execute_command(&request.header);
}

int wm_client_set_active_buffer(wm_handle_t handle)
{
    wm_request_surface_set_active_t request;


    return wm_execute_command(&request.header);
}

int wm_client_shutdown(void)
{
    // send disconnect request
    // join thread
    //thrd_join(wm_listener_thread);
    return 0;
}
