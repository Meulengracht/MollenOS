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

#include "crc.h"
#include <errno.h>
#include "private.h"
#include <stdio.h>

#define STREAM_TYPE_FILE   0
#define STREAM_TYPE_MEMORY 1

static int __new_stream(
    int                      type,
    uint32_t                 blockSize,
    enum VaFsCompressionType compressionType,
    struct VaFsStream**      streamOut)
{
    struct VaFsStream* stream;
    
    stream = (struct VaFsStream*)malloc(sizeof(struct VaFsStream));
    if (!stream) {
        errno = ENOMEM;
        return -1;
    }

    stream->Type = type;
    stream->BlockSize = blockSize;
    stream->CompressionType = compressionType;
    stream->StreamIndex = 0;

    stream->StageBuffer = malloc(blockSize);
    if (!stream->StageBuffer) {
        free(stream);
        errno = ENOMEM;
        return -1;
    }
    stream->StageBufferIndex = 0;

    *streamOut = stream;
    return 0;
}

int vafs_stream_open_file(
    const char*              path,
    uint32_t                 blockSize,
    enum VaFsCompressionType compressionType,
    struct VaFsStream**      streamOut)
{
    struct VaFsStream* stream;
    FILE*              handle;

    if (path == NULL  || blockSize == 0 || streamOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    handle = fopen(path, "wb+");
    if (!handle) {
        return -1;
    }

    if (__new_stream(STREAM_TYPE_FILE, blockSize, compressionType, &stream)) {
        fclose(handle);
        return -1;
    }

    stream->File = handle;
    
    *streamOut = stream;
    return 0;
}

int vafs_stream_open_memory(
    uint32_t                 blockSize,
    enum VaFsCompressionType compressionType,
    struct VaFsStream**      streamOut)
{
    struct VaFsStream* stream;
    void*              buffer;

