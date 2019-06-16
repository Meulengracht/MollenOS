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

#ifndef __LIBWM_CLIENT_H__
#define __LIBWM_CLIENT_H__

#include "libwm_types.h"

typedef void(*wm_client_message_handler_t)(wm_request_header_t*);

// Client API
// Clients can have multiple windows, and multiple buffers per window, which can be
// manipulated by the client. The client is responsible for drawing and informing
// the server when to redraw
int wm_client_initialize(wm_client_message_handler_t);
int wm_client_create_window(void);
int wm_client_destroy_Window(void);
int wm_client_redraw_window(void);
int wm_client_window_set_title(void);
int wm_client_request_buffer(void);
int wm_client_release_buffer(void);
int wm_client_resize_buffer(void);
int wm_client_set_active_buffer(void);
int wm_client_shutdown(void);

#endif // !__LIBWM_CLIENT_H__
