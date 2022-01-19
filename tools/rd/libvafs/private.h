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

#ifndef __VAFS_PRIVATE_H__
#define __VAFS_PRIVATE_H__

#include <platform.h>
#include <stdint.h>
#include <stdio.h>
#include <vafs.h>

struct VaFsStream;

#define VA_FS_MAGIC       0x3144524D
#define VA_FS_VERSION     0x00010000
#define VA_FS_BLOCKMAGIC  0xAE305532

#define VA_FS_INVALID_BLOCK  0xFFFF
#define VA_FS_INVALID_OFFSET 0xFFFFFFFF

// Default ramdisk block size is 64 kb
#define VA_FS_BLOCKSIZE (64 * 1024)

PACKED_TYPESTRUCT(VaFsBlock, {
    uint32_t Magic;
    uint32_t Length;
    uint32_t Crc;
    uint16_t CompressionType;
    uint16_t Flags;
});

PACKED_TYPESTRUCT(VaFsBlockPosition, {
    uint16_t Index;
    uint32_t Offset;
});

PACKED_TYPESTRUCT(VaFsHeader, {
    uint32_t            Magic;
    uint32_t            Version;
    uint32_t            Architecture;
    uint32_t            DescriptorBlockOffset;
    uint32_t            DataBlockOffset;
    VaFsBlockPosition_t RootDescriptor;
});

#define VA_FS_DESCRIPTOR_TYPE_FILE      0x01
#define VA_FS_DESCRIPTOR_TYPE_DIRECTORY 0x02

PACKED_TYPESTRUCT(VaFsDescriptor, {
    uint16_t Type;
    uint16_t Length; // Length of the descriptor
});

PACKED_TYPESTRUCT(VaFsFileDescriptor, {
    VaFsDescriptor_t    Base;
    VaFsBlockPosition_t Data;
    uint32_t            FileLength;
});

PACKED_TYPESTRUCT(VaFsDirectoryDescriptor, {
    VaFsDescriptor_t    Base;
    VaFsBlockPosition_t Descriptor;
});

PACKED_TYPESTRUCT(VaFsDirectoryHeader, {
    int Count;
});

enum VaFsMode {
    VaFsMode_Read,
    VaFsMode_Write
};

struct VaFsFile {
    struct VaFs*         VaFs;
    VaFsFileDescriptor_t Descriptor;
    const char*          Name;
};

struct VaFsDirectory {
    struct VaFs*              VaFs;
    VaFsDirectoryDescriptor_t Descriptor;
    const char*               Name;
};

struct VaFs {
    enum VaFsArchitecture Architecture;
    enum VaFsMode         Mode;
    struct VaFsStream*    Stream;
    struct VaFsStream*    DescriptorStream;
    struct VaFsStream*    DataStream;
    struct VaFsDirectory* RootDirectory;
};

/**
 * @brief 
 * 
 * @param path 
 * @param blockSize 
 * @param compressionType 
 * @param streamOut 
 * @return int 
 */
extern int vafs_stream_open_file(
    const char*              path,
    uint32_t                 blockSize,
    enum VaFsCompressionType compressionType,
    struct VaFsStream**      streamOut);

/**
 * @brief 
 * 
 * @param blockSize 
 * @param compressionType 
 * @param streamOut 
 * @return int 
 */
extern int vafs_stream_open_memory(
    uint32_t                 blockSize,
    enum VaFsCompressionType compressionType,
    struct VaFsStream**      streamOut);

/**
 * @brief Locks a specific stream for exclusive access, this is neccessary while
 * writing dat to the stream, to avoid any concurrent access to those streams, or
 * the user deciding to write two files at once. For read access this is not as neccessary
 * but could still be done.
 * 
 * @param[In] stream The stream that should be locked.
 * @return int Returns -1 if the stream is already locked, 0 on success.
 */
extern int vafs_stream_lock(
    struct VaFsStream* stream);

/**
 * @brief Unlocks a previously locked stream.
 * 
 * @param[In] stream The stream that should be unlocked.
 * @return int Returns -1 if the stream was not locked, 0 on success.
 */
extern int vafs_stream_unlock(
    struct VaFsStream* stream);

/**
 * @brief 
 * 
 * @param stream 
 * @param blockOut 
 * @param offsetOut 
 * @return int 
 */
extern int vafs_stream_position(
    struct VaFsStream* stream, 
    uint16_t*          blockOut,
    uint32_t*          offsetOut);

/**
 * @brief 
 * 
 * @param stream 
 * @param buffer 
 * @param size 
 * @return int 
 */
extern int vafs_stream_write(
    struct VaFsStream* stream,
    const void*        buffer,
    size_t             size);

/**
 * @brief 
 * 
 * @param stream 
 * @param source 
 * @return int 
 */
extern int vafs_stream_copy(
    struct VaFsStream* stream,
    struct VaFsStream* source);

/**
 * @brief 
 * 
 * @param stream 
 * @return int 
 */
extern int vafs_stream_flush(
    struct VaFsStream* stream);

/**
 * @brief 
 * 
 * @param stream 
 * @return size_t 
 */
extern size_t vafs_stream_size(
    struct VaFsStream* stream);

/**
 * @brief 
 * 
 * @param stream 
 * @return int 
 */
extern int vafs_stream_close(
    struct VaFsStream* stream);


/**
 * @brief 
 * 
 * @param stream 
 * @param directoryOut 
 * @return int 
 */
extern int vafs_directory_create_root(
    struct VaFs*           vafs,
    struct VaFsDirectory** directoryOut);

/**
 * @brief 
 * 
 * @param directory 
 * @return int 
 */
extern int vafs_directory_flush(
    struct VaFsDirectory* directory);

/**
 * @brief 
 * 
 * @param stream 
 * @param directoryOut 
 * @return int 
 */
extern int vafs_directory_open_root(
    struct VaFs*           vafs,
    VaFsBlockPosition_t*   position,
    struct VaFsDirectory** directoryOut);

/**
 * @brief 
 * 
 * @param fileEntry 
 * @return struct VaFsFileHandle* 
 */
extern struct VaFsFileHandle* vafs_file_create_handle(
    struct VaFsFile* fileEntry);

#endif // __VAFS_PRIVATE_H__