    if (blockSize == 0 || streamOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    buffer = malloc(blockSize);
    if (!buffer) {
        errno = ENOMEM;
        return -1;
    }

    if (__new_stream(STREAM_TYPE_MEMORY, blockSize, compressionType, &stream)) {
        free(buffer);
        return -1;
    }

    stream->Memory.Buffer = buffer;
    stream->Memory.Length = blockSize;
    stream->Memory.Offset = 0;
    
    *streamOut = stream;
    return 0;
}

off_t __get_position(
    struct VaFsStream* stream)
{
    if (stream->Type == STREAM_TYPE_FILE) {
        return ftell(stream->File);
    }
    else {
        return stream->Memory.Offset;
    }
}

int vafs_stream_position(
    struct VaFsStream* stream, 
    uint16_t*          blockOut,
    uint32_t*          offsetOut)
{
    off_t position;

    if (stream == NULL || blockOut == NULL || offsetOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (stream->Type == STREAM_TYPE_FILE) {
        position = ftell(stream->File);
    }
    else {
        position = stream->Memory.Offset;
    }

    *blockOut = (uint16_t)(position / stream->BlockSize);
    *offsetOut = (uint32_t)(position % stream->BlockSize);
    return 0;
}

static int __grow_buffer(
    struct VaFsStream* stream)
{
    void*  buffer;
    size_t newSize;

    newSize = stream->Memory.Length + stream->BlockSize;
    buffer = realloc(stream->Memory.Buffer, newSize);
    if (!buffer) {
        errno = ENOMEM;
        return -1;
    }

    stream->Memory.Buffer = buffer;
    stream->Memory.Length = newSize;
    return 0;
}

static inline int __memsize_available(
    struct VaFsStream* stream)
{
    return stream->Memory.Length - stream->Memory.Offset;
}

static uint32_t __get_crc(
#include <vafs.h>
    struct VaFsStream* stream)
{
    return crc_calculate(
        CRC_BEGIN, 
        (uint8_t*)stream->StageBuffer, 
        stream->StageBufferIndex
    );
}

static int __write_stream(
    struct VaFsStream* stream,
    const void*        buffer,
    size_t             size)
{
    // flush the actual block data
    if (stream->Type == STREAM_TYPE_FILE) {
        return fwrite(buffer, 1, size, stream->File);
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

        memcpy(stream->Memory.Buffer, buffer, size);
        stream->Memory.Offset += size;
        return 0;
    }
}

static int __write_block_header(
    struct VaFsStream* stream,
    uint32_t           blockLength)
{
    VaFsBlock_t header;

    header.Magic = VA_FS_BLOCKMAGIC;
    header.Crc = __get_crc(stream);
    header.CompressionType = stream->CompressionType;
    header.Length = blockLength;
    header.Flags = 0;
    return __write_stream(stream, &header, sizeof(header));
}

static int __flush_block(
    struct VaFsStream* stream)
{
    void*  compressedData = stream->StageBuffer;
    size_t compressedSize = stream->StageBufferIndex;
    int    status;
    
    // compress data first
    if (stream->CompressionType != VaFsCompressionType_NONE) {

    }

    // flush the block to the stream, write header first
    if (__write_block_header(stream, compressedSize)) {
        return -1;
    }

    status = __write_stream(stream, compressedData, compressedSize);
    stream->StageBufferIndex = 0;
    return status;
}

int vafs_stream_write(
    struct VaFsStream* stream,
    const void*        buffer,
    size_t             size)
{
    uint32_t bytesLeftInBlock;
    size_t   bytesToWrite = size;

    if (stream == NULL || buffer == NULL || size == 0) {
        errno = EINVAL;
        return -1;
    }

    // write the data to stream, taking care of block boundaries
    while (bytesToWrite) {
        size_t byteCount;

        bytesLeftInBlock = stream->BlockSize - (stream->StageBufferIndex % stream->BlockSize);
        byteCount = MIN(bytesToWrite, bytesLeftInBlock);

        memcpy(stream->StageBuffer + stream->StageBufferIndex, buffer, byteCount);

        stream->StreamIndex += byteCount;
        stream->StageBufferIndex += byteCount;
        bytesToWrite -= byteCount;

        if (stream->StageBufferIndex == stream->BlockSize) {
            if (__flush_block(stream)) {
                return -1;
            }
        }
    }

    return 0;
}

int vafs_stream_copy(
    struct VaFsStream* stream,
    struct VaFsStream* source)
{
    if (stream == NULL || source == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (stream->Type == STREAM_TYPE_FILE) {
        if (source->Type == STREAM_TYPE_FILE) {
            return fcopy(source->File, stream->File);
        }
        else if (source->Type == STREAM_TYPE_MEMORY) {
            return fwrite(source->Memory.Buffer, 1, source->Memory.Length, stream->File);
        }
    }
    else if (stream->Type == STREAM_TYPE_MEMORY) {
        if (source->Type == STREAM_TYPE_FILE) {
            return fread(stream->Memory.Buffer, 1, stream->Memory.Length, source->File);
        }
        else if (source->Type == STREAM_TYPE_MEMORY) {
            if (stream->Memory.Length < source->Memory.Length) {
                if (__grow_buffer(stream)) {
                    return -1;
                }
            }

            memcpy(stream->Memory.Buffer, source->Memory.Buffer, source->Memory.Length);
            stream->Memory.Offset = source->Memory.Length;
            return source->Memory.Length;
        }
    }

    errno = EINVAL;
    return -1;
}

int vafs_stream_close(
    struct VaFsStream* stream)
{
    if (!stream) {
        errno = EINVAL;
        return -1;
    }

    if (stream->Type == STREAM_TYPE_FILE) {
        fclose(stream->File);
    }
    else if (stream->Type == STREAM_TYPE_MEMORY) {
        free(stream->Memory.Buffer);
    }

    free(stream->StageBuffer);
    free(stream);
    return 0;
}
