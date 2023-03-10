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

#include <assert.h>
#include <ddk/utils.h>
#include <ds/streambuffer.h>
#include <errno.h>
#include <internal/_io.h>
#include <internal/_tls.h>
#include <io.h>
#include <ioctl.h>
#include <os/handle.h>
#include <os/notification_queue.h>
#include <os/shm.h>
#include <os/mollenos.h>
#include <string.h>
#include <stdlib.h>

struct Pipe {
    unsigned int Options;
};

static size_t __pipe_serialize(void* context, void* out);
static size_t __pipe_deserialize(const void* in, void** contextOut);
static oserr_t __pipe_clone(const void* context, void** contextOut);
static oserr_t __pipe_read(stdio_handle_t*, void*, size_t, size_t*);
static oserr_t __pipe_write(stdio_handle_t*, const void*, size_t, size_t*);
static oserr_t __pipe_ioctl(stdio_handle_t*, int, va_list);
static void    __pipe_close(stdio_handle_t*, int);

stdio_ops_t g_pipeOps = {
        .serialize = __pipe_serialize,
        .deserialize = __pipe_deserialize,
        .clone = __pipe_clone,
        .read = __pipe_read,
        .write = __pipe_write,
        .ioctl = __pipe_ioctl,
        .close = __pipe_close
};

static struct Pipe*
__pipe_new(void)
{
    struct Pipe* pipe;

    pipe = malloc(sizeof(struct Pipe));
    if (pipe == NULL) {
        return NULL;
    }

    pipe->Options = STREAMBUFFER_ALLOW_PARTIAL;
    return pipe;
}

static void
__pipe_delete(
        _In_ struct Pipe* pipe)
{
    if (pipe == NULL) {
        return;
    }
    free(pipe);
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

static oserr_t
__CreatePipeArea(
        _In_ OSHandle_t* handle,
        _In_ size_t      size)
{
    oserr_t oserr;

    oserr = SHMCreate(
            &(SHM_t) {
                    .Key = NULL,
                    .Flags = SHM_COMMIT,
                    .Access = SHM_ACCESS_READ | SHM_ACCESS_WRITE,
                    .Type = SHM_TYPE_REGULAR,
                    .Size = size
            },
            handle
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    streambuffer_construct(
            SHMBuffer(handle),
            size - sizeof(struct streambuffer),
            STREAMBUFFER_MULTIPLE_WRITERS | STREAMBUFFER_GLOBAL
    );
    return oserr;
}

int pipe(long size, int flags)
{
    stdio_handle_t* object;
    OSHandle_t      osHandle;
    oserr_t         oserr;
    int             status;
    struct Pipe*    pipe;
    
    if (__check_unsupported(flags)) {
        return -1;
    }

    oserr = __CreatePipeArea(&osHandle, size);
    if (oserr != OS_EOK) {
        return OsErrToErrNo(oserr);
    }

    pipe = __pipe_new();
    if (pipe == NULL) {
        OSHandleDestroy(&osHandle);
        return -1;
    }

    status = stdio_handle_create2(
            -1,
            flags,
            WX_PIPE | WX_APPEND,
            PIPE_SIGNATURE,
            pipe,
            &object
    );
    if (status) {
        __pipe_delete(pipe);
        return -1;
    }

    stdio_handle_set_handle(object, &osHandle);
    return stdio_handle_iod(object);
}

static oserr_t
__pipe_read(stdio_handle_t* handle, void* buffer, size_t length, size_t* bytes_read)
{
    struct Pipe*    pipe = handle->OpsContext;
    streambuffer_t* stream = SHMBuffer(&handle->OSHandle);
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
    struct Pipe*    pipe = handle->OpsContext;
    streambuffer_t* stream = SHMBuffer(&handle->OSHandle);
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

static void
__pipe_close(
        _In_ stdio_handle_t* handle,
        _In_ int             options)
{
    if (options & STDIO_CLOSE_FULL) {
        __pipe_delete(handle->OpsContext);
    }
}

static size_t
__pipe_serialize(void* context, void* out)
{
    struct Pipe* pipe = context;
    uint8_t*     out8 = out;

    *((unsigned int*)&out8[0]) = pipe->Options;

    return sizeof(unsigned int);
}

static size_t
__pipe_deserialize(const void* in, void** contextOut)
{
    const uint8_t* in8 = in;
    struct Pipe*   pipe = __pipe_new();
    assert(pipe != NULL);

    pipe->Options = *((unsigned int*)&in8[0]);

    *contextOut = pipe;
    return sizeof(unsigned int);
}

static oserr_t
__pipe_clone(const void* context, void** contextOut)
{
    const struct Pipe* original = context;
    struct Pipe*       clone;

    // allocate a new structure for the clone
    clone = malloc(sizeof(struct Pipe));
    if (clone == NULL) {
        return OS_EOOM;
    }
    memcpy(clone, original, sizeof(struct Pipe));
    *contextOut = clone;
    return OS_EOK;
}

static oserr_t
__pipe_ioctl(
        _In_ stdio_handle_t* handle,
        _In_ int             request,
        _In_ va_list         args)
{
    struct Pipe*    pipe = handle->OpsContext;
    streambuffer_t* stream = SHMBuffer(&handle->OSHandle);

    if ((unsigned int)request == FIONBIO) {
        int* nonBlocking = va_arg(args, int*);
        if (nonBlocking) {
            if (*nonBlocking) {
                pipe->Options |= STREAMBUFFER_NO_BLOCK;
            } else {
                pipe->Options &= ~(STREAMBUFFER_NO_BLOCK);
            }
        }
        return OS_EOK;
    } else if ((unsigned int)request == FIONREAD) {
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
