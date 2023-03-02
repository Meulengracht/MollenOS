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
 */

//#define __TRACE

#include "os/notification_queue.h"
#include <ddk/utils.h>
#include <errno.h>
#include <internal/_io.h>
#include <internal/_tls.h>
#include <io.h>
#include <ioctl.h>
#include <os/shm.h>
#include <os/mollenos.h>
#include <string.h>

struct Pipe {
    SHMHandle_t  SHM;
    unsigned int Options;
};

static oserr_t __pipe_inherit(stdio_handle_t*);
static oserr_t __pipe_read(stdio_handle_t*, void*, size_t, size_t*);
static oserr_t __pipe_write(stdio_handle_t*, const void*, size_t, size_t*);
static oserr_t __pipe_resize(stdio_handle_t*, long long);
static oserr_t __pipe_seek(stdio_handle_t*, int, off64_t, long long*);
static oserr_t __pipe_ioctl(stdio_handle_t*, int, va_list);
static void    __pipe_close(stdio_handle_t*, int);

static stdio_ops_t g_pipeOps = {
        .inherit = __pipe_inherit,
        .read = __pipe_read,
        .write = __pipe_write,
        .resize = __pipe_resize,
        .seek = __pipe_seek,
        .ioctl = __pipe_ioctl,
        .close = __pipe_close
};

static struct Pipe*
__pipe_new(
        _In_ size_t size)
{
    struct Pipe* pipe;
    oserr_t      oserr;

    pipe = malloc(sizeof(struct Pipe));
    if (pipe == NULL) {
        return NULL;
    }

    oserr = SHMCreate(
            &(SHM_t) {
                    .Key = NULL,
                    .Flags = SHM_COMMIT,
                    .Access = SHM_ACCESS_READ | SHM_ACCESS_WRITE,
                    .Type = SHM_TYPE_REGULAR,
                    .Size = size
            },
            &pipe->SHM
    );
    if (oserr != OS_EOK) {
        (void)OsErrToErrNo(oserr);
        free(pipe);
        return NULL;
    }

    streambuffer_construct(
            pipe->SHM.Buffer,
            size - sizeof(struct streambuffer),
            STREAMBUFFER_MULTIPLE_WRITERS | STREAMBUFFER_GLOBAL
    );

    pipe->Options = STREAMBUFFER_ALLOW_PARTIAL;
    return pipe;
}

static void
__pipe_delete(
        _In_ struct Pipe* pipe)
{
    oserr_t oserr;

    if (pipe == NULL) {
        return;
    }

    oserr = SHMDetach(&pipe->SHM);
    if (oserr != OS_EOK) {
        WARNING("__pipe_delete: failed to detach from SHM id %u (%u)", pipe->SHM.ID, oserr);
    }
}

static int __check_unsupported(int flags)
{
    if (flags & ~(
            O_BINARY|O_TEXT|O_APPEND|
            O_TRUNC|O_EXCL|O_CREAT|
            O_RDWR|O_WRONLY|O_TMPFILE|
            O_NOINHERIT|
            O_SEQUENTIAL|O_RANDOM|O_SHORT_LIVED|
            O_WTEXT|O_U16TEXT|O_U8TEXT
    )) {
        errno = ENOTSUP;
        return -1;
    }
    return 0;
}

int pipe(long size, int flags)
{
    stdio_handle_t* object;
    int             status;
    struct Pipe*    pipe;
    
    if (__check_unsupported(flags)) {
        return -1;
    }

    pipe = __pipe_new(size);
    if (pipe == NULL) {
        return -1;
    }

    status = stdio_handle_create2(
            -1,
            flags,
            WX_PIPE | WX_APPEND,
            PIPE_SIGNATURE,
            &g_pipeOps,
            pipe,
            &object
    );
    if (status) {
        __pipe_delete(pipe);
        return -1;
    }

    stdio_handle_set_handle(object, pipe->SHM.ID);
    return object->fd;
}

