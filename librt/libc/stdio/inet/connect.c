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
 * The connect() system call connects the socket referred to by the file descriptor sockfd to the 
 * address specified by addr. The addrlen argument specifies the size of addr. The format of 
 * the address in addr is determined by the address space of the socket sockfd; see socket(2) for further details.
 * If the socket sockfd is of type SOCK_DGRAM then addr is the address to which datagrams are sent 
 * by default, and the only address from which datagrams are received. If the socket is of type 
 * SOCK_STREAM or SOCK_SEQPACKET, this call attempts to make a connection to the socket that is 
 * bound to the address specified by addr.
 *
 * Generally, connection-based protocol sockets may successfully connect() only once; 
 * connectionless protocol sockets may use connect() multiple times to change their association. 
 * Connectionless sockets may dissolve the association by connecting to an address with the 
 * sa_family member of sockaddr set to AF_UNSPEC.
 */

#include <ddk/services/net.h>
#include <errno.h>
#include <internal/_io.h>
#include <inet/local.h>
#include <os/mollenos.h>
#include <string.h>

int connect(int iod, const struct sockaddr* address, socklen_t address_length)
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
    
    // Per the specification, if the type is DGRAM/RAW, then we simply provide a default
    // address for the send calls
    if (handle->object.data.socket.type == SOCK_DGRAM
        || handle->object.data.socket.type == SOCK_RAW) {
        if (address->sa_family == AF_UNSPEC) {
            handle->object.data.socket.flags &= ~(SOCKET_CONNECTED);
        }
        else {
            memcpy(&handle->object.data.socket.default_address, address, address_length);
            handle->object.data.socket.flags |= SOCKET_CONNECTED;
        }
    }
    else {
        if (handle->object.data.socket.flags & (SOCKET_CONNECTED | SOCKET_PASSIVE)) {
            _set_errno(EISCONN);
            return -1;
        }
        
        status = ConnectSocket(handle->object.handle, address);
        if (status != OsSuccess) {
            OsStatusToErrno(status);
            return -1;
        }
        
        // So if we reach here we can continue the connection process, update
        // the socket state to reflect the new state
        handle->object.data.socket.flags |= SOCKET_CONNECTED;
    }
    return 0;
}
