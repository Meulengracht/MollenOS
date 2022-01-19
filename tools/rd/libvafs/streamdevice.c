/**
 * Copyright 2022, Philip Meulengracht
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
 * Vali Initrd Filesystem
 * - Contains the implementation of the Vali Initrd Filesystem.
 *   This filesystem is used to store the initrd of the kernel.
 */

#include <errno.h>
#include "private.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STREAMDEVICE_FILE   0
#define STREAMDEVICE_MEMORY 1

struct VaFsStreamDevice {
    int   Type;
    mtx_t Lock;

    union {
        struct {
            void*  Buffer;
            size_t Length;
            size_t BlockSize;
            long   Offset;
        } Memory;
        FILE* File;
    };
};

static int __new_streamdevice(
    int                       type,
    struct VaFsStreamDevice** deviceOut)
{
    struct VaFsStreamDevice* device;
    
    device = (struct VaFsStreamDevice*)malloc(sizeof(struct VaFsStreamDevice));
    if (!device) {
        errno = ENOMEM;
        return -1;
    }
    memset(device, 0, sizeof(struct VaFsStreamDevice));

    mtx_init(&device->Lock, mtx_plain);
    device->Type = type;

    *streamOut = stream;
    return 0;
}

int vafs_streamdevice_open_file(
    const char*               path,
    struct VaFsStreamDevice** deviceOut)
{
    struct VaFsStreamDevice* device;
    FILE*                    handle;

    if (path == NULL  || device == NULL) {
        errno = EINVAL;
        return -1;
    }

    handle = fopen(path, "r");
    if (!handle) {
        return -1;
    }

    if (__new_streamdevice(STREAMDEVICE_FILE, &device)) {
        fclose(handle);
        return -1;
    }

    stream->File = handle;
    
    *streamOut = stream;
    return 0;
}

int vafs_streamdevice_open_memory(
    void*                     buffer,
    size_t                    length,
    struct VaFsStreamDevice** deviceOut)
{
    struct VaFsStreamDevice* device;
    void*                    buffer;

    if (buffer == NULL || length == 0 || deviceOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (__new_streamdevice(STREAMDEVICE_MEMORY, &stream)) {
        free(buffer);
        return -1;
    }

    stream->Memory.Buffer = buffer;
    stream->Memory.Length = length;
    stream->Memory.BlockSize = 0;
    stream->Memory.Offset = 0;
    
    *streamOut = stream;
    return 0;
}

int vafs_streamdevice_create_file(
    const char*               path,
    struct VaFsStreamDevice** deviceOut)
{
    struct VaFsStreamDevice* device;
    FILE*                    handle;

    if (path == NULL  || device == NULL) {
        errno = EINVAL;
        return -1;
    }

    handle = fopen(path, "wb+");
    if (!handle) {
        return -1;
    }

    if (__new_streamdevice(STREAMDEVICE_FILE, &device)) {
        fclose(handle);
        return -1;
    }

    stream->File = handle;
    
    *streamOut = stream;
    return 0;
}

int vafs_streamdevice_create_memory(
    size_t                    blockSize,
    struct VaFsStreamDevice** deviceOut)
{
    struct VaFsStreamDevice* device;
    void*                    buffer;

