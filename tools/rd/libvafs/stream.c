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
    uint32_t                 BlockSize;
    struct VaFsStreamDevice* Device;
    long                     DeviceOffset;
    VaFsFilterEncodeFunc     Encode;
    VaFsFilterDecodeFunc     Decode;

    // The block buffer is used for staging data before
    // we flush it to the data stream. The staging buffer
    // is always the size of the block size.
    void*  BlockBuffer;
    size_t BlockBufferIndex;
    size_t BlockBufferOffset;
};

static int __new_stream(
    struct VaFsStreamDevice* device,
    long                     deviceOffset,
    uint32_t                 blockSize,
    struct VaFsStream**      streamOut)
{
    struct VaFsStream* stream;
    VAFS_DEBUG("__new_stream(offset=%lu, blockSize=%u)\n",
        deviceOffset, blockSize);
    
    stream = (struct VaFsStream*)malloc(sizeof(struct VaFsStream));
    if (!stream) {
        errno = ENOMEM;
        return -1;
    }

    stream->Device = device;
    stream->DeviceOffset = deviceOffset;
    stream->BlockSize = blockSize;
    stream->Encode = NULL;
    stream->Decode = NULL;

    stream->BlockBuffer = malloc(blockSize);
    if (!stream->BlockBuffer) {
        free(stream);
        errno = ENOMEM;
        return -1;
    }
    stream->BlockBufferIndex = 0;
    stream->BlockBufferOffset = 0;

    *streamOut = stream;
    return 0;
}

int vafs_stream_create(
    struct VaFsStreamDevice* device,
    long                     deviceOffset,
    uint32_t                 blockSize,
    struct VaFsStream**      streamOut)
{
    struct VaFsStream* stream;

    if (blockSize == 0 || streamOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (__new_stream(device, deviceOffset, blockSize, &stream)) {
        return -1;
    }

    *streamOut = stream;
    return 0;
}

int vafs_stream_set_filter(
    struct VaFsStream*   stream,
    VaFsFilterEncodeFunc encode,
    VaFsFilterDecodeFunc decode)
{
    if (stream == NULL) {
        errno = EINVAL;
        return -1;
    }

    stream->Encode = encode;
    stream->Decode = decode;
    return 0;
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

    *blockOut = (uint16_t)stream->BlockBufferIndex;
    *offsetOut = (uint32_t)stream->BlockBufferOffset;
    return 0;
}

static uint32_t __get_blockbuffer_crc(
    struct VaFsStream* stream,
    size_t             length)
{
    return crc_calculate(
        CRC_BEGIN, 
        (uint8_t*)stream->BlockBuffer, 
        length
    );
}

static int __load_blockbuffer(
    struct VaFsStream* stream,
    VaFsBlock_t*       block)
{
    void*    blockData;
    uint32_t blockSize;
    size_t   read;
    uint32_t crc;
    int      status;
    VAFS_DEBUG("__load_blockbuffer(length=%u)\n", block->Length);

    blockSize = block->Length;
    blockData = malloc(block->Length);
    if (!blockData) {
        errno = ENOMEM;
        return -1;
    }

    status = vafs_streamdevice_read(stream->Device, blockData, blockSize, &read);
    if (status) {
        return status;
    }

    // Handle data filters
    if (stream->Decode) {
        uint32_t blockBufferSize = stream->BlockSize;

        VAFS_DEBUG("__load_blockbuffer decoding buffer of size %u\n", blockSize);
        status = stream->Decode(blockData, blockSize, stream->BlockBuffer, &blockBufferSize);
        if (status) {
            free(blockData);
            return status;
        }
        VAFS_DEBUG("__load_blockbuffer decoded buffer size %u\n", blockBufferSize);

        // TODO we should keep a current length of block
        // verify the length of the block is correct, the actual size
        // of the decoded data is now in blockSize
        blockSize = blockBufferSize;
    }
    else {
        memcpy(stream->BlockBuffer, blockData, blockSize);
    }
    free(blockData);

    crc = __get_blockbuffer_crc(stream, blockSize);
    if (crc != block->Crc) {
        VAFS_WARN("__load_blockbuffer: CRC mismatch: %u != %u\n", crc, block->Crc);
        errno = EIO;
        return -1;
    }
    return 0;
}

int vafs_stream_seek(
    struct VaFsStream* stream, 
    uint16_t           blockIndex,
    uint32_t           blockOffset)
{
    VaFsBlock_t block;
    int         status;
    long        offset;
    uint16_t    targetBlock = blockIndex;
    uint32_t    targetOffset = blockOffset;
    size_t      read;
    uint16_t    i = 0;
    VAFS_DEBUG("vafs_stream_seek(blockIndex=%u, blockOffset=%u)\n",
        blockIndex, blockOffset);

    if (stream == NULL) {
        errno = EINVAL;
        return -1;
    }
    
    // seek to start of stream
    offset = stream->DeviceOffset;
    while (1) {
        status = vafs_streamdevice_seek(stream->Device, offset, SEEK_SET);
        if (status < 0) {
            VAFS_ERROR("vafs_stream_seek: seek failed in stream device: %i\n", status);
            return status;
        }
        
        status = vafs_streamdevice_read(stream->Device, &block, sizeof(VaFsBlock_t), &read);
        if (status) {
            VAFS_ERROR("vafs_stream_seek: read failed from stream device: %i\n", status);
            return status;
        }
        
        // have we reached the target block, and does it contain our index?
        if (i >= targetBlock) {
            if (targetOffset < stream->BlockSize) {
                offset += sizeof(VaFsBlock_t);
                break;
            }

            targetBlock++;
            targetOffset -= stream->BlockSize;
        }

        offset += sizeof(VaFsBlock_t) + block.Length;
        i++;
    }

    status = __load_blockbuffer(stream, &block);
    if (status) {
        VAFS_ERROR("vafs_stream_seek: load blockbuffer failed: %i\n", status);
        return status;
    }

    stream->BlockBufferIndex = targetBlock;
    stream->BlockBufferOffset = targetOffset;
    return 0;
}

static int __write_block_header(
    struct VaFsStream* stream,
    uint32_t           blockLength)
{
    VaFsBlock_t header;
    size_t      written;
    VAFS_DEBUG("__write_block_header(blockLength=%u)\n", blockLength);

    header.Magic = VA_FS_BLOCKMAGIC;
    header.Crc = __get_blockbuffer_crc(stream, stream->BlockBufferOffset);
    header.Length = blockLength;
    header.Flags = 0;
    return vafs_streamdevice_write(stream->Device, &header, sizeof(VaFsBlock_t), &written);
}

static int __flush_block(
    struct VaFsStream* stream)
{
    void*    compressedData = stream->BlockBuffer;
    uint32_t compressedSize = stream->BlockBufferOffset;
    size_t   written;
    int      status;
    VAFS_DEBUG("__flush_block(blockLength=%u)\n", stream->BlockBufferOffset);
    
