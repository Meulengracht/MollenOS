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

#ifndef __VAFS_H__
#define __VAFS_H__

#include <platform.h>
#include <stdint.h>

struct VaFs;
struct VaFsDirectoryHandle;
struct VaFsFileHandle;

enum VaFsCompressionType {
    VaFsCompressionType_NONE
};

enum VaFsArchitecture {
    VaFsArchitecture_X86 = 0x8086,
    VaFsArchitecture_X64 = 0x8664,
    VaFsArchitecture_ARM = 0xA12B,
    VaFsArchitecture_ARM64 = 0xAA64,
    VaFsArchitecture_RISCV32 = 0x5032,
    VaFsArchitecture_RISCV64 = 0x5064,
};

/**
 * @brief 
 * 
 * @param[In]  path 
 * @param[In]  architecture 
 * @param[In]  compressionType
 * @param[Out] vafsOut
 * @return int
 */
extern int vafs_create(
    const char*              path, 
    enum VaFsArchitecture    architecture, 
    enum VaFsCompressionType compressionType,
    struct VaFs**            vafsOut);

/**
 * @brief 
 * 
 * @param path 
 * @param vafsOut 
 * @return int 
 */
extern int vafs_open(
    const char*   path,
    struct VaFs** vafsOut);

/**
 * @brief 
 * 
 * @param handle 
 * @return int 
 */
extern int vafs_close(
    struct VaFs* vafs);

/**
 * @brief 
 * 
 * @param handle
 * @param path 
 * @param handleOut 
 * @return int 
 */
extern int vafs_directory_open(
    struct VaFs*                 vafs,
    const char*                  path,
    struct VaFsDirectoryHandle** handleOut);

/**
 * @brief 
 * 
 * @param handle 
 * @return int 
 */
extern int vafs_directory_close(
    struct VaFsDirectoryHandle* handle);

/**
 * @brief 
 * 
 * @param[In]  handle The directory handle to read an entry from.
 * @param[Out] entry  A pointer to a dirent that is filled with information if an entry is available. 
 * @return int Returns -1 on error or if no more entries are available (errno is set accordingly), 0 on success
 */
extern int vafs_directory_read(
    struct VaFsDirectoryHandle* handle,
    struct dirent*              entry);

/**
 * @brief 
 * 
 * @param handle 
 * @param name 
 * @param handleOut 
 * @return int 
 */
extern int vafs_directory_open_directory(
    struct VaFsDirectoryHandle*  handle,
    const char*                  name,
    struct VaFsDirectoryHandle** handleOut);

/**
 * @brief 
 * 
 * @param handle 
 * @param name 
 * @return int 
 */
extern int vafs_directory_open_file(
    struct VaFsDirectoryHandle* handle,
    const char*                 name,
    struct VaFsFileHandle**     handleOut);

/**
 * @brief 
 * 
 * @param handle 
 * @return int 
 */
extern int vafs_file_close(
    struct VaFsFileHandle* handle);

/**
 * @brief 
 * 
 * @param handle 
 * @return size_t 
 */
extern size_t vafs_file_length(
    struct VaFsFileHandle* handle);

/**
 * @brief 
 * 
 * @param handle 
 * @param offset 
 * @param whence 
 * @return int 
 */
extern int vafs_file_seek(
    struct VaFsFileHandle* handle,
    off_t                  offset,
    int                    whence);

/**
 * @brief 
 * 
 * @param handle 
 * @param buffer 
 * @param size 
 * @return size_t 
 */
extern size_t vafs_file_read(
    struct VaFsFileHandle* handle,
    void*                  buffer,
    size_t                 size);

/**
 * @brief 
 * 
 * @param handle 
 * @param buffer 
 * @param size 
 * @return size_t 
 */
extern size_t vafs_file_write(
    struct VaFsFileHandle* handle,
    void*                  buffer,
    size_t                 size);

#endif //!__VAFS_H__
