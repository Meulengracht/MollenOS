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
 * Wm Client Type Definitions & Structures
 * - This header describes the base client-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __LIBWM_CLIENT_H__
#define __LIBWM_CLIENT_H__

#include "libwm_types.h"

typedef struct wm_client wm_client_t;

// Client API
// An application can utilize multiple clients, that connect to different
// servers. When invoking a protocol the specific client can be specified.
int wm_client_initialize(wm_client_t**);
int wm_client_invoke(wm_client_t*, uint8_t, uint8_t, void*, size_t, void*, size_t);
int wm_client_shutdown(wm_client_t*);

#endif // !__LIBWM_CLIENT_H__
