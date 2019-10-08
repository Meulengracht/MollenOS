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
#include <string.h>

// Valid flags for send are
// MSG_OOB          (No OOB support)
// MSG_DONTROUTE    (Dunno what this is)
// MSG_WAITALL      (Supported)
// MSG_DONTWAIT     (Supported)
// MSG_NOSIGNAL     (Ignored on Vali)
// MSG_CMSG_CLOEXEC (Ignored on Vali)
static intmax_t perform_send(stdio_handle_t* handle, const struct msghdr* msg, int flags)
{
    unsigned int     sb_options = 0;
    intmax_t         numbytes   = 0;
    size_t           total_len  = msg->msg_namelen + msg->msg_controllen;
    streambuffer_t*  stream     = handle->object.data.socket.send_buffer.buffer;
    struct packethdr packet;
    size_t           avail_len;
    unsigned int     base, state;
    int              i;
    
    if (flags & MSG_OOB) {
        sb_options |= STREAMBUFFER_PRIORITY;
    }
    
    if (!(flags & MSG_WAITALL)) {
        sb_options |= STREAMBUFFER_ALLOW_PARTIAL;
    }
    
    if (flags & MSG_DONTWAIT) {
        sb_options |= STREAMBUFFER_NO_BLOCK;
    }
    
    // For stream sockets we don't need to build the packet header. Simply just
    // write all the bytes possible to the send socket and return
    if (handle->object.data.socket.type == SOCK_STREAM) {
        total_len = 0;
        for (i = 0; i < msg->msg_iovlen; i++) {
            struct iovec* iov            = &msg->msg_iov[i];
            size_t        bytes_streamed = streambuffer_stream_out(stream, 
                iov->iov_base, iov->iov_len, sb_options);
            if (!bytes_streamed) {
                break;
            }
            total_len += bytes_streamed;
        }
        return total_len;
    }
    
    // Otherwise we must build a packet, to do this we need to know the entire
    // length of the message before committing.
    for (i = 0; i < msg->msg_iovlen; i++) {
        total_len += msg->msg_iov[i].iov_len;
    }
    
    packet.flags = flags & (MSG_OOB | MSG_DONTROUTE);
    packet.controllen = msg->msg_controllen;
    packet.addresslen = msg->msg_namelen;
    packet.payloadlen = total_len - msg->msg_namelen - msg->msg_controllen;
    
    avail_len = streambuffer_write_packet_start(stream, 
        total_len, sb_options, &base, &state);
    if (avail_len < total_len) {
        if (!(flags & MSG_DONTWAIT)) {
            _set_errno(EPIPE);
            return -1;
        }
        return 0;
    }
    
    streambuffer_write_packet_data(stream, &packet, sizeof(struct packethdr), &state);
    streambuffer_write_packet_data(stream, msg->msg_name, msg->msg_namelen, &state);
    if (msg->msg_controllen) {
        streambuffer_write_packet_data(stream, msg->msg_control, msg->msg_controllen, &state);
    }
    total_len = sizeof(struct packethdr) + packet.controllen + packet.addresslen;
    for (i = 0; i < msg->msg_iovlen; i++) {
        struct iovec* iov = &msg->msg_iov[i];
        size_t        byte_count = MIN(avail_len - total_len, iov->iov_len);
        if (!byte_count) {
            break;
        }
        
        streambuffer_write_packet_data(stream, iov->iov_base, iov->iov_len, &state);
        
        total_len += byte_count;
        numbytes  += byte_count;
    }
    streambuffer_write_packet_end(stream, base, avail_len);
    stdio_handle_activity(handle, IOEVTIN);
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
    
    // We must return ESHUTDOWN if the sending socket has been requested shut
    // on this socket handle.
    if (handle->object.data.socket.flags & SOCKET_WRITE_DISABLED) {
        _set_errno(ESHUTDOWN);
        return -1;
    }
    
    // Stream sockets must always be in a connected state, and thus ignore the
    // destination address parameter. If one is provided we are allowed to return
    // EISCONN.
    if (handle->object.data.socket.type == SOCK_STREAM) {
        if (!(handle->object.data.socket.flags & SOCKET_CONNECTED)) {
            _set_errno(ENOTCONN);
            return -1;
        }
        
        if (msg_hdr->msg_name != NULL) {
            _set_errno(EISCONN);
            return -1;
        }
    }
    
    // Lastly, make sure we actually have a destination address. For the rest of 
    // the socket types, we use the stored address ('connected address'), or the
    // one provided.
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
