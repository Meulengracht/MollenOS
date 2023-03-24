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
 *
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <internal/_io.h>
#include <internal/_file.h>
#include <io.h>
#include <ioctl.h>
#include <string.h>

struct MemoryStream {
    void*  Buffer;
    size_t Capacity;
    size_t Position;

    char**  UserBuffer;
    size_t* UserSize;
};

static oserr_t __memstream_write(stdio_handle_t*, const void*, size_t, size_t*);
static oserr_t __memstream_resize(stdio_handle_t*, long long);
static oserr_t __memstream_seek(stdio_handle_t*, int, off64_t, long long*);
static oserr_t __memstream_ioctl(stdio_handle_t*, int, va_list);
static void    __memstream_close(stdio_handle_t*, int);

stdio_ops_t g_memstreamOps = {
    .write = __memstream_write,
    .resize = __memstream_resize,
    .seek = __memstream_seek,
    .ioctl = __memstream_ioctl,
    .close = __memstream_close
};

static struct MemoryStream*
__memstream_new(
        _In_ char**  userBuffer,
        _In_ size_t* userSize)
{
    struct MemoryStream* memoryStream;

    memoryStream = malloc(sizeof(struct MemoryStream));
    if (memoryStream == NULL) {
        return NULL;
    }
    memset(memoryStream, 0, sizeof(struct MemoryStream));
    memoryStream->UserBuffer = userBuffer;
    memoryStream->UserSize = userSize;

    // Zero the buffers initially
    *userBuffer = NULL;
    *userSize = 0;

    return memoryStream;
}

static void
__memstream_delete(
        _In_ struct MemoryStream* memoryStream)
{
    if (memoryStream == NULL) {
        return;
    }
    free(memoryStream);
}

FILE* open_memstream(char** ptr, size_t* sizeloc)
{
    struct MemoryStream* memoryStream;
    stdio_handle_t*      object;
    int                  status;

    if (ptr == NULL || sizeloc == NULL) {
        errno = EINVAL;
        return NULL;
    }

    memoryStream = __memstream_new(ptr, sizeloc);
    if (memoryStream == NULL) {
        return NULL;
    }

    status = stdio_handle_create(
            -1,
            O_WRONLY | O_NOINHERIT,
            0,
            MEMORYSTREAM_SIGNATURE,
            memoryStream,
            &object
    );
    if (status) {
        __memstream_delete(memoryStream);
        return NULL;
    }

    status = stdio_handle_set_buffered(object, NULL, _IOWR, _IOFBF);
    if (status) {
        stdio_handle_delete(object);
        __memstream_delete(memoryStream);
        return NULL;
    }
    return stdio_handle_stream(object);
}

static oserr_t
__ensure_capacity(
        _In_ struct MemoryStream* memoryStream,
        _In_ size_t               length)
{
    void*  replacementBuffer;
    size_t replacementCapacity;

    if (memoryStream->Capacity > length) {
        return OS_EOK;
    }

    // Include a terminating null
    replacementCapacity = length + 1;

    replacementBuffer = realloc(memoryStream->Buffer, replacementCapacity);
    if (replacementBuffer == NULL) {
        return OS_EOOM;
    }

    memoryStream->Buffer = replacementBuffer;
    memoryStream->Capacity = replacementCapacity;

    // Update user-pointer after each buffer change
    *(memoryStream->UserBuffer) = (char*)memoryStream->Buffer;
    return OS_EOK;
}

static oserr_t
__memstream_write(
        _In_  stdio_handle_t* handle,
        _In_  const void*     buffer,
        _In_  size_t          length,
        _Out_ size_t*         bytesWritten)
{
    struct MemoryStream* memoryStream = handle->OpsContext;
    oserr_t              oserr;

    // Ensure that the stream has enough bytes
    oserr = __ensure_capacity(memoryStream, memoryStream->Position + length);
    if (oserr != OS_EOK) {
        return oserr;
    }

    // Write the actual data first
    memcpy(
            (char*)memoryStream->Buffer + memoryStream->Position,
            buffer,
            length
    );
    memoryStream->Position += length;

    // Ensure terminating zero
    ((char*)memoryStream->Buffer)[memoryStream->Position] = 0;
    *bytesWritten = length;

    // Update user supplied length after each write, but never take
    // the terminating zero into account
    *(memoryStream->UserSize) = memoryStream->Capacity - 1;
    return OS_EOK;
}

static oserr_t
__memstream_resize(
        _In_ stdio_handle_t* handle,
        _In_ long long       size)
{
    struct MemoryStream* memoryStream = handle->OpsContext;
    return __ensure_capacity(memoryStream, (size_t)size);
}

static oserr_t
__memstream_seek(
        _In_  stdio_handle_t* handle,
        _In_  int             origin,
        _In_  off64_t         offset,
        _Out_ long long*      positionOut)
{
    struct MemoryStream* memoryStream = handle->OpsContext;
    off64_t              position;

    if (origin == SEEK_CUR) {
        position = (off64_t)memoryStream->Position + offset;
    } else if (origin == SEEK_END) {
        position = (off64_t)memoryStream->Capacity + offset;
    } else {
        position = offset;
    }

    if (position < 0) {
        position = 0;
    } else if (position > (off64_t)memoryStream->Capacity) {
        position = (off64_t)memoryStream->Capacity;
    }

    memoryStream->Position = (size_t)position;
    *positionOut = position;
    return OS_EOK;
}

static oserr_t
__memstream_ioctl(
        _In_ stdio_handle_t* handle,
        _In_ int             request,
        _In_ va_list         args)
{
    struct MemoryStream* memoryStream = handle->OpsContext;
    if ((unsigned int)request == FIONREAD) {
        int* bytesAvailableOut = va_arg(args, int*);
        if (bytesAvailableOut) {
            size_t bytesAvailable = memoryStream->Capacity - memoryStream->Position;
            *bytesAvailableOut = (int)bytesAvailable;
        }
        return OS_EOK;
    }
    return OS_ENOTSUPPORTED;
}

static void
__memstream_close(
        _In_ stdio_handle_t* handle,
        _In_ int             __unused)
{
    _CRT_UNUSED(__unused);
    __memstream_delete(handle->OpsContext);
}
