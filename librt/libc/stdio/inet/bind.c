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
 * When a socket is created with socket(2), it exists in a name space (address family) but has no address assigned to it. 
 * bind() assigns the address specified by addr to the socket referred to by the file descriptor sockfd. addrlen specifies the 
 * size, in bytes, of the address structure pointed to by addr. Traditionally, this operation is called "assigning a name to a socket".
 * 
 * It is normally necessary to assign a local address using bind() before a SOCK_STREAM socket may receive connections (see accept(2)).
 */

#include <ddk/services/net.h>
#include <ddk/utils.h>
#include <errno.h>
#include <internal/_io.h>
#include <inet/local.h>
#include <os/mollenos.h>

int bind(int iod, const struct sockaddr* address, socklen_t address_length)
{
    stdio_handle_t* handle = stdio_handle_get(iod);
    OsStatus_t      status;
    
    if (!handle) {
        _set_errno(EBADF);
        return -1;
    }
    
    if (handle->object.type != STDIO_HANDLE_SOCKET) {
        _set_errno(ENOTSOCK);
        return -1;
    }
    
    if (handle->object.data.socket.flags & (SOCKET_BOUND | SOCKET_PASSIVE | SOCKET_CONNECTED)) {
        if (handle->object.data.socket.flags & (SOCKET_CONNECTED | SOCKET_PASSIVE)) {
            _set_errno(EISCONN);
        }
        else if (handle->object.data.socket.flags & SOCKET_BOUND) {
            _set_errno(EALREADY);
        }
        return -1;
    }
    

    status = BindSocket(handle->object.handle, address);
    if (status != OsSuccess) {
        OsStatusToErrno(status);
        return -1;
    }
    
    // So if we reach here we can continue the binding process, update
    // the socket state to reflect the new state
    handle->object.data.socket.flags |= SOCKET_BOUND;
    return 0;
}
