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
 * The accept() system call is used with connection-based socket types (SOCK_STREAM, SOCK_SEQPACKET). 
 * It extracts the first connection request on the queue of pending connections for the listening socket, 
 * sockfd, creates a new connected socket, and returns a new file descriptor referring to that socket. 
 * The newly created socket is not in the listening state. The original socket sockfd is unaffected by this call.
 * 
 * The argument sockfd is a socket that has been created with socket(2), bound to a local address with bind(2), 
 * and is listening for connections after a listen(2).
 * 
 * The argument addr is a pointer to a sockaddr structure. This structure is filled in with 
 * the address of the peer socket, as known to the communications layer. 
 * The exact format of the address returned addr is determined by the socket's address 
 * family (see socket(2) and the respective protocol man pages). When addr is NULL, 
 * nothing is filled in; in this case, addrlen is not used, and should also be NULL.
 * 
 * The addrlen argument is a value-result argument: the caller must initialize it to contain the 
 * size (in bytes) of the structure pointed to by addr; on return it will contain the actual size
 * of the peer address.
 * 
 * The returned address is truncated if the buffer provided is too small; in this case, 
 * addrlen will return a value greater than was supplied to the call.
 * 
 * If no pending connections are present on the queue, and the socket is not marked as 
 * nonblocking, accept() blocks the caller until a connection is present. If the socket is marked 
 * nonblocking and no pending connections are present on the queue, accept() fails with the error 
 * EAGAIN or EWOULDBLOCK.
 * 
 * In order to be notified of incoming connections on a socket, you can use select(2) or poll(2). 
 * A readable event will be delivered when a new connection is attempted and you may then call accept() 
 * to get a socket for that connection. Alternatively, you can set the socket to deliver SIGIO when 
 * activity occurs on a socket; see socket(7) for details.
 * 
 * For certain protocols which require an explicit confirmation, such as DECNet, accept() can be thought 
 * of as merely dequeuing the next connection request and not implying confirmation. Confirmation can be 
 * implied by a normal read or write on the new file descriptor, and rejection can be implied by closing 
 * the new socket. Currently only DECNet has these semantics on Linux.
 */

#include "sys_process_service_client.h"
#include "ddk/service.h"
#include "errno.h"
#include "gracht/link/vali.h"
#include "internal/_io.h"
#include "internal/_utils.h"
#include "inet/local.h"
#include "inet/socket.h"
#include "os/mollenos.h"

int accept(int iod, struct sockaddr* address, socklen_t* address_length)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetNetService());
    stdio_handle_t*          handle = stdio_handle_get(iod);
    uuid_t                   socket_handle;
    uuid_t                   send_handle;
    uuid_t                   recv_handle;
    oserr_t               status;
    int                      accept_iod;
    
    if (!handle) {
        _set_errno(EBADF);
        return -1;
    }
    
    if (handle->object.type != STDIO_HANDLE_SOCKET) {
        _set_errno(ENOTSOCK);
        return -1;
    }
    
    if (handle->object.data.socket.type != SOCK_SEQPACKET &&
        handle->object.data.socket.type != SOCK_STREAM) {
        _set_errno(ESOCKTNOSUPPORT);
        return -1;        
    }
    
    sys_socket_accept(GetGrachtClient(), &msg.base, handle->object.handle);
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_socket_accept_result(GetGrachtClient(), &msg.base, &status, (uint8_t*)address, *address_length,
        &socket_handle, &recv_handle, &send_handle);
    if (status != OS_EOK) {
        OsErrToErrNo(status);
        return -1;
    }
    
    accept_iod = socket_create(handle->object.data.socket.domain,
        handle->object.data.socket.type, handle->object.data.socket.protocol,
        socket_handle, send_handle, recv_handle);
    if (accept_iod == -1) {
        return -1;
    }
    
    *address_length = (socklen_t)(uint32_t)address->sa_len;
    return accept_iod;
}
