/**
 * Copyright 2023, Philip Meulengracht
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
 */

#include <errno.h>
#include <internal/_io.h>
#include <inet/local.h>
#include <os/mollenos.h>
#include <os/services/net.h>

int getpeername(int iod, struct sockaddr* address_out, socklen_t* address_length_out)
{
    stdio_handle_t* handle = stdio_handle_get(iod);
    oserr_t oserr;

    if (stdio_handle_signature(handle) != NET_SIGNATURE) {
        _set_errno(ENOTSOCK);
        return -1;
    }

    if (!address_out || !address_length_out) {
        _set_errno(EINVAL);
        return -1;
    }

    oserr = OSSocketAddress(
            &handle->OSHandle,
            SOCKET_ADDRESS_PEER,
            address_out,
            *address_length_out
    );
    if (oserr != OS_EOK) {
        return OsErrToErrNo(oserr);
    }
    *address_length_out = address_out->sa_len;
    return 0;
}
