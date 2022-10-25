/**
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
 */

#include <ddk/handle.h>
#include <errno.h>
#include <internal/_io.h>
#include <ioctl.h>

oserr_t stdio_ipc_op_read(stdio_handle_t* handle, void* buffer, size_t length, size_t* bytes_read)
{
    streambuffer_t* stream  = handle->object.data.ipcontext.stream;
    unsigned int    options = handle->object.data.ipcontext.options;
    unsigned int    base;
    unsigned int    state;
    size_t          bytesAvailable;

    bytesAvailable = streambuffer_read_packet_start(stream, options, &base, &state);
    if (!bytesAvailable) {
        _set_errno(ENODATA);
        return -1;
    }

    streambuffer_read_packet_data(stream, buffer, MIN(length, bytesAvailable), &state);
    streambuffer_read_packet_end(stream, base, bytesAvailable);

    *bytes_read = MIN(length, bytesAvailable);
    return OS_EOK;
}

oserr_t stdio_ipc_op_write(stdio_handle_t* handle, const void* buffer, size_t length, size_t* bytes_written)
{
    // Write is not supported
    return OS_ENOTSUPPORTED;
}

oserr_t stdio_ipc_op_seek(stdio_handle_t* handle, int origin, off64_t offset, long long* position_out)
{
    // Seek is not supported
    return OS_ENOTSUPPORTED;
}

oserr_t stdio_ipc_op_resize(stdio_handle_t* handle, long long resize_by)
{
    // Resize is not supported
    return OS_ENOTSUPPORTED;
}

oserr_t stdio_ipc_op_close(stdio_handle_t* handle, int options)
{
    if (handle->object.handle != UUID_INVALID) {
        return handle_destroy(handle->object.handle);
    }
    return OS_ENOTSUPPORTED;
}

oserr_t stdio_ipc_op_inherit(stdio_handle_t* handle)
{
    // Is not supported
    return OS_EOK;
}

oserr_t stdio_ipc_op_ioctl(stdio_handle_t* handle, int request, va_list args)
{
    streambuffer_t* stream = handle->object.data.ipcontext.stream;

    if ((unsigned int)request == FIONBIO) {
        int* nonBlocking = va_arg(args, int*);
        if (nonBlocking) {
            if (*nonBlocking) {
                handle->object.data.ipcontext.options |= STREAMBUFFER_NO_BLOCK;
            }
            else {
                handle->object.data.ipcontext.options &= ~(STREAMBUFFER_NO_BLOCK);
            }
        }
        return OS_EOK;
    }
    else if ((unsigned int)request == FIONREAD) {
        int* bytesAvailableOut = va_arg(args, int*);
        if (bytesAvailableOut) {
            size_t bytesAvailable;
            streambuffer_get_bytes_available_in(stream, &bytesAvailable);
            *bytesAvailableOut = (int)bytesAvailable;
        }
        return OS_EOK;
    }
    return OS_ENOTSUPPORTED;
}

void stdio_get_ipc_operations(stdio_ops_t* ops)
{
    ops->inherit = stdio_ipc_op_inherit;
    ops->read    = stdio_ipc_op_read;
    ops->write   = stdio_ipc_op_write;
    ops->seek    = stdio_ipc_op_seek;
    ops->resize  = stdio_ipc_op_resize;
    ops->ioctl   = stdio_ipc_op_ioctl;
    ops->close   = stdio_ipc_op_close;
}
