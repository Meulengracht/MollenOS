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
 * Gracht Buffer Type Definitions & Structures
 * - This header describes the base buffer-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __GRACHT_BUFFER_H__
#define __GRACHT_BUFFER_H__

#include "types.h"

struct gracht_buffer;

// Buffer API
// Generally os-specific buffer functions that are needed during execution
// of the libgracht operations. The buffer handles are void* pointers since how
// shm-buffers are implemented varies across os

// Client
int gracht_buffer_create(size_t, size_t, void**, struct gracht_buffer**);
int gracht_buffer_resize(struct gracht_buffer*, size_t);

// Server
int gracht_buffer_inherit(gracht_handle_t, void**, struct gracht_buffer**);
int gracht_buffer_refresh(struct gracht_buffer*);

// Common
int gracht_buffer_get_handle(struct gracht_buffer*, gracht_handle_t*);
int gracht_buffer_get_pointer(struct gracht_buffer*, void**);
int gracht_buffer_get_length(struct gracht_buffer*, size_t*);
int gracht_buffer_destroy(struct gracht_buffer*);

#endif // !__GRACHT_BUFFER_H__
