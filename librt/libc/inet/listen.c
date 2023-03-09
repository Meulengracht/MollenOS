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
 * listen() marks the socket referred to by sockfd as a passive socket, that is, as a socket 
 * that will be used to accept incoming connection requests using accept(2).
 * The sockfd argument is a file descriptor that refers to a socket of type SOCK_STREAM or SOCK_SEQPACKET.
 * 
 * The backlog argument defines the maximum length to which the queue of pending connections for sockfd may grow. 
 * If a connection request arrives when the queue is full, the client may receive an error with an indication 
 * of ECONNREFUSED or, if the underlying protocol supports retransmission, the request may be ignored so that 
 * a later reattempt at connection succeeds.
 */

#include <errno.h>
#include <internal/_io.h>
#include <inet/local.h>
#include <inet/socket.h>
#include <os/mollenos.h>
#include <os/services/net.h>

int listen(int iod, int backlog)
{
    stdio_handle_t* handle = stdio_handle_get(iod);

    if (stdio_handle_signature(handle) != NET_SIGNATURE) {
        _set_errno(ENOTSOCK);
        return -1;
    }

    return OsErrToErrNo(
            OSSocketListen(&handle->OSHandle, backlog)
    );
}
