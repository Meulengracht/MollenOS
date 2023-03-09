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
 * When a socket is created with socket(2), it exists in a name space (address family) but has no address assigned to it. 
 * bind() assigns the address specified by addr to the socket referred to by the file descriptor sockfd. addrlen specifies the 
 * size, in bytes, of the address structure pointed to by addr. Traditionally, this operation is called "assigning a name to a socket".
 * 
 * It is normally necessary to assign a local address using bind() before a SOCK_STREAM socket may receive connections (see accept(2)).
 */

#include <errno.h>
#include <ddk/utils.h>
#include <internal/_io.h>
#include <inet/local.h>
#include <inet/socket.h>
#include <os/mollenos.h>
#include <os/services/net.h>

int bind(int iod, const struct sockaddr* address, socklen_t address_length)
{
    stdio_handle_t* handle = stdio_handle_get(iod);

    if (stdio_handle_signature(handle) != NET_SIGNATURE) {
        _set_errno(ENOTSOCK);
        return -1;
    }

    return OsErrToErrNo(
            OSSocketBind(
                    &handle->OSHandle,
                    address,
                    address_length
            )
    );
}
