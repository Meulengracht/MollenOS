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
#include <internal/_io.h>
#include <internal/_file.h>
#include <io.h>
#include <ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct MemoryStream {
    void*  Buffer;
    size_t Capacity;
    size_t Position;
    bool   Cleanup;
};

static oserr_t __memstream_read(stdio_handle_t*, void*, size_t, size_t*);
static oserr_t __memstream_write(stdio_handle_t*, const void*, size_t, size_t*);
static oserr_t __memstream_resize(stdio_handle_t*, long long);
static oserr_t __memstream_seek(stdio_handle_t*, int, off64_t, long long*);
static oserr_t __memstream_ioctl(stdio_handle_t*, int, va_list);
static void    __memstream_close(stdio_handle_t*, int);

stdio_ops_t g_fmemOps = {
    .read = __memstream_read,
    .write = __memstream_write,
    .resize = __memstream_resize,
    .seek = __memstream_seek,
    .ioctl = __memstream_ioctl,
    .close = __memstream_close
};

static struct MemoryStream*
__memstream_new(
        _In_ void*  buffer,
        _In_ size_t length)
{
    struct MemoryStream* memoryStream;

    memoryStream = malloc(sizeof(struct MemoryStream));
    if (memoryStream == NULL) {
        return NULL;
    }

    if (buffer == NULL) {
        memoryStream->Buffer = malloc(length);
        if (memoryStream->Buffer == NULL) {
            free(memoryStream);
            return NULL;
        }
        memoryStream->Cleanup = true;
    } else {
        memoryStream->Buffer  = buffer;
        memoryStream->Cleanup = false;
    }
    memoryStream->Capacity = length;
    memoryStream->Position = 0;
    return memoryStream;
}

static void
__memstream_delete(
        _In_ struct MemoryStream* memoryStream)
{
    if (memoryStream == NULL) {
        return;
    }

    if (memoryStream->Cleanup) {
        free(memoryStream->Buffer);
    }
    free(memoryStream);
}

FILE* fmemopen(void *buf, size_t size, const char *mode)
{
    int                  flags;
    struct MemoryStream* memoryStream;
    stdio_handle_t*      object;
    int                  status;

    if (size == 0 || mode == NULL) {
        errno = EINVAL;
        return NULL;
    }

    if (__fmode_to_flags(mode, &flags)) {
        errno = EINVAL;
        return NULL;
    }

    memoryStream = __memstream_new(buf, size);
    if (memoryStream == NULL) {
        return NULL;
    }

    status = stdio_handle_create(
            -1,
            flags | O_NOINHERIT,
            0,
            FMEM_SIGNATURE,
            memoryStream,
            &object
    );
    if (status) {
        __memstream_delete(memoryStream);
        return NULL;
    }

    status = stdio_handle_set_buffered(object, NULL, _IORW, _IOFBF);
    if (status) {
        stdio_handle_delete(object);
        __memstream_delete(memoryStream);
        return NULL;
    }
    return stdio_handle_stream(object);
}

static oserr_t
__memstream_read(
        _In_  stdio_handle_t* handle,
        _In_  void*           buffer,
        _In_  size_t          length,
        _Out_ size_t*         bytesRead)
{
    struct MemoryStream* memoryStream = handle->OpsContext;
    size_t               bytesToRead = memoryStream->Capacity - memoryStream->Position;

    // clamp to smallest size
    if (length < bytesToRead) {
        bytesToRead = length;
    }

    if (bytesToRead > 0) {
        memcpy(
                buffer,
                (char*)memoryStream->Buffer + memoryStream->Position,
                bytesToRead
        );
        memoryStream->Position += bytesToRead;
    }
    *bytesRead = bytesToRead;
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
    size_t               bytesToWrite = memoryStream->Capacity - memoryStream->Position;

    // There must always be space available for a zero byte at end of buffer
    if (bytesToWrite < 1) {
        return OS_ENOENT;
    }

    // Clamp to smallest size
    if ((length + 1) < bytesToWrite) {
        bytesToWrite = length;
    }

    // Write the actual data first
    memcpy(
            (char*)memoryStream->Buffer + memoryStream->Position,
            buffer,
            bytesToWrite
    );
    memoryStream->Position += bytesToWrite;

    // Ensure the terminating zero
    ((char*)memoryStream->Buffer)[memoryStream->Position] = 0;
    *bytesWritten = bytesToWrite;
    return OS_EOK;
}

static oserr_t
__memstream_resize(
        _In_ stdio_handle_t* handle,
        _In_ long long       size)
{
    struct MemoryStream* memoryStream = handle->OpsContext;
    char*                replacementBuffer;

    replacementBuffer = malloc((size_t)size);
    if (replacementBuffer == NULL) {
        return OS_EOOM;
    }

    memcpy(replacementBuffer, memoryStream->Buffer, size - 1);
    replacementBuffer[size - 1] = 0;
    free(memoryStream->Buffer);
    memoryStream->Buffer = replacementBuffer;
    return OS_EOK;
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
        _In_ int             options)
{
    if (options & STDIO_CLOSE_FULL) {
        __memstream_delete(handle->OpsContext);
    }
}