    // Handle compressions
    if (stream->Encode) {
        status = stream->Encode(stream->BlockBuffer, stream->BlockBufferOffset, &compressedData, &compressedSize);
        if (status) {
            return status;
        }
        VAFS_DEBUG("__flush_block compressed buffer size %u\n", compressedSize);
    }

    // flush the block to the stream, write header first
    if (__write_block_header(stream, compressedSize)) {
        VAFS_ERROR("__flush_block: failed to write block header\n");
        return -1;
    }

    status = vafs_streamdevice_write(stream->Device, compressedData, compressedSize, &written);
    if (status) {
        VAFS_ERROR("__flush_block: failed to write block data\n");
        return -1;
    }

    // In the case of a compressed stream, we need to free the compressed data
    if (stream->Encode) {
        free(compressedData);
    }

    stream->BlockBufferIndex++;
    stream->BlockBufferOffset = 0;
    return status;
}

int vafs_stream_write(
    struct VaFsStream* stream,
    const void*        buffer,
    size_t             size)
{
    uint32_t bytesLeftInBlock;
    size_t   bytesToWrite = size;
    VAFS_DEBUG("vafs_stream_write(size=%u)\n", size);

    if (stream == NULL || buffer == NULL || size == 0) {
        errno = EINVAL;
        return -1;
    }

    // write the data to stream, taking care of block boundaries
    while (bytesToWrite) {
        size_t byteCount;

        bytesLeftInBlock = stream->BlockSize - (stream->BlockBufferOffset % stream->BlockSize);
        byteCount = MIN(bytesToWrite, bytesLeftInBlock);

        memcpy(stream->BlockBuffer + stream->BlockBufferOffset, buffer, byteCount);
        stream->BlockBufferOffset += byteCount;
        bytesToWrite -= byteCount;

        if (stream->BlockBufferOffset == stream->BlockSize) {
            if (__flush_block(stream)) {
                VAFS_ERROR("vafs_stream_write: failed to flush block\n");
                return -1;
            }
        }
    }

    return 0;
}

int vafs_stream_read(
    struct VaFsStream* stream,
    void*              buffer,
    size_t             size)
{
    size_t bytesLeftInBlock;
    size_t bytesToRead = size;
    VAFS_DEBUG("vafs_stream_read(size=%u)\n", size);

    if (stream == NULL || buffer == NULL || size == 0) {
        errno = EINVAL;
        return -1;
    }

    // read the data from stream, taking care of block boundaries
    while (bytesToRead) {
        size_t byteCount;

        bytesLeftInBlock = stream->BlockSize - stream->BlockBufferOffset;
        byteCount = MIN(bytesToRead, bytesLeftInBlock);

        memcpy(buffer, stream->BlockBuffer + stream->BlockBufferOffset, byteCount);
        stream->BlockBufferOffset += byteCount;
        bytesToRead -= byteCount;

        if (stream->BlockBufferOffset == stream->BlockSize) {
            VaFsBlock_t block;
            size_t      read;
            int         status;

            status = vafs_streamdevice_read(stream->Device, &block, sizeof(VaFsBlock_t), &read);
            if (status) {
                VAFS_ERROR("vafs_stream_read: failed to read block header\n");
                return -1;
            }

            if (__load_blockbuffer(stream, &block)) {
                VAFS_ERROR("vafs_stream_read: failed to load block\n");
                return -1;
            }

            stream->BlockBufferIndex++;
            stream->BlockBufferOffset = 0;
        }
    }

    return 0;
}

int vafs_stream_flush(
    struct VaFsStream* stream)
{
    VAFS_DEBUG("vafs_stream_flush()\n");
    if (!stream) {
        errno = EINVAL;
        return -1;
    }

    return __flush_block(stream);
}

int vafs_stream_close(
    struct VaFsStream* stream)
{
    VAFS_DEBUG("vafs_stream_close()\n");
    if (!stream) {
        errno = EINVAL;
        return -1;
    }

    free(stream->BlockBuffer);
    free(stream);
    return 0;
}

int vafs_stream_lock(
    struct VaFsStream* stream)
{
    if (!stream) {
        errno = EINVAL;
        return -1;
    }

    return vafs_streamdevice_lock(stream->Device);
}

int vafs_stream_unlock(
    struct VaFsStream* stream)
{
    if (!stream) {
        errno = EINVAL;
        return -1;
    }

    return vafs_streamdevice_unlock(stream->Device);
}