    if (blockSize == 0 || deviceOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    buffer = malloc(blockSize);
    if (!buffer) {
        errno = ENOMEM;
        return -1;
    }

    if (__new_streamdevice(STREAMDEVICE_MEMORY, &stream)) {
        free(buffer);
        return -1;
    }

    stream->Memory.Buffer = buffer;
    stream->Memory.Length = blockSize;
    stream->Memory.BlockSize = blockSize;
    stream->Memory.Offset = 0;
    
    *streamOut = stream;
    return 0;
}

int vafs_streamdevice_close(
    struct VaFsStreamDevice* device)
{
    if (device == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (device->Type == STREAMDEVICE_FILE) {
        fclose(device->File);
    }
    else if (device->Type == STREAMDEVICE_MEMORY) {
        free(device->Memory.Buffer);
    }

    mtx_destroy(&device->Lock);
    free(device);
    return 0;
}

long vafs_streamdevice_seek(
    struct VaFsStreamDevice* device,
    long                     offset,
    int                      whence)
{
    if (device == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (device->Type == STREAMDEVICE_FILE) {
        int status = fseek(device->File, offset, whence);
        if (status != 0) {
            return -1;
        }
        return ftell(device->File);
    }
    else if (device->Type == STREAMDEVICE_MEMORY) {
        switch (whence) {
            case SEEK_SET:
                device->Memory.Offset = offset;
                break;
            case SEEK_CUR:
                device->Memory.Offset += offset;
                break;
            case SEEK_END:
                device->Memory.Offset = device->Memory.Length + offset;
                break;
            default:
                errno = EINVAL;
                return -1;
        }
        device->Memory.Offset = MIN(MAX(device->Memory.Offset, 0), device->Memory.Length);
        return device->Memory.Offset;
    }

    errno = EINVAL;
    return -1;
}

int vafs_streamdevice_read(
    struct VaFsStreamDevice* device,
    void*                    buffer,
    size_t                   length,
    size_t*                  bytesRead)
{
    if (device == NULL || buffer == NULL || length == 0 || bytesRead == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (device->Type == STREAMDEVICE_FILE) {
        *bytesRead = fread(buffer, 1, length, device->File);
        if (*bytesRead != length) {
            return -1;
        }
        return 0;
    }
    else if (device->Type == STREAMDEVICE_MEMORY) {
        size_t bytesRead = MIN(length, device->Memory.Length - device->Memory.Offset);
        memcpy(buffer, device->Memory.Buffer + device->Memory.Offset, bytesRead);
        device->Memory.Offset += bytesRead;
        *bytesRead = bytesRead;
        return 0;
    }

    errno = EINVAL;
    return -1;
}

static int __grow_buffer(
    struct VaFsStreamDevice* device)
{
    void*  buffer;
    size_t newSize;
    
    if (!device->Memory.BlockSize) {
        errno = ENOTSUP;
        return -1;
    }

    newSize = device->Memory.Length + device->Memory.BlockSize;
    buffer = realloc(device->Memory.Buffer, newSize);
    if (!buffer) {
        errno = ENOMEM;
        return -1;
    }

    device->Memory.Buffer = buffer;
    device->Memory.Length = newSize;
    return 0;
}

static inline int __memsize_available(
    struct VaFsStream* stream)
{
    return stream->Memory.Length - stream->Memory.Offset;
}

int vafs_streamdevice_write(
    struct VaFsStreamDevice* device,
    void*                    buffer,
    size_t                   length,
    size_t*                  bytesWritten)
{
    if (device == NULL || buffer == NULL || length == 0 || bytesWritten == NULL) {
        errno = EINVAL;
        return -1;
    }

    // flush the actual block data
    if (stream->Type == STREAM_TYPE_FILE) {
        *bytesWritten = fwrite(buffer, 1, size, stream->File);
        if (*bytesWritten != size) {
            return -1;
        }
    }
    else if (stream->Type == STREAM_TYPE_MEMORY) {
        // if the stream is a memory stream, then ensure enough space in buffer
        if (stream->Type == STREAM_TYPE_MEMORY) {
            while (size > __memsize_available(stream)) {
                if (__grow_buffer(stream)) {
                    return -1;
                }
            }
        }

        memcpy(stream->Memory.Buffer + stream->Memory.Offset, buffer, size);
        stream->Memory.Offset += size;
        *bytesWritten = size;
    }
    return 0;
}

int vafs_streamdevice_lock(
    struct VaFsStreamDevice* device)
{
    if (!device) {
        errno = EINVAL;
        return -1;
    }

    if (mtx_trylock(&device->Lock) != thrd_success) {
        errno = EBUSY;
        return -1;
    }
    return 0;
}

int vafs_streamdevice_unlock(
    struct VaFsStreamDevice* device)
{
    if (!device) {
        errno = EINVAL;
        return -1;
    }

    if (mtx_unlock(&device->Lock) != thrd_success) {
        errno = ENOTSUP;
        return -1;
    }
    return 0;
}
