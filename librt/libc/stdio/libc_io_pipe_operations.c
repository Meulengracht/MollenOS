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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * C Standard Library
 * - Standard IO pipe operation implementations.
 */

#include <ds/streambuffer.h>
#include <ddk/handle.h>
#include <ddk/utils.h>
#include <ioctl.h>
#include <internal/_io.h>

OsStatus_t stdio_pipe_op_read(stdio_handle_t* handle, void* buffer, size_t length, size_t* bytes_read)
{
    streambuffer_t* stream  = handle->object.data.pipe.attachment.buffer;
    unsigned int    options = handle->object.data.pipe.options;
    size_t          bytesRead;
    if (handle->wxflag & WX_NULLPIPE) {
        return OsNotSupported;
    }

    bytesRead = streambuffer_stream_in(stream, buffer, length, options);
    *bytes_read = bytesRead;
    return OsSuccess;
}

OsStatus_t stdio_pipe_op_write(stdio_handle_t* handle, const void* buffer, size_t length, size_t* bytes_written)
{
    streambuffer_t* stream = handle->object.data.pipe.attachment.buffer;
    unsigned int    options = handle->object.data.pipe.options;
    size_t          bytesWritten;
    if (handle->wxflag & WX_NULLPIPE) {
        *bytes_written = length;
        ERROR("%s", buffer);
        return OsSuccess;
    }

    bytesWritten = streambuffer_stream_out(stream, (void*)buffer, length, options);
    stdio_handle_activity(handle, IOSETIN); // Mark pipe for recieved data

    *bytes_written = bytesWritten;
    return OsSuccess;
}

OsStatus_t stdio_pipe_op_seek(stdio_handle_t* handle, int origin, off64_t offset, long long* position_out)
{
    return OsNotSupported;
}

OsStatus_t stdio_pipe_op_resize(stdio_handle_t* handle, long long resize_by)
{
    // This could be implemented some day, but for now we do not support
    // the resize operation on pipes.
    return OsNotSupported;
}

OsStatus_t stdio_pipe_op_close(stdio_handle_t* handle, int options)
{
    // Depending on the setup of the pipe. If the pipe is local, then we 
    // can simply free the structure. If the pipe is global/inheritable, we need
    // to free the memory used, and destroy the handle.
    (void)dma_attachment_unmap(&handle->object.data.pipe.attachment);
    (void)dma_detach(&handle->object.data.pipe.attachment);
    if (options == STDIO_CLOSE_FULL) {
        handle_destroy(handle->object.handle);
    }
    return OsSuccess;
}

OsStatus_t stdio_pipe_op_inherit(stdio_handle_t* handle)
{
    OsStatus_t status;

    status = dma_attach(handle->object.data.pipe.attachment.handle,
        &handle->object.data.pipe.attachment);
    if (status != OsSuccess) {
        return status;
    }
    
    status = dma_attachment_map(&handle->object.data.pipe.attachment, DMA_ACCESS_WRITE);
    return status;
}

OsStatus_t stdio_pipe_op_ioctl(stdio_handle_t* handle, int request, va_list args)
{
    streambuffer_t* stream = handle->object.data.pipe.attachment.buffer;

    if ((unsigned int)request == FIONBIO) {
        int* nonBlocking = va_arg(args, int*);
        if (nonBlocking) {
            if (*nonBlocking) {
                handle->object.data.pipe.options |= STREAMBUFFER_NO_BLOCK;
            }
            else {
                handle->object.data.pipe.options &= ~(STREAMBUFFER_NO_BLOCK);
            }
        }
        return OsSuccess;
    }
    else if ((unsigned int)request == FIONREAD) {
        int* bytesAvailableOut = va_arg(args, int*);
        if (bytesAvailableOut) {
            size_t bytesAvailable = 0;
            streambuffer_get_bytes_available_in(stream, &bytesAvailable);
            *bytesAvailableOut = (int)bytesAvailable;
        }
        return OsSuccess;
    }
    return OsNotSupported;
}

void stdio_get_pipe_operations(stdio_ops_t* ops)
{
    ops->inherit = stdio_pipe_op_inherit;
    ops->read    = stdio_pipe_op_read;
    ops->write   = stdio_pipe_op_write;
    ops->seek    = stdio_pipe_op_seek;
    ops->resize  = stdio_pipe_op_resize;
    ops->ioctl   = stdio_pipe_op_ioctl;
    ops->close   = stdio_pipe_op_close;
}
