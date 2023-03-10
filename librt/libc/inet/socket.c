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
#define __TRACE

#include <ddk/utils.h>
#include <internal/_io.h>
#include <inet/socket.h>
#include <ioctl.h>
#include <os/handle.h>
#include <os/services/net.h>

static oserr_t __net_read(stdio_handle_t*, void*, size_t, size_t*);
static oserr_t __net_write(stdio_handle_t*, const void*, size_t, size_t*);
static oserr_t __net_ioctl(stdio_handle_t*, int, va_list);

stdio_ops_t g_netOps = {
        .read = __net_read,
        .write = __net_write,
        .ioctl = __net_ioctl
};

int socket(int domain, int type, int protocol)
{
    stdio_handle_t* handle;
    OSHandle_t      osHandle;
    oserr_t         oserr;
    int             status;

    oserr = OSSocketOpen(domain, type, protocol, &osHandle);
    if (oserr != OS_EOK) {
        return OsErrToErrNo(oserr);
    }

    status = stdio_handle_create(
            -1,
            0,
            0,
            NET_SIGNATURE,
            NULL,
            &handle
    );
    if (status) {
        OSHandleDestroy(&osHandle);
        return status;
    }
    return stdio_handle_iod(handle);
}

static oserr_t __net_read(stdio_handle_t* handle, void* buffer, size_t length, size_t* bytes_read)
{
    intmax_t num_bytes = recv(handle->IOD, buffer, length, 0);
    if (num_bytes >= 0) {
        *bytes_read = (size_t)num_bytes;
        return OS_EOK;
    }
    return OS_EUNKNOWN;
}

static oserr_t __net_write(stdio_handle_t* handle, const void* buffer, size_t length, size_t* bytes_written)
{
    intmax_t num_bytes = send(handle->IOD, buffer, length, 0);
    if (num_bytes >= 0) {
        *bytes_written = (size_t)num_bytes;
        return OS_EOK;
    }
    return OS_EUNKNOWN;
}

static oserr_t __net_ioctl(stdio_handle_t* handle, int request, va_list args)
{
    streambuffer_t* recvStream;
    oserr_t         oserr;

    oserr = OSSocketRecvPipe(&handle->OSHandle, &recvStream);
    if (oserr != OS_EOK) {
        return oserr;
    }

    if ((unsigned int)request == FIONREAD) {
        int* bytesAvailableOut = va_arg(args, int*);
        if (bytesAvailableOut) {
            size_t bytesAvailable;
            streambuffer_get_bytes_available_in(recvStream, &bytesAvailable);
            *bytesAvailableOut = (int)bytesAvailable;
        }
        return OS_EOK;
    }
    return OS_ENOTSUPPORTED;
}
