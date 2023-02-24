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

#include "internal/_io.h"
#include "internal/_ipc.h"
#include "inet/local.h"
#include "inet/socket.h"
#include "errno.h"
#include "os/mollenos.h"

int listen(int iod, int backlog)
{
    struct vali_link_message msg    = VALI_MSG_INIT_HANDLE(GetNetService());
    stdio_handle_t*          handle = stdio_handle_get(iod);
    oserr_t                  status;
    
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
    
    sys_socket_listen(GetGrachtClient(), &msg.base, handle->object.handle, backlog);
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_socket_listen_result(GetGrachtClient(), &msg.base, &status);
    if (status != OS_EOK) {
        OsErrToErrNo(status);
        return -1;
    }
    return 0;
}
