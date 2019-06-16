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

#ifndef __LIBWM_SERVER_H__
#define __LIBWM_SERVER_H__

#include "libwm_types.h"

typedef void(*wm_server_message_handler_t)(int, wm_request_header_t*);

// Server API
// This should be called for the compositor that wants to manage
// wm-clients. This will initiate data structures and setup handler threads
int wm_server_initialize(wm_server_message_handler_t);
int wm_server_shutdown(void);

#endif // !__LIBWM_SERVER_H__
