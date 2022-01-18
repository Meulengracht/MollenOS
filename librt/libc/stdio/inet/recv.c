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
#include <os/mollenos.h>
#include <string.h>

static inline unsigned int
get_streambuffer_flags(int flags)
{
    unsigned int sb_options = 0;
    
    if (flags & MSG_OOB) {
        sb_options |= STREAMBUFFER_PRIORITY;
    }
    
    if (flags & MSG_DONTWAIT) {
        sb_options |= STREAMBUFFER_NO_BLOCK;
    }
    
    if (!(flags & MSG_WAITALL)) {
        sb_options |= STREAMBUFFER_ALLOW_PARTIAL;
    }
    
    if (flags & MSG_PEEK) {
        sb_options |= STREAMBUFFER_PEEK;
    }
    
    return sb_options;
}

static intmax_t perform_recv_stream(streambuffer_t* stream, struct msghdr* msg, int flags, unsigned int sb_options)
{
    intmax_t numbytes = 0;
    int      i;
    
    for (i = 0; i < msg->msg_iovlen; i++) {
        struct iovec* iov = &msg->msg_iov[i];
        TRACE("[libc] [socket] [recv] reading %" PRIuIN "bytes", iov->iov_len);
        numbytes += streambuffer_stream_in(stream, iov->iov_base, iov->iov_len, sb_options);
        TRACE("[libc] [socket] [recv] read %i bytes", numbytes);
        if (numbytes < iov->iov_len) {
            if (!(flags & MSG_DONTWAIT)) {
                _set_errno(EPIPE);
                numbytes = -1;
            }
            break;
        }
    }
    return numbytes;
}

// Valid flags for recv are
// MSG_OOB          (No OOB support)
// MSG_PEEK         (No peek support)
// MSG_WAITALL      (Supported)
// MSG_DONTWAIT     (Supported)
// MSG_NOSIGNAL     (Ignored on Vali)
// MSG_CMSG_CLOEXEC (Ignored on Vali)
static intmax_t perform_recv(stdio_handle_t* handle, struct msghdr* msg, int flags)
{
    streambuffer_t*   stream     = handle->object.data.socket.recv_buffer.buffer;
    intmax_t          numbytes   = 0;
    unsigned int      sb_options = get_streambuffer_flags(flags);
    struct packethdr  packet;
    int               i;
    unsigned int      base, state;

    // In case of stream sockets we simply just read as many bytes as requested
    // or available and return, unless WAITALL has been specified.
    if (handle->object.data.socket.type == SOCK_STREAM) {
        return perform_recv_stream(stream, msg, flags, sb_options);
    }
    
    // Reading an packet is an atomic action and the entire packet must be read
    // at once. So don't support STREAMBUFFER_ALLOW_PARTIAL
    sb_options &= ~(STREAMBUFFER_ALLOW_PARTIAL);
    
    numbytes = streambuffer_read_packet_start(stream, sb_options, &base, &state);
    if (numbytes < sizeof(struct packethdr)) {
        if (!numbytes) {
            if (flags & MSG_WAITALL) {
                _set_errno(EPIPE);
                return -1;
            }
            return 0;
        }
        
        // If we read an invalid number of bytes then something evil happened.
        streambuffer_read_packet_end(stream, base, numbytes);
        _set_errno(EPIPE);
        return -1;
    }
    
    // Reset the message flag so we can properly report status of message.
    streambuffer_read_packet_data(stream, &packet, sizeof(struct packethdr), &state);
    msg->msg_flags = packet.flags;
    
    // Handle the source address that is given in the packet
    if (packet.addresslen && msg->msg_name && msg->msg_namelen) {
        size_t bytes_to_copy    = MIN(msg->msg_namelen, packet.addresslen);
        size_t bytes_to_discard = packet.addresslen - bytes_to_copy;
        
        streambuffer_read_packet_data(stream, msg->msg_name, bytes_to_copy, &state);
        state += bytes_to_discard; // hack
    }
    else {
        state += packet.addresslen; // discard, hack
        msg->msg_namelen = 0;
    }
    
    // Handle control data, and set the appropriate flags and update the actual
    // length read of control data if it is shorter than the buffer provided.
    if (packet.controllen && msg->msg_control && msg->msg_controllen) {
        size_t bytes_to_copy    = MIN(msg->msg_controllen, packet.controllen);
        size_t bytes_to_discard = packet.controllen - bytes_to_copy;
        
        streambuffer_read_packet_data(stream, msg->msg_control, bytes_to_copy, &state);
        state += bytes_to_discard; // hack
    }
    else {
        state += packet.controllen; // discard, hack
        msg->msg_controllen = 0;
    }
    
    if (packet.controllen > msg->msg_controllen) {
        msg->msg_flags |= MSG_CTRUNC;
    }
    
    // Finally read the payload data
    if (packet.payloadlen) {
        size_t bytes_remaining = packet.payloadlen;
        int    iov_not_filled  = 0;
        
        for (i = 0; i < msg->msg_iovlen && bytes_remaining; i++) {
            struct iovec* iov = &msg->msg_iov[i];
            if (!iov->iov_len) {
                break;
            }
            
            size_t bytes_to_copy = MIN(iov->iov_len, bytes_remaining);
            if (iov->iov_len > bytes_to_copy) {
                iov_not_filled = 1;
            }
            
            streambuffer_read_packet_data(stream, iov->iov_base, bytes_to_copy, &state);
            bytes_remaining -= bytes_to_copy;
        }
        streambuffer_read_packet_end(stream, base, numbytes);
        
        // The first special case is when there is more data available than we
        // requested, that means we simply trunc the data.
        if (bytes_remaining) {
            msg->msg_flags |= MSG_TRUNC;
        }
        
        // The second case is a lot more complex, that means we are missing data
        // though we requested more, if WAITALL is set, then we need to keep reading
        // untill we read all the data
        else if (!bytes_remaining && iov_not_filled && (flags & MSG_WAITALL)) {
            // However on message-based sockets, we must read datagrams as atomic
            // operations, and thus MSG_WAITALL has no effect as this is effectively
            // the standard mode of operation.
        }
    }
    else {
        streambuffer_read_packet_end(stream, base, numbytes);
        for (i = 0; i < msg->msg_iovlen; i++) {
            struct iovec* iov = &msg->msg_iov[i];
            iov->iov_len = 0;
        }
    }
    return packet.payloadlen;
}

intmax_t recvmsg(int iod, struct msghdr* msg_hdr, int flags)
{
    stdio_handle_t* handle = stdio_handle_get(iod);
    streambuffer_t* stream;
    intmax_t        numbytes;
    
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
    
    stream = handle->object.data.socket.recv_buffer.buffer;
    if (streambuffer_has_option(stream, STREAMBUFFER_DISABLED)) {
        _set_errno(ESHUTDOWN);
        return 0; // Should return 0
    }
    
    numbytes = perform_recv(handle, msg_hdr, flags);
    
    // Fill in the source address if one was provided already, and overwrite
    // the one provided by the packet.
    if (numbytes > 0 && msg_hdr->msg_name != NULL) {
        if (handle->object.data.socket.default_address.__ss_family != AF_UNSPEC) {
            memcpy(msg_hdr->msg_name, &handle->object.data.socket.default_address,
                handle->object.data.socket.default_address.__ss_len);
            msg_hdr->msg_namelen = handle->object.data.socket.default_address.__ss_len;
        }
    }
    return numbytes;
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
