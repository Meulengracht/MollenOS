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

#define __need_minmax
//#define __TRACE

#include <ddk/handle.h>
#include <ddk/utils.h>
#include <errno.h>
#include <internal/_io.h>
#include <internal/_tls.h>
#include <ioctl.h>
#include <ipcontext.h>
#include <os/ipc.h>

struct IPCContext {
    streambuffer_t* stream;
    unsigned int    options;
};

static oserr_t __ipc_inherit(stdio_handle_t*);
static oserr_t __ipc_read(stdio_handle_t*, void*, size_t, size_t*);
static oserr_t __ipc_write(stdio_handle_t*, const void*, size_t, size_t*);
static oserr_t __ipc_resize(stdio_handle_t*, long long);
static oserr_t __ipc_seek(stdio_handle_t*, int, off64_t, long long*);
static oserr_t __ipc_ioctl(stdio_handle_t*, int, va_list);
static void    __ipc_close(stdio_handle_t*, int);

static stdio_ops_t g_ipcOps = {
        .inherit = __ipc_inherit,
        .read = __ipc_read,
        .write = __ipc_write,
        .resize = __ipc_resize,
        .seek = __ipc_seek,
        .ioctl = __ipc_ioctl,
        .close = __ipc_close
};

static struct IPCContext*
__ipccontext_new()
{
    struct IPCContext* ipc;

    ipc = malloc(sizeof(struct IPCContext));
    if (ipc == NULL) {
        return NULL;
    }

}

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
    if (oserr != OS_EOK) {
        OsErrToErrNo(oserr);
        return -1;
    }

    status = stdio_handle_create(-1, WX_OPEN | WX_PIPE | WX_DONTINHERIT, &io_object);
    if (status) {
        OSHandleDestroy(handle);
        return -1;
    }
    
    stdio_handle_set_handle(io_object, handle);
    stdio_handle_set_ops_type(io_object, STDIO_HANDLE_IPCONTEXT);
    
    io_object->object.data.ipcontext.stream  = ipcContext;
    io_object->object.data.ipcontext.options = 0;
    
    return io_object->fd;
}

int ipsend(int iod, IPCAddress_t* addr, const void* data, unsigned int len, const struct timespec* deadline)
{
    stdio_handle_t*   handle = stdio_handle_get(iod);
    OSAsyncContext_t* asyncContext = __tls_current()->async_context;
    if (!handle) {
        _set_errno(EBADF);
        return -1;
    }

    if (handle->object.type != STDIO_HANDLE_IPCONTEXT) {
        _set_errno(EBADFD);
        return -1;
    }

    if (asyncContext) {
        OSAsyncContextInitialize(asyncContext);
    }
    return OsErrToErrNo(
            IPCContextSend(
                    handle->object.handle,
                    addr,
                    data,
                    len,
                    deadline == NULL ? NULL : &(OSTimestamp_t) {
                            .Seconds = deadline->tv_sec,
                            .Nanoseconds = deadline->tv_nsec
                        },
                    asyncContext
            )
    );
}

int iprecv(int iod, void* buffer, unsigned int len, int flags, uuid_t* fromHandle)
{
    stdio_handle_t*   handle = stdio_handle_get(iod);
    size_t            bytesRead = 0;
    oserr_t           oserr;
    int               status = -1;
    OSAsyncContext_t* asyncContext = __tls_current()->async_context;
    TRACE("iprecv(len=%u, flags=0x%x)");

    if (!handle || !buffer || !len) {
        _set_errno(EINVAL);
        goto exit;
    }
    
    if (handle->object.type != STDIO_HANDLE_IPCONTEXT) {
        _set_errno(EBADF);
        goto exit;
    }

    if (asyncContext) {
        OSAsyncContextInitialize(asyncContext);
    }
    oserr = IPCContextRecv(
            handle->object.data.ipcontext.stream,
            buffer,
            len,
            flags,
            asyncContext,
            fromHandle,
            &bytesRead
    );
    if (oserr != OS_EOK) {
        OsErrToErrNo(oserr);
        goto exit;
    } else if (bytesRead == 0) {
        errno = ENODATA;
        goto exit;
    }
    status = (int)bytesRead;
exit:
    return status;
}

oserr_t __ipc_read(stdio_handle_t* handle, void* buffer, size_t length, size_t* bytes_read)
{
    streambuffer_t*           stream = handle->object.data.ipcontext.stream;
    streambuffer_packet_ctx_t packetCtx;
    streambuffer_rw_options_t rwOptions;
    size_t                    bytesAvailable;

    rwOptions.flags = handle->object.data.ipcontext.options;
    rwOptions.async_context = __tls_current()->async_context;
    rwOptions.deadline = NULL;

    bytesAvailable = streambuffer_read_packet_start(stream, &rwOptions, &packetCtx);
    if (!bytesAvailable) {
        _set_errno(ENODATA);
        return -1;
    }

    streambuffer_read_packet_data(buffer, MIN(length, bytesAvailable), &packetCtx);
    streambuffer_read_packet_end(&packetCtx);

    *bytes_read = MIN(length, bytesAvailable);
    return OS_EOK;
}

oserr_t __ipc_write(stdio_handle_t* handle, const void* buffer, size_t length, size_t* bytes_written)
{
    // Write is not supported
    return OS_ENOTSUPPORTED;
}

oserr_t __ipc_seek(stdio_handle_t* handle, int origin, off64_t offset, long long* position_out)
{
    // Seek is not supported
    return OS_ENOTSUPPORTED;
}

oserr_t __ipc_resize(stdio_handle_t* handle, long long resize_by)
{
    // Resize is not supported
    return OS_ENOTSUPPORTED;
}

void __ipc_close(stdio_handle_t* handle, int options)
{
    if (handle->object.handle != UUID_INVALID) {
        (void)OSHandleDestroy(handle->object.handle);
    }
}

oserr_t __ipc_inherit(stdio_handle_t* handle)
{
    // Is not supported
    return OS_EOK;
}

oserr_t __ipc_ioctl(stdio_handle_t* handle, int request, va_list args)
{
    streambuffer_t* stream = handle->object.data.ipcontext.stream;

    if ((unsigned int)request == FIONBIO) {
        int* nonBlocking = va_arg(args, int*);
        if (nonBlocking) {
            if (*nonBlocking) {
                handle->object.data.ipcontext.options |= STREAMBUFFER_NO_BLOCK;
            }
            else {
                handle->object.data.ipcontext.options &= ~(STREAMBUFFER_NO_BLOCK);
            }
        }
        return OS_EOK;
    }
    else if ((unsigned int)request == FIONREAD) {
        int* bytesAvailableOut = va_arg(args, int*);
        if (bytesAvailableOut) {
            size_t bytesAvailable;
            streambuffer_get_bytes_available_in(stream, &bytesAvailable);
            *bytesAvailableOut = (int)bytesAvailable;
        }
        return OS_EOK;
    }
    return OS_ENOTSUPPORTED;
}
