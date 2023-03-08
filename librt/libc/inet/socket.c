/**
 * MollenOS
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Standard C Support
 * - Standard Socket IO Implementation
 */
#define __TRACE

#include "ddk/utils.h"
#include "internal/_io.h"
#include "internal/_ipc.h"
#include "inet/socket.h"

int socket(int domain, int type, int protocol)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetNetService());
    oserr_t                  oserr;
    uuid_t                   handle;
    uuid_t                   send_handle;
    uuid_t                   recv_handle;
    int                      fd;
    
    // We need to create the socket object at kernel level, as we need
    // kernel assisted functionality to support a centralized storage of
    // all system sockets. They are the foundation of the microkernel for
    // communication between processes and are needed long before anything else.
    TRACE("[socket] remote create");
    sys_socket_create(GetGrachtClient(), &msg.base, domain, type, protocol);
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_socket_create_result(GetGrachtClient(), &msg.base, &oserr, &handle, &recv_handle, &send_handle);
    if (oserr != OS_EOK) {
        ERROR("[socket] CreateSocket failed with code %u", oserr);
        (void)OsErrToErrNo(oserr);
        return -1;
    }
    
    fd = socket_create(domain, type, protocol, handle, send_handle, recv_handle);
    if (fd == -1) {
        ERROR("[socket] socket_create failed");
        return -1;
    }
    return fd;
}

oserr_t stdio_net_op_read(stdio_handle_t* handle, void* buffer, size_t length, size_t* bytes_read)
{
    intmax_t num_bytes = recv(handle->fd, buffer, length, 0);
    if (num_bytes >= 0) {
        *bytes_read = (size_t)num_bytes;
        return OS_EOK;
    }
    return OS_EUNKNOWN;
}

oserr_t stdio_net_op_write(stdio_handle_t* handle, const void* buffer, size_t length, size_t* bytes_written)
{
    intmax_t num_bytes = send(handle->fd, buffer, length, 0);
    if (num_bytes >= 0) {
        *bytes_written = (size_t)num_bytes;
        return OS_EOK;
    }
    return OS_EUNKNOWN;
}

oserr_t stdio_net_op_seek(stdio_handle_t* handle, int origin, off64_t offset, long long* position_out)
{
    // It is not possible to seek in sockets.
    return OS_ENOTSUPPORTED;
}

oserr_t stdio_net_op_resize(stdio_handle_t* handle, long long resize_by)
{
    // TODO: Implement resizing of socket buffers
    return OS_ENOTSUPPORTED;
}

oserr_t stdio_net_op_close(stdio_handle_t* handle, int options)
{
    oserr_t status = OS_EOK;

    if (options & STDIO_CLOSE_FULL) {
        struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetNetService());
        sys_socket_close(GetGrachtClient(), &msg.base, handle->object.handle,
                         SYS_CLOSE_OPTIONS_DESTROY);
        sys_socket_close_result(GetGrachtClient(), &msg.base, &status);
    }

    if (handle->object.data.socket.send_buffer.Buffer) {
        (void)SHMUnmap(&handle->object.data.socket.send_buffer);
        (void)SHMDetach(&handle->object.data.socket.send_buffer);
    }

    if (handle->object.data.socket.recv_buffer.Buffer) {
        (void)SHMUnmap(&handle->object.data.socket.recv_buffer);
        (void)SHMDetach(&handle->object.data.socket.recv_buffer);
    }
    return status;
}

oserr_t stdio_net_op_inherit(stdio_handle_t* handle)
{
    oserr_t status1, status2;
    uuid_t  send_buffer_handle = handle->object.data.socket.send_buffer.ID;
    uuid_t  recv_buffer_handle = handle->object.data.socket.recv_buffer.ID;

    // When we inherit a socket from another application, we must reattach
    // the handle that is stored in dma_attachment.
    status1 = SHMAttach(send_buffer_handle, &handle->object.data.socket.send_buffer);
    status2 = SHMAttach(recv_buffer_handle, &handle->object.data.socket.recv_buffer);
    if (status1 != OS_EOK || status2 != OS_EOK) {
        return status1 != OS_EOK ? status1 : status2;
    }

    status1 = SHMMap(
            &handle->object.data.socket.send_buffer,
            0, handle->object.data.socket.send_buffer.Capacity,
            SHM_ACCESS_READ | SHM_ACCESS_WRITE
    );
    if (status1 != OS_EOK) {
        return status1;
    }

    status1 = SHMMap(
            &handle->object.data.socket.recv_buffer,
            0, handle->object.data.socket.recv_buffer.Capacity,
            SHM_ACCESS_READ | SHM_ACCESS_WRITE
    );
    if (status1 != OS_EOK) {
        return status1;
    }
    return status1;
}

oserr_t stdio_net_op_ioctl(stdio_handle_t* handle, int request, va_list args)
{
    streambuffer_t* recvStream = handle->object.data.socket.recv_buffer.Buffer;

    if ((unsigned int)request == FIONREAD) {
        int* bytesAvailableOut = va_arg(args, int*);
        if (bytesAvailableOut) {
            size_t bytesAvailable;
            streambuffer_get_bytes_available_in(recvStream, &bytesAvailable);
            *bytesAvailableOut = (int)bytesAvailable;
        }
        return OS_EOK;
    }
    return OS_ENOTSUPPORTED;
}
