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
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define STREAM_TYPE_FILE   0
#define STREAM_TYPE_MEMORY 1

struct VaFsStream {
    int                      Type;
    uint32_t                 BlockSize;
    enum VaFsCompressionType CompressionType;
    mtx_t                    Lock;

    // The stage buffer is used for staging data before
    // we flush it to the data stream. The staging buffer
    // is always the size of the block size.
    void*  StageBuffer;
    size_t StageBufferIndex;

    // The stream index is the uncompressed location into the
    // data stream. This is used to determine the current location
    // of uncompressed data.
    size_t StreamIndex;

    union {
        struct {
            void*  Buffer;
            size_t Length;
            long   Offset;
        } Memory;
        FILE* File;
    };
};

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

    mtx_init(&stream->Lock, mtx_plain);
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

int vafs_stream_lock(
    struct VaFsStream* stream)
{
    if (!stream) {
        errno = EINVAL;
        return -1;
    }

    if (mtx_trylock(&stream->Lock) != thrd_success) {
        errno = EBUSY;
        return -1;
    }
    return 0;
}

int vafs_stream_unlock(
    struct VaFsStream* stream)
{
    if (!stream) {
        errno = EINVAL;
        return -1;
    }

    if (mtx_unlock(&stream->Lock) != thrd_success) {
        errno = ENOTSUP;
        return -1;
    }
    return 0;
}

long __get_position(
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
    if (stream == NULL || blockOut == NULL || offsetOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    *blockOut = (uint16_t)(stream->StreamIndex / stream->BlockSize);
    *offsetOut = (uint32_t)(stream->StreamIndex % stream->BlockSize);
    return 0;
}

extern size_t vafs_stream_size(
    struct VaFsStream* stream)
{
    if (stream == NULL) {
        errno = EINVAL;
        return 0;
    }

    if (stream->Type == STREAM_TYPE_FILE) {
        return ftell(stream->File);
    }
    else {
        return stream->Memory.Offset;
    }
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
        size_t bytesWritten = fwrite(buffer, 1, size, stream->File);
        if (bytesWritten != size) {
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

        memcpy(stream->Memory.Buffer, buffer, size);
        stream->Memory.Offset += size;
    }
    return 0;
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
        fprintf(stderr, "__flush_block: failed to write block header\n");
        return -1;
    }

    status = __write_stream(stream, compressedData, compressedSize);
    if (status) {
        fprintf(stderr, "__flush_block: failed to write block data\n");
        return -1;
    }

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
                fprintf(stderr, "vafs_stream_write: failed to flush block\n");
                return -1;
            }
        }
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

int vafs_stream_copy(
    struct VaFsStream* stream,
    struct VaFsStream* source)
{
    int status = 0;

    if (stream == NULL || source == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (stream->Type == STREAM_TYPE_FILE) {
        if (source->Type == STREAM_TYPE_FILE) {
            status = __fcopy(stream->File, source->File);
        }
        else if (source->Type == STREAM_TYPE_MEMORY) {
            if (fwrite(source->Memory.Buffer, 1, source->Memory.Offset, stream->File) != source->Memory.Offset) {
                return -1;
            }
        }
    }
    else if (stream->Type == STREAM_TYPE_MEMORY) {
        long sourceSize = vafs_stream_size(source);
        while (stream->Memory.Length < (stream->Memory.Offset + sourceSize)) {
            if (__grow_buffer(stream)) {
                return -1;
            }
        }

        if (source->Type == STREAM_TYPE_FILE) {
            if (fread(stream->Memory.Buffer + stream->Memory.Offset, 1, sourceSize, source->File) != sourceSize) {
                return -1;
            }
        }
        else if (source->Type == STREAM_TYPE_MEMORY) {
            memcpy(stream->Memory.Buffer + stream->Memory.Offset, source->Memory.Buffer, sourceSize);
        }

        if (!status) {
            stream->Memory.Offset += sourceSize;
        }
    }

    return status;
}

int vafs_stream_flush(
    struct VaFsStream* stream)
{
    if (!stream) {
        errno = EINVAL;
        return -1;
    }

    return __flush_block(stream);
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
