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
 * Wm Buffer Type Definitions & Structures
 * - This header describes the base buffer-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __LIBWM_BUFFER_H__
#define __LIBWM_BUFFER_H__

#include "libwm_types.h"

struct wm_buffer;

// Buffer API
// Generally os-specific buffer functions that are needed during execution
// of the libwm operations. The buffer handles are void* pointers since how
// shm-buffers are implemented varies across os

// Client
int wm_buffer_create(size_t, size_t, void**, struct wm_buffer**);
int wm_buffer_resize(struct wm_buffer*, size_t);

// Server
int wm_buffer_inherit(wm_handle_t, void**, struct wm_buffer**);
int wm_buffer_refresh(struct wm_buffer*);

// Common
int wm_buffer_get_handle(struct wm_buffer*, wm_handle_t*);
int wm_buffer_get_pointer(struct wm_buffer*, void**);
int wm_buffer_get_length(struct wm_buffer*, size_t*);
int wm_buffer_destroy(struct wm_buffer*);

#endif // !__LIBWM_SURFACE_H__
