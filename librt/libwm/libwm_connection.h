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

#ifndef __LIBWM_CONNECTION_H__
#define __LIBWM_CONNECTION_H__

#include "libwm_types.h"

// Prototypes
struct sockaddr;

typedef void(*wm_connection_event_handler_t)(int, wm_request_header_t*);

// Connection API
// Used to manage all the connections to the window manager.
int wm_connection_initialize(wm_connection_event_handler_t);
int wm_connection_create(int, struct sockaddr*, int);
int wm_connection_shutdown(int);

#endif // !__LIBWM_CONNECTION_H__
