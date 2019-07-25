/* MollenOS
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
 * - Standard IO socket operation implementations.
 */

#include <internal/_io.h>
#include <internal/_syscalls.h>
#include <errno.h>
#include <os/mollenos.h>

OsStatus_t stdio_net_op_read(stdio_handle_t* handle, void* buffer, size_t length, size_t* bytes_read)
{
    intmax_t num_bytes = recv(handle->fd, buffer, length, 0);
    if (num_bytes >= 0) {
        *bytes_read = (size_t)num_bytes;
        return OsSuccess;
    }
    return OsError;
}

OsStatus_t stdio_net_op_write(stdio_handle_t* handle, const void* buffer, size_t length, size_t* bytes_written)
{
    intmax_t num_bytes = send(handle->fd, buffer, length, 0);
    if (num_bytes >= 0) {
        *bytes_written = (size_t)num_bytes;
        return OsSuccess;
    }
    return OsError;
}

OsStatus_t stdio_net_op_seek(stdio_handle_t* handle, int origin, off64_t offset, long long* position_out)
{
    // It is not possible to seek in sockets.
    return OsNotSupported;
}

OsStatus_t stdio_net_op_resize(stdio_handle_t* handle, long long resize_by)
{
    // TODO: Implement resizing of socket buffers
    return OsNotSupported;
}

OsStatus_t stdio_net_op_close(stdio_handle_t* handle, int options)
{
    // Reuse existing system calls as the socket structure is a combination
    // of different services. What we should do here is actually query the size
    // of the queues before calling free, but currently all queues are 4k
    MemoryFree(handle->object.data.socket.recv_queue, 0x1000);
    return Syscall_DestroyHandle(handle->object.handle);
}

OsStatus_t stdio_net_op_inherit(stdio_handle_t* handle)
{
    OsStatus_t status = Syscall_InheritSocket(handle->object.handle, 
        &handle->object.data.socket.recv_queue);
    if (status != OsSuccess) {
        return status;
    }
    
    switch (handle->object.data.socket.domain) {
        case AF_LOCAL: {
            get_socket_ops_local(&handle->object.data.socket.domain_ops);
        } break;
        
        case AF_INET:
        case AF_INET6: {
            get_socket_ops_inet(&handle->object.data.socket.domain_ops);
        } break;
        
        default: {
            get_socket_ops_null(&handle->object.data.socket.domain_ops);
        } break;
    }
    return status;
}

void stdio_get_net_operations(stdio_ops_t* ops)
{
    ops->inherit = stdio_net_op_inherit;
    ops->read    = stdio_net_op_read;
    ops->write   = stdio_net_op_write;
    ops->seek    = stdio_net_op_seek;
    ops->resize  = stdio_net_op_resize;
    ops->close   = stdio_net_op_close;
}
