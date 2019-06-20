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

#include <ddk/buffer.h>
#include "../../libwm_buffer.h"

int wm_buffer_create(size_t initial_size, size_t capacity, wm_handle_t* handle_out)
{
    return OsStatusToErrno(BufferCreate(initial_size, capacity, (UUId_t*)handle_out));
}

int wm_buffer_inherit(wm_handle_t handle)
{
    return OsStatusToErrno(BufferCreateFrom((UUId_t)handle));
}

int wm_buffer_destroy(wm_handle_t handle)
{
    return OsStatusToErrno(BufferDestroy((UUId_t)handle));
}

int wm_buffer_resize(wm_handle_t handle, size_t size)
{
    return OsStatusToErrno(BufferResize((UUId_t)handle, size));
}

void* wm_buffer_get_pointer(wm_handle_t handle)
{
    return BufferGetAccessPointer((UUId_t)handle);
}

size_t wm_buffer_get_metrics(wm_handle_t handle, size_t* size_out, size_t* capacity_out)
{
    return OsStatusToErrno(BufferGetMetrics((UUId_t)handle, size_out, capacity_out));
}

int wm_buffer_get_handle(wm_handle_t handle, wm_handle_t* handle_out)
{
    // our handles are already portable.
    *handle_out = handle;
    return 0;
}
