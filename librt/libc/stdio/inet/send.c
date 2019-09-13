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

#include <errno.h>
#include <internal/_io.h>
#include <inet/local.h>
#include <os/mollenos.h>

static intmax_t perform_send(stdio_handle_t* handle, const struct msghdr* msg, int flags)
{
    intmax_t numbytes = 0;
    int      i;
    
    for (i = 0; i < msg->msg_iovlen; i++) {
        struct iovec* iov = &msg->msg_iov[i];
        size_t        byte_count;
        OsStatus_t    status;
        
        byte_count = ringbuffer_write(handle->object.data.socket.send_queue, 
            iov->iov_base, iov->iov_len);
        status = WriteSocket((struct sockaddr_lc*)msg->msg_name, 
            iov->iov_base, iov->iov_len, &byte_count);
        if (status != OsSuccess) {
            OsStatusToErrno(status);
            break;
        }
        numbytes += byte_count;
    }
    return numbytes;
}

intmax_t sendmsg(int iod, const struct msghdr* msg_hdr, int flags)
{
    stdio_handle_t* handle = stdio_handle_get(iod);
    
    if (!handle) {
        _set_errno(EBADF);
        return -1;
    }
    
    if (!msg_hdr) {
        _set_errno(EINVAL);
        return -1;
    }
    
    if (handle->object.type != STDIO_HANDLE_SOCKET) {
        _set_errno(ENOTSOCK);
        return -1;
    }
    
    if (handle->object.data.socket.flags & SOCKET_WRITE_DISABLED) {
        _set_errno(ESHUTDOWN);
        return -1;
    }
    
    if (msg_hdr->msg_name == NULL) {
        if (!(handle->object.data.socket.flags & SOCKET_CONNECTED)) {
            _set_errno(EDESTADDRREQ);
            return -1;
        }
        struct msghdr* msg_ptr = (struct msghdr*)msg_hdr;
        msg_ptr->msg_name    = &handle->object.data.socket.default_address;
        msg_ptr->msg_namelen = handle->object.data.socket.default_address.__ss_len;
    }
    
    return perform_send(handle, msg_hdr, flags);
}

intmax_t sendto(int iod, const void* buffer, size_t length, int flags, const struct sockaddr* address, socklen_t address_length)
{
    struct iovec  iov = { .iov_base = (void*)buffer, .iov_len = length };
    struct msghdr msg = {
        .msg_name       = (struct sockaddr*)address,
        .msg_namelen    = address_length,
        .msg_iov        = &iov,
        .msg_iovlen     = 1,
        .msg_control    = NULL,
        .msg_controllen = 0,
        .msg_flags      = flags
    };
    return sendmsg(iod, &msg, flags);
}

intmax_t send(int iod, const void* buffer, size_t length, int flags)
{
    return sendto(iod, buffer, length, flags, NULL, 0);
}
