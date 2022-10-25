/**
 * MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * C Standard Library
 * - Standard IO null operation implementations.
 */

#include <errno.h>
#include <internal/_io.h>
#include <ddk/handle.h>

oserr_t stdio_null_op_read(stdio_handle_t* handle, void* buffer, size_t length, size_t* bytes_read)
{
    return OS_ENOTSUPPORTED;
}

oserr_t stdio_null_op_write(stdio_handle_t* handle, const void* buffer, size_t length, size_t* bytes_written)
{
    return OS_ENOTSUPPORTED;
}

oserr_t stdio_null_op_seek(stdio_handle_t* handle, int origin, off64_t offset, long long* position_out)
{
    return OS_ENOTSUPPORTED;
}

oserr_t stdio_null_op_resize(stdio_handle_t* handle, long long resize_by)
{
    return OS_ENOTSUPPORTED;
}

oserr_t stdio_null_op_close(stdio_handle_t* handle, int options)
{
    if (handle->object.handle != UUID_INVALID) {
        return handle_destroy(handle->object.handle);
    }
    return OS_ENOTSUPPORTED;
}

oserr_t stdio_null_op_inherit(stdio_handle_t* handle)
{
    return OS_EOK;
}

oserr_t stdio_null_op_ioctl(stdio_handle_t* handle, int request, va_list vlist)
{
    return OS_ENOTSUPPORTED;
}

void stdio_get_null_operations(stdio_ops_t* ops)
{
    ops->inherit = stdio_null_op_inherit;
    ops->read    = stdio_null_op_read;
    ops->write   = stdio_null_op_write;
    ops->seek    = stdio_null_op_seek;
    ops->resize  = stdio_null_op_resize;
    ops->ioctl   = stdio_null_op_ioctl;
    ops->close   = stdio_null_op_close;
}
