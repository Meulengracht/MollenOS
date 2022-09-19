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
 * - Standard IO socket operation implementations.
 */

#include <internal/_io.h>
#include <internal/_ipc.h>
#include <ioctl.h>
#include <os/dmabuf.h>

oserr_t stdio_net_op_read(stdio_handle_t* handle, void* buffer, size_t length, size_t* bytes_read)
{
    intmax_t num_bytes = recv(handle->fd, buffer, length, 0);
    if (num_bytes >= 0) {
        *bytes_read = (size_t)num_bytes;
        return OsOK;
    }
    return OsError;
}

oserr_t stdio_net_op_write(stdio_handle_t* handle, const void* buffer, size_t length, size_t* bytes_written)
{
    intmax_t num_bytes = send(handle->fd, buffer, length, 0);
    if (num_bytes >= 0) {
        *bytes_written = (size_t)num_bytes;
        return OsOK;
    }
    return OsError;
}

oserr_t stdio_net_op_seek(stdio_handle_t* handle, int origin, off64_t offset, long long* position_out)
{
    // It is not possible to seek in sockets.
    return OsNotSupported;
}

oserr_t stdio_net_op_resize(stdio_handle_t* handle, long long resize_by)
{
    // TODO: Implement resizing of socket buffers
    return OsNotSupported;
}

oserr_t stdio_net_op_close(stdio_handle_t* handle, int options)
{
    oserr_t status = OsOK;
    
    if (options & STDIO_CLOSE_FULL) {
        struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetNetService());
        sys_socket_close(GetGrachtClient(), &msg.base, handle->object.handle,
            SYS_CLOSE_OPTIONS_DESTROY);
        sys_socket_close_result(GetGrachtClient(), &msg.base, &status);
    }
    
    if (handle->object.data.socket.send_buffer.buffer) {
        (void)dma_attachment_unmap(&handle->object.data.socket.send_buffer);
        (void)dma_detach(&handle->object.data.socket.send_buffer);
    }
    
    if (handle->object.data.socket.recv_buffer.buffer) {
        (void)dma_attachment_unmap(&handle->object.data.socket.recv_buffer);
        (void)dma_detach(&handle->object.data.socket.recv_buffer);
    }
    return status;
}

oserr_t stdio_net_op_inherit(stdio_handle_t* handle)
{
    oserr_t status1, status2;
    uuid_t     send_buffer_handle = handle->object.data.socket.send_buffer.handle;
    uuid_t     recv_buffer_handle = handle->object.data.socket.recv_buffer.handle;
    
    // When we inherit a socket from another application, we must reattach
    // the handle that is stored in dma_attachment.
    status1 = dma_attach(send_buffer_handle, &handle->object.data.socket.send_buffer);
    status2 = dma_attach(recv_buffer_handle, &handle->object.data.socket.recv_buffer);
    if (status1 != OsOK || status2 != OsOK) {
        return status1 != OsOK ? status1 : status2;
    }
    
    status1 = dma_attachment_map(&handle->object.data.socket.send_buffer, DMA_ACCESS_WRITE);
    if (status1 != OsOK) {
        return status1;
    }
    
    status1 = dma_attachment_map(&handle->object.data.socket.recv_buffer, DMA_ACCESS_WRITE);
    if (status1 != OsOK) {
        return status1;
    }
    return status1;
}

oserr_t stdio_net_op_ioctl(stdio_handle_t* handle, int request, va_list args)
{
    streambuffer_t* recvStream = handle->object.data.socket.recv_buffer.buffer;

    if ((unsigned int)request == FIONREAD) {
        int* bytesAvailableOut = va_arg(args, int*);
        if (bytesAvailableOut) {
            size_t bytesAvailable;
            streambuffer_get_bytes_available_in(recvStream, &bytesAvailable);
            *bytesAvailableOut = (int)bytesAvailable;
        }
        return OsOK;
    }
    return OsNotSupported;
}

void stdio_get_net_operations(stdio_ops_t* ops)
{
    ops->inherit = stdio_net_op_inherit;
    ops->read    = stdio_net_op_read;
    ops->write   = stdio_net_op_write;
    ops->seek    = stdio_net_op_seek;
    ops->resize  = stdio_net_op_resize;
    ops->ioctl   = stdio_net_op_ioctl;
    ops->close   = stdio_net_op_close;
}
