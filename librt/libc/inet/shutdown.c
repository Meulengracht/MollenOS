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
 */

#include <errno.h>
#include <internal/_io.h>
#include <inet/socket.h>
#include <os/services/net.h>
#include <os/mollenos.h>

enum OSocketShutdownType
__ToOSSocketShutdownType(int how)
{
    switch (how) {
        case SHUT_RD: return OSSOCKET_SHUTDOWN_RECV;
        case SHUT_WR: return OSSOCKET_SHUTDOWN_SEND;
        case SHUT_RDWR: return OSSOCKET_SHUTDOWN_RECVSEND;
        default: return -1; // Invalid value
    }
}

int shutdown(int iod, int how)
{
    stdio_handle_t* handle = stdio_handle_get(iod);

    if (how <= 0 || how > SHUT_RDWR) {
        _set_errno(EINVAL);
        return -1;
    }

    if (stdio_handle_signature(handle) != NET_SIGNATURE) {
        _set_errno(ENOTSOCK);
        return -1;
    }

    return OsErrToErrNo(
        OSSocketShutdown(
            &handle->OSHandle,
            __ToOSSocketShutdownType(how)
        )
    );
}
