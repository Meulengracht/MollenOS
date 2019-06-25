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
 * Wm Surface Type Definitions & Structures
 * - This header describes the base surface-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __LIBWM_SURFACE_H__
#define __LIBWM_SURFACE_H__

#include "libwm_types.h"

// Surface API
// Generally os-specific surface functions that are needed during execution
// of the libwm operations. The surface handles are void* pointers since how
// shm-buffers are implemented varies
int   wm_buffer_create(size_t, size_t, wm_handle_t*, void**);
int   wm_buffer_inherit(wm_handle_t, void**);
int   wm_buffer_destroy(wm_handle_t, void*);
int   wm_buffer_resize(wm_handle_t, void*, size_t);
int   wm_buffer_get_metrics(wm_handle_t, size_t*, size_t*);
int   wm_buffer_get_handle(wm_handle_t, wm_handle_t*);

#endif // !__LIBWM_SURFACE_H__
