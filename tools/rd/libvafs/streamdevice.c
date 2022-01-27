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
            long   Capacity;
            long   Position;
            int    Owned;
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

    *deviceOut = device;
    return 0;
}

int vafs_streamdevice_open_file(
    const char*               path,
    struct VaFsStreamDevice** deviceOut)
{
    struct VaFsStreamDevice* device;
    FILE*                    handle;

    if (path == NULL  || deviceOut == NULL) {
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

    device->File = handle;
    
    *deviceOut = device;
    return 0;
}

int vafs_streamdevice_open_memory(
    const void*               buffer,
    size_t                    length,
    struct VaFsStreamDevice** deviceOut)
{
    struct VaFsStreamDevice* device;

    if (buffer == NULL || length == 0 || deviceOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (__new_streamdevice(STREAMDEVICE_MEMORY, &device)) {
        return -1;
    }

    device->Memory.Buffer = (void*)buffer;
    device->Memory.Capacity = (long)length;
    device->Memory.Position = 0;
    device->Memory.Owned = 0;
    
    *deviceOut = device;
    return 0;
}

int vafs_streamdevice_create_file(
    const char*               path,
    struct VaFsStreamDevice** deviceOut)
{
    struct VaFsStreamDevice* device;
    FILE*                    handle;

    if (path == NULL  || deviceOut == NULL) {
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

    device->File = handle;
    
    *deviceOut = device;
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

    if (__new_streamdevice(STREAMDEVICE_MEMORY, &device)) {
        free(buffer);
        return -1;
    }

    device->Memory.Buffer = buffer;
    device->Memory.Capacity = (long)blockSize;
    device->Memory.Position = 0;
    device->Memory.Owned = 1;
    
    *deviceOut = device;
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
    else if (device->Type == STREAMDEVICE_MEMORY && device->Memory.Owned) {
        free(device->Memory.Buffer);
    }

    mtx_destroy(&device->Lock);
    free(device);
    return 0;
}

long __get_position(
    struct VaFsStreamDevice* device)
{
    if (device->Type == STREAMDEVICE_FILE) {
        return ftell(device->File);
    }
    else {
        return device->Memory.Position;
    }
}

long vafs_streamdevice_seek(
    struct VaFsStreamDevice* device,
    long                     offset,
    int                      whence)
{
    VAFS_DEBUG("vafs_streamdevice_seek(offset=%ld, whence=%i)\n", offset, whence);
    if (device == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (offset == 0 && whence == SEEK_CUR) {
        return __get_position(device);
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
                device->Memory.Position = offset;
                break;
            case SEEK_CUR:
                device->Memory.Position += offset;
                break;
            case SEEK_END:
                device->Memory.Position = device->Memory.Capacity + offset;
                break;
            default:
                errno = EINVAL;
                return -1;
        }
        device->Memory.Position = MIN(MAX(device->Memory.Position, 0), device->Memory.Capacity);
        return device->Memory.Position;
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
        size_t byteCount = MIN(length, device->Memory.Capacity - device->Memory.Position);
        memcpy(buffer, device->Memory.Buffer + device->Memory.Position, byteCount);
        device->Memory.Position += byteCount;
        *bytesRead = byteCount;
        return 0;
    }

    errno = EINVAL;
    return -1;
}

static int __grow_buffer(
    struct VaFsStreamDevice* device,
    size_t                   length)
{
    void*  buffer;
    size_t newSize;
    
    newSize = device->Memory.Capacity + length;
    buffer = realloc(device->Memory.Buffer, newSize);
    if (!buffer) {
        errno = ENOMEM;
        return -1;
    }

    device->Memory.Buffer = buffer;
    device->Memory.Capacity = newSize;
    return 0;
}

static inline int __memsize_available(
    struct VaFsStreamDevice* device)
{
    return device->Memory.Capacity - device->Memory.Position;
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
    if (device->Type == STREAMDEVICE_FILE) {
        *bytesWritten = fwrite(buffer, 1, length, device->File);
        if (*bytesWritten != length) {
            return -1;
        }
    }
    else if (device->Type == STREAMDEVICE_MEMORY) {
        // if the stream is a memory stream, then ensure enough space in buffer
        if (device->Type == STREAMDEVICE_MEMORY) {
            while (length > __memsize_available(device)) {
                if (__grow_buffer(device, length - __memsize_available(device))) {
                    return -1;
                }
            }
        }

        memcpy(device->Memory.Buffer + device->Memory.Position, buffer, length);
        device->Memory.Position += length;
        *bytesWritten = length;
    }
    return 0;
}

static int __fcopy(FILE* destination, FILE* source)
{
    char chr;
    int  status;

    status = fseek(source, 0, SEEK_SET);
    if (status) {
        return status;
    }

    while ((chr = fgetc(source)) != EOF) {
        fputc(chr, destination);
    }
    return 0;
}

int vafs_streamdevice_copy(
    struct VaFsStreamDevice* destination,
    struct VaFsStreamDevice* source)
{
    int status = 0;

    if (destination == NULL || source == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (destination->Type == STREAMDEVICE_FILE) {
        if (source->Type == STREAMDEVICE_FILE) {
            status = __fcopy(destination->File, source->File);
        }
        else if (source->Type == STREAMDEVICE_MEMORY) {
            if (fwrite(source->Memory.Buffer, 1, source->Memory.Position, destination->File) != source->Memory.Position) {
                return -1;
            }
        }
    }
    else if (destination->Type == STREAMDEVICE_MEMORY) {
        long sourceSize = __get_position(source);
        long capacityRequired = destination->Memory.Position + sourceSize;
        while (destination->Memory.Capacity < capacityRequired) {
            if (__grow_buffer(destination, capacityRequired - destination->Memory.Capacity)) {
                return -1;
            }
        }

        if (source->Type == STREAMDEVICE_FILE) {
            if (fread(destination->Memory.Buffer + destination->Memory.Position, 1, sourceSize, source->File) != sourceSize) {
                return -1;
            }
        }
        else if (source->Type == STREAMDEVICE_MEMORY) {
            memcpy(destination->Memory.Buffer + destination->Memory.Position, source->Memory.Buffer, sourceSize);
        }
        destination->Memory.Position += sourceSize;
    }

    return status;
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
