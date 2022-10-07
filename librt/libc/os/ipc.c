/**
 * Copyright 2020, Philip Meulengracht
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
 */

//#define __TRACE

#include <ddk/handle.h>
#include <ddk/utils.h>
#include <errno.h>
#include <internal/_io.h>
#include <ipcontext.h>
#include <os/ipc.h>

int ipcontext(unsigned int len, IPCAddress_t* addr)
{
    stdio_handle_t* io_object;
    int             status;
    oserr_t         oserr;
    void*           ipcContext;
    uuid_t          handle;

    TRACE("ipcontext(len=%u, addr=0x" PRIxIN ")", len, addr);
    
    if (!len) {
        _set_errno(EINVAL);
        return -1;
    }

    oserr = IPCContextCreate(len, addr, &handle, &ipcContext);
    if (oserr != OsOK) {
        OsErrToErrNo(oserr);
        return -1;
    }

    status = stdio_handle_create(-1, WX_OPEN | WX_PIPE | WX_DONTINHERIT, &io_object);
    if (status) {
        handle_destroy(handle);
        return -1;
    }
    
    stdio_handle_set_handle(io_object, handle);
    stdio_handle_set_ops_type(io_object, STDIO_HANDLE_IPCONTEXT);
    
    io_object->object.data.ipcontext.stream  = ipcContext;
    io_object->object.data.ipcontext.options = 0;
    
    return io_object->fd;
}

int ipsend(int iod, IPCAddress_t* addr, const void* data, unsigned int len, int timeout)
{
    stdio_handle_t* handle = stdio_handle_get(iod);
    if (!handle) {
        _set_errno(EBADF);
        return -1;
    }

    if (handle->object.type != STDIO_HANDLE_IPCONTEXT) {
        _set_errno(EBADFD);
        return -1;
    }
    return OsErrToErrNo(IPCContextSend(handle->object.handle, addr, data, len, timeout));
}

int iprecv(int iod, void* buffer, unsigned int len, int flags, uuid_t* fromHandle)
{
    stdio_handle_t* handle = stdio_handle_get(iod);
    size_t          bytesAvailable;
    oserr_t         oserr;
    int             status = 0;

    if (!handle || !buffer || !len) {
        _set_errno(EINVAL);
        status = -1;
        goto exit;
    }
    
    if (handle->object.type != STDIO_HANDLE_IPCONTEXT) {
        _set_errno(EBADF);
        status = -1;
        goto exit;
    }

    oserr = IPCContextRecv(
            handle->object.data.ipcontext.stream,
            buffer,
            len,
            flags,
            fromHandle,
            &bytesAvailable
    );
    if (oserr != OsOK) {
        return OsErrToErrNo(oserr);
    } else if (bytesAvailable == 0) {
        errno = ENODATA;
        return -1;
    }

exit:
    return status;
}
