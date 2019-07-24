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
 * 
 * listen() marks the socket referred to by sockfd as a passive socket, that is, as a socket 
 * that will be used to accept incoming connection requests using accept(2).
 * The sockfd argument is a file descriptor that refers to a socket of type SOCK_STREAM or SOCK_SEQPACKET.
 * 
 * The backlog argument defines the maximum length to which the queue of pending connections for sockfd may grow. 
 * If a connection request arrives when the queue is full, the client may receive an error with an indication 
 * of ECONNREFUSED or, if the underlying protocol supports retransmission, the request may be ignored so that 
 * a later reattempt at connection succeeds.
 */

#include <internal/_io.h>
#include <internal/_syscalls.h>
#include <inet/local.h>
#include <errno.h>
#include <os/mollenos.h>

int listen(int iod, int backlog)
{
    stdio_handle_t* handle = stdio_handle_get(iod);
    
    if (!handle) {
        _set_errno(EBADF);
        return -1;
    }
    
    if (handle->object.type != STDIO_HANDLE_SOCKET) {
        _set_errno(ENOTSOCK);
        return -1;
    }
    
    if (handle->object.data.socket.type != SOCK_STREAM &&
        handle->object.data.socket.type != SOCK_SEQPACKET) {
        _set_errno(ESOCKTNOSUPPORT);
        return -1;
    }
    
    if (handle->object.data.socket.state != socket_bound) {
        if (handle->object.data.socket.state == socket_connected ||
            handle->object.data.socket.state == socket_listener) {
            _set_errno(EISCONN);
        }
        else {
            _set_errno(EHOSTUNREACH);
        }
        return -1;
    }
    
    switch (handle->object.data.socket.domain) {
        case AF_LOCAL: {
            // Local sockets on the pc does not support connection-oriented mode yet.
            // TODO: Implement connected sockets locally. This is only to provide transparency
            // to userspace application, the functionality is not required.
            _set_errno(ENOTSUP);
            return -1;
        } break;
        
        case AF_INET:
        case AF_INET6: {
            _set_errno(ENOTSUP);
            return -1;
        } break;
        
        default: {
            _set_errno(ENOTSUP);
            return -1;
        }
    };
    
    // So if we reach here we can continue the listener process, update
    // the socket state to reflect the new state
    handle->object.data.socket.state = socket_listener;
    return 0;
}
