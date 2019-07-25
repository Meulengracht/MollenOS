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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Standard C Support
 * - Standard Socket IO Implementation
 */

#include <internal/_io.h>
#include <internal/_syscalls.h>
#include <inet/local.h>
#include <os/mollenos.h>

static intmax_t local_recv(stdio_handle_t* handle, struct msghdr* msg, int flags)
{
    intmax_t numbytes = 0;
    int      i;
    
    for (i = 0; i < msg->msg_iovlen; i++) {
        struct iovec* iov = &msg->msg_iov[i];
        
        numbytes += ringbuffer_read(handle->object.data.socket.recv_queue, 
            iov->iov_base, iov->iov_len);
    }
    return numbytes;
}

static intmax_t local_send(stdio_handle_t* handle, const struct msghdr* msg, int flags)
{
    intmax_t numbytes = 0;
    int      i;
    
    for (i = 0; i < msg->msg_iovlen; i++) {
        struct iovec* iov = &msg->msg_iov[i];
        size_t        byte_count;
        OsStatus_t    status;
        
        status = Syscall_WriteSocket((struct sockaddr_lc*)msg->msg_name, 
            iov->iov_base, iov->iov_len, &byte_count);
        if (status != OsSuccess) {
            OsStatusToErrno(status);
            break;
        }
        numbytes += byte_count;
    }
    return numbytes;
}

void get_socket_ops_local(struct socket_ops* ops)
{
    ops->recv = local_recv;
    ops->send = local_send;
}
