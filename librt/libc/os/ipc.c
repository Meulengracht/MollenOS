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

#include <ddk/utils.h>
#include <errno.h>
#include <internal/_io.h>
#include <internal/_tls.h>
#include <ioctl.h>
#include <io.h>
#include <ipcontext.h>
#include <os/handle.h>
#include <os/ipc.h>
#include <os/shm.h>

struct IPCContext {
    unsigned int Options;
};

static oserr_t __ipc_read(stdio_handle_t*, void*, size_t, size_t*);
static oserr_t __ipc_ioctl(stdio_handle_t*, int, va_list);

stdio_ops_t g_ipcOps = {
        .read = __ipc_read,
        .ioctl = __ipc_ioctl
};

static struct IPCContext*
__ipccontext_new(void)
{
    struct IPCContext* ipc;

    ipc = malloc(sizeof(struct IPCContext));
    if (ipc == NULL) {
        return NULL;
    }
    ipc->Options = 0;
    return ipc;
}

static void
__ipccontext_delete(
        _In_ struct IPCContext* ipc)
{
    if (ipc == NULL) {
        return;
    }
    free(ipc);
}

int ipcontext(unsigned int len, IPCAddress_t* addr)
{
    stdio_handle_t*    object;
    int                status;
    oserr_t            oserr;
    OSHandle_t         osHandle;
    struct IPCContext* ipc;
    TRACE("ipcontext(len=%u, addr=0x" PRIxIN ")", len, addr);
    
    if (!len) {
        _set_errno(EINVAL);
        return -1;
    }

    oserr = IPCContextCreate(len, addr, &osHandle);
    if (oserr != OS_EOK) {
        return OsErrToErrNo(oserr);
    }

    ipc = __ipccontext_new();
    if (ipc == NULL) {
        OSHandleDestroy(&osHandle);
        return -1;
    }

    status = stdio_handle_create2(
            -1,
            O_RDWR | O_NOINHERIT,
            WX_PIPE,
            IPC_SIGNATURE,
            ipc,
            &object
    );
    if (status) {
        __ipccontext_delete(ipc);
        OSHandleDestroy(&osHandle);
        return -1;
    }

    stdio_handle_set_handle(object, &osHandle);
    return stdio_handle_iod(object);
}

int ipsend(int iod, IPCAddress_t* addr, const void* data, unsigned int len, const struct timespec* deadline)
{
    stdio_handle_t*   handle = stdio_handle_get(iod);
    OSAsyncContext_t* asyncContext = __tls_current()->async_context;
    if (!handle || stdio_handle_signature(handle) != IPC_SIGNATURE) {
        _set_errno(EBADF);
        return -1;
    }

    if (asyncContext) {
        OSAsyncContextInitialize(asyncContext);
    }
    return OsErrToErrNo(
            IPCContextSend(
                    &handle->OSHandle,
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
    stdio_handle_t*    handle = stdio_handle_get(iod);
    size_t             bytesRead = 0;
    oserr_t            oserr;
    int                status = -1;
    OSAsyncContext_t*  asyncContext = __tls_current()->async_context;
    TRACE("iprecv(len=%u, flags=0x%x)");

    if (!handle || !buffer || !len) {
        _set_errno(EINVAL);
        goto exit;
    }
    
    if (stdio_handle_signature(handle) != IPC_SIGNATURE) {
        _set_errno(EBADF);
        goto exit;
    }

    if (asyncContext) {
        OSAsyncContextInitialize(asyncContext);
    }
    oserr = IPCContextRecv(
            &handle->OSHandle,
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

static oserr_t
__ipc_read(stdio_handle_t* handle, void* buffer, size_t length, size_t* bytes_read)
{
    struct IPCContext*        ipc = handle->OpsContext;
    streambuffer_t*           stream = SHMBuffer(&handle->OSHandle);
    streambuffer_packet_ctx_t packetCtx;
    streambuffer_rw_options_t rwOptions;
    size_t                    bytesAvailable;

    rwOptions.flags = ipc->Options;
    rwOptions.async_context = __tls_current()->async_context;
    rwOptions.deadline = NULL;

    bytesAvailable = streambuffer_read_packet_start(
            stream,
            &rwOptions,
            &packetCtx
    );
    if (!bytesAvailable) {
        _set_errno(ENODATA);
        return -1;
    }

    streambuffer_read_packet_data(buffer, MIN(length, bytesAvailable), &packetCtx);
    streambuffer_read_packet_end(&packetCtx);

    *bytes_read = MIN(length, bytesAvailable);
    return OS_EOK;
}

static oserr_t
__ipc_ioctl(stdio_handle_t* handle, int request, va_list args)
{
    struct IPCContext* ipc = handle->OpsContext;
    streambuffer_t*    stream = SHMBuffer(&handle->OSHandle);

    if ((unsigned int)request == FIONBIO) {
        int* nonBlocking = va_arg(args, int*);
        if (nonBlocking) {
            if (*nonBlocking) {
                ipc->Options |= STREAMBUFFER_NO_BLOCK;
            } else {
                ipc->Options &= ~(STREAMBUFFER_NO_BLOCK);
            }
        }
        return OS_EOK;
    } else if ((unsigned int)request == FIONREAD) {
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
