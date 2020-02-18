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

#include <errno.h>
#include <os/dmabuf.h>
#include <os/mollenos.h>
#include "../../include/gracht/buffer.h"
#include <stdlib.h>

struct gracht_buffer {
    struct dma_buffer_info info;
    struct dma_attachment  attachment;
};

int gracht_buffer_create(size_t initial_size, size_t capacity, void** pointer_out, struct gracht_buffer** buffer_out)
{
    struct gracht_buffer* buffer;
    int               status;
    
    buffer = malloc(sizeof(struct gracht_buffer));
    if (!buffer) {
        _set_errno(ENOMEM);
        return -1;
    }
    
    // initialize the info structure
    buffer->info.name     = "libgracht_dma_buffer";
    buffer->info.length   = initial_size;
    buffer->info.capacity = capacity;
    buffer->info.flags    = 0;
    
    status = OsStatusToErrno(dma_create(&buffer->info, &buffer->attachment));
    if (status) {
        free(buffer);
        return -1;
    }
    
    *pointer_out = buffer->attachment.buffer;
    *buffer_out  = buffer;
    return EOK;
}

int gracht_buffer_resize(struct gracht_buffer* buffer, size_t size)
{
    if (size > buffer->info.capacity) {
        _set_errno(ENOSPC);
        return -1;
    }
    return OsStatusToErrno(dma_attachment_resize(&buffer->attachment, size));
}

int gracht_buffer_inherit(gracht_handle_t handle, void** memory_out, struct gracht_buffer** buffer_out)
{
    struct gracht_buffer* buffer;
    int               status;
    
    buffer = malloc(sizeof(struct gracht_buffer));
    if (!buffer) {
        _set_errno(ENOMEM);
        return -1;
    }
    
    status = OsStatusToErrno(dma_attach((UUId_t)handle, &buffer->attachment));
    if (status) {
        free(buffer);
        return -1;
    }
    
    status = OsStatusToErrno(dma_attachment_map(&buffer->attachment));
    if (status) {
        (void)dma_detach(&buffer->attachment);
        free(buffer);
        return -1;
    }
    
    *memory_out = buffer->attachment.buffer;
    *buffer_out = buffer;
    return EOK;
}

int gracht_buffer_refresh(struct gracht_buffer* buffer)
{
    if (!buffer) {
        _set_errno(EINVAL);
        return -1;
    }
    return OsStatusToErrno(dma_attachment_refresh_map(&buffer->attachment));
}

int gracht_buffer_get_handle(struct gracht_buffer* buffer, gracht_handle_t* handle_out)
{
    if (!buffer || !handle_out) {
        _set_errno(EINVAL);
        return -1;
    }
    
    *handle_out = (gracht_handle_t)buffer->attachment.handle;
    return EOK;
}

int gracht_buffer_get_pointer(struct gracht_buffer* buffer, void** pointer_out)
{
    if (!buffer || !pointer_out) {
        _set_errno(EINVAL);
        return -1;
    }
    
    *pointer_out = buffer->attachment.buffer;
    return EOK;
}

int gracht_buffer_get_length(struct gracht_buffer* buffer, size_t* length_out)
{
    if (!buffer || !length_out) {
        _set_errno(EINVAL);
        return -1;
    }
    
    *length_out = buffer->attachment.length;
    return EOK;
}

int gracht_buffer_destroy(struct gracht_buffer* buffer)
{
    if (!buffer) {
        _set_errno(EINVAL);
        return -1;
    }
    
    (void)dma_attachment_unmap(&buffer->attachment);
    (void)dma_detach(&buffer->attachment);
    free(buffer);
    return 0;
}