static oserr_t
__pipe_read(stdio_handle_t* handle, void* buffer, size_t length, size_t* bytes_read)
{
    struct Pipe*    pipe = handle->ops_ctx;
    streambuffer_t* stream = pipe->SHM.Buffer;
    size_t          bytesRead;
    TRACE("stdio_pipe_op_read(handle=0x%" PRIxIN ", buffer = 0x%" PRIxIN ", length=%" PRIuIN ")",
          handle, buffer, length);

    bytesRead = streambuffer_stream_in(
            stream,
            buffer,
            length,
            &(streambuffer_rw_options_t) {
                    .flags = pipe->Options,
                    .async_context = __tls_current()->async_context,
                    .deadline = NULL
            }
    );
    *bytes_read = bytesRead;
    TRACE("stdio_pipe_op_read returns %" PRIuIN, bytesRead);
    return OS_EOK;
}

static oserr_t
__pipe_write(stdio_handle_t* handle, const void* buffer, size_t length, size_t* bytes_written)
{
    struct Pipe*    pipe = handle->ops_ctx;
    streambuffer_t* stream = pipe->SHM.Buffer;
    size_t          bytesWritten;
    TRACE("stdio_pipe_op_write(handle=0x%" PRIxIN ", buffer = 0x%" PRIxIN ", length=%" PRIuIN ")",
          handle, buffer, length);

    bytesWritten = streambuffer_stream_out(
            stream,
            (void*)buffer,
            length,
            &(streambuffer_rw_options_t) {
                    .flags = pipe->Options,
                    .async_context = __tls_current()->async_context,
                    .deadline = NULL
            }
    );
    stdio_handle_activity(handle, IOSETIN); // Mark pipe for recieved data

    *bytes_written = bytesWritten;
    TRACE("stdio_pipe_op_write returns %" PRIuIN, bytesWritten);
    return OS_EOK;
}

static oserr_t
__pipe_seek(stdio_handle_t* handle, int origin, off64_t offset, long long* position_out)
{
    return OS_ENOTSUPPORTED;
}

static oserr_t
__pipe_resize(stdio_handle_t* handle, long long resize_by)
{
    // This could be implemented some day, but for now we do not support
    // the resize operation on pipes.
    return OS_ENOTSUPPORTED;
}

static void
__pipe_close(
        _In_ stdio_handle_t* handle,
        _In_ int             options)
{
    if (options & STDIO_CLOSE_FULL) {
        __pipe_delete(handle->ops_ctx);
    }
}

static oserr_t
__pipe_inherit(
        _In_ stdio_handle_t* handle)
{
    struct Pipe* pipe = handle->ops_ctx;
    oserr_t      oserr;

    oserr = SHMAttach(
            handle->object.handle,
            &pipe->SHM
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = SHMMap(
            &pipe->SHM,
            0,
            pipe->SHM.Capacity,
            SHM_ACCESS_READ | SHM_ACCESS_WRITE
    );
    return oserr;
}

static oserr_t
__pipe_ioctl(
        _In_ stdio_handle_t* handle,
        _In_ int             request,
        _In_ va_list         args)
{
    struct Pipe*    pipe = handle->ops_ctx;
    streambuffer_t* stream = pipe->SHM.Buffer;

    if ((unsigned int)request == FIONBIO) {
        int* nonBlocking = va_arg(args, int*);
        if (nonBlocking) {
            if (*nonBlocking) {
                pipe->Options |= STREAMBUFFER_NO_BLOCK;
            }
            else {
                pipe->Options &= ~(STREAMBUFFER_NO_BLOCK);
            }
        }
        return OS_EOK;
    }
    else if ((unsigned int)request == FIONREAD) {
        int* bytesAvailableOut = va_arg(args, int*);
        if (bytesAvailableOut) {
            size_t bytesAvailable = 0;
            streambuffer_get_bytes_available_in(stream, &bytesAvailable);
            *bytesAvailableOut = (int)bytesAvailable;
        }
        return OS_EOK;
    }
    return OS_ENOTSUPPORTED;
}
