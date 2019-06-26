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

#include <errno.h>
#include <os/mollenos.h>
#include "../../libwm_buffer.h"
#include <stdlib.h>

struct wm_buffer {
    UUId_t handle;
    size_t length;
    size_t capacity;
    void*  pointer;
};

int wm_buffer_create(size_t initial_size, size_t capacity, void** pointer_out, struct wm_buffer** buffer_out)
{
    struct wm_buffer* buffer;
    int               status;
    
    buffer = malloc(sizeof(struct wm_buffer));
    if (!buffer) {
        _set_errno(ENOMEM);
        return -1;
    }
    
    status = OsStatusToErrno(MemoryShare(initial_size, capacity, 
        pointer_out, &buffer->handle));
    if (status) {
        free(buffer);
        return -1;
    }
    
    buffer->length   = initial_size;
    buffer->capacity = capacity;
    buffer->pointer  = *pointer_out;
    *buffer_out      = buffer;
    return EOK;
}

int wm_buffer_resize(struct wm_buffer* buffer, size_t size)
{
    int status;
    
    if (size > buffer->capacity) {
        _set_errno(ENOSPC);
        return -1;
    }
    
    status = OsStatusToErrno(MemoryResize(buffer->handle, buffer->pointer, size));
    if (!status) {
        buffer->length = size;
    }
    return status;
}

int wm_buffer_inherit(wm_handle_t handle, void** memory_out, struct wm_buffer** buffer_out)
{
    struct wm_buffer* buffer;
    int               status;
    
    buffer = malloc(sizeof(struct wm_buffer));
    if (!buffer) {
        _set_errno(ENOMEM);
        return -1;
    }
    
    buffer->handle = (UUId_t)handle;
    status         = OsStatusToErrno(MemoryInherit(buffer->handle, 
        &buffer->pointer, &buffer->length, &buffer->capacity));
    if (status) {
        free(buffer);
        return -1;
    }
    
    *memory_out = buffer->pointer;
    *buffer_out = buffer;
    return EOK;
}

int wm_buffer_refresh(struct wm_buffer* buffer)
{
    if (!buffer) {
        _set_errno(EINVAL);
        return -1;
    }
    
    return OsStatusToErrno(MemoryRefresh(buffer->handle, 
        buffer->pointer, buffer->length));
}

int wm_buffer_get_handle(struct wm_buffer* buffer, wm_handle_t* handle_out)
{
    if (!buffer || !handle_out) {
        _set_errno(EINVAL);
        return -1;
    }
    
    *handle_out = (wm_handle_t)buffer->handle;
    return EOK;
}

int wm_buffer_get_pointer(struct wm_buffer* buffer, void** pointer_out)
{
    if (!buffer || !pointer_out) {
        _set_errno(EINVAL);
        return -1;
    }
    
    *pointer_out = buffer->pointer;
    return EOK;
}

int wm_buffer_get_length(struct wm_buffer* buffer, size_t* length_out)
{
    if (!buffer || !length_out) {
        _set_errno(EINVAL);
        return -1;
    }
    
    *length_out = buffer->length;
    return EOK;
}

int wm_buffer_destroy(struct wm_buffer* buffer)
{
    int status;
    
    if (!buffer) {
        _set_errno(EINVAL);
        return -1;
    }
    
    status = OsStatusToErrno(MemoryFree(buffer->pointer, buffer->capacity));
    if (status) {
        return -1;
    }
    
    status = OsStatusToErrno(MemoryUnshare(buffer->handle));
    if (status) {
        return -1;
    }
    
    free(buffer);
    return 0;
}
