/**
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
 */

#include <errno.h>
#include <internal/_io.h>
#include <inet/local.h>
#include <os/services/net.h>
#include <os/mollenos.h>

intmax_t sendmsg(int iod, const struct msghdr* msg_hdr, int flags)
{
    stdio_handle_t* handle = stdio_handle_get(iod);
    oserr_t         oserr;
    size_t          bytesSent;

    if (!msg_hdr) {
        _set_errno(EINVAL);
        return -1;
    }

    if (stdio_handle_signature(handle) != NET_SIGNATURE) {
        _set_errno(ENOTSOCK);
        return -1;
    }

    oserr = OSSocketSend(&handle->OSHandle, msg_hdr, flags, &bytesSent);
    if (oserr != OS_EOK) {
        return (intmax_t)OsErrToErrNo(oserr);
    }
    return (intmax_t)bytesSent;
}

intmax_t sendto(int iod, const void* buffer, size_t length, int flags, const struct sockaddr* address, socklen_t address_length)
{
    struct iovec  iov = { .iov_base = (void*)buffer, .iov_len = length };
    struct msghdr msg = {
        .msg_name       = (struct sockaddr*)address,
        .msg_namelen    = address_length,
        .msg_iov        = &iov,
        .msg_iovlen     = 1,
        .msg_control    = NULL,
        .msg_controllen = 0,
        .msg_flags      = flags
    };
    return sendmsg(iod, &msg, flags);
}

intmax_t send(int iod, const void* buffer, size_t length, int flags)
{
    return sendto(iod, buffer, length, flags, NULL, 0);
}
