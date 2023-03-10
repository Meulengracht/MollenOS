/**
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
 * 
 * The recvfrom() and recvmsg() calls are used to receive messages from a socket, 
 * and may be used to receive data on a socket whether or not it is connection-oriented.
 * 
 * If src_addr is not NULL, and the underlying protocol provides the source address, 
 * this source address is filled in. When src_addr is NULL, nothing is filled in; 
 * in this case, addrlen is not used, and should also be NULL. The argument addrlen 
 * is a value-result argument, which the caller should initialize before the call to 
 * the size of the buffer associated with src_addr, and modified on return to indicate 
 * the actual size of the source address. The returned address is truncated if the buffer 
 * provided is too small; in this case, addrlen will return a value greater than was supplied to the call.
 * 
 * The recv() call is normally used only on a connected socket (see connect(2)) and 
 * is identical to recvfrom() with a NULL src_addr argument.
 * 
 * All three routines return the length of the message on successful completion. 
 * If a message is too long to fit in the supplied buffer, excess bytes may be 
 * discarded depending on the type of socket the message is received from.
 * 
 * If no messages are available at the socket, the receive calls wait for a message to arrive, 
 * unless the socket is nonblocking (see fcntl(2)), in which case the value -1 is returned and 
 * the external variable errno is set to EAGAIN or EWOULDBLOCK. The receive calls normally 
 * return any data available, up to the requested amount, rather than waiting for receipt 
 * of the full amount requested.
 */

//#define __TRACE

#include <ddk/utils.h>
#include <errno.h>
#include <internal/_io.h>
#include <inet/local.h>
#include <os/services/net.h>

intmax_t recvmsg(int iod, struct msghdr* msg_hdr, int flags)
{
    stdio_handle_t* handle = stdio_handle_get(iod);
    size_t          bytesRecv;
    oserr_t         oserr;

    if (!msg_hdr) {
        _set_errno(EINVAL);
        return -1;
    }

    if (stdio_handle_signature(handle) != NET_SIGNATURE) {
        _set_errno(ENOTSOCK);
        return -1;
    }

    oserr = OSSocketRecv(&handle->OSHandle, msg_hdr, flags, &bytesRecv);
    if (oserr != OS_EOK) {
        return (intmax_t)OsErrToErrNo(oserr);
    }
    return (intmax_t)bytesRecv;
}

intmax_t recvfrom(int iod, void* buffer, size_t length, int flags, struct sockaddr* address_out, socklen_t* address_length_out)
{
    struct iovec  iov = { .iov_base = buffer, .iov_len = length };
    struct msghdr msg = {
        .msg_name       = address_out,
        .msg_namelen    = (address_length_out != NULL) ? *address_length_out : 0,
        .msg_iov        = &iov,
        .msg_iovlen     = 1,
        .msg_control    = NULL,
        .msg_controllen = 0,
        .msg_flags      = flags
    };
    intmax_t bytes_recv = recvmsg(iod, &msg, flags);
    if (address_length_out && msg.msg_namelen != *address_length_out) {
        *address_length_out = msg.msg_namelen;
    }
    return bytes_recv;
}

intmax_t recv(int iod, void* buffer, size_t length, int flags)
{
    socklen_t addr_len = 0;
    return recvfrom(iod, buffer, length, flags, NULL, &addr_len);
}
