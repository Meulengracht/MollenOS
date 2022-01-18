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

enum VaFsCompressionType {
    VaFsCompressionType_NONE
};

enum VaFsArchitecture {
    VaFsArchitecture_X86 = 0x8086,
    VaFsArchitecture_X64 = 0x8664,
    VaFsArchitecture_ARM = 0xA12B,
    VaFsArchitecture_ARM64 = 0xAA64,
    VaFsArchitecture_RISCV = 0x5032
};

/**
 * @brief 
 * 
 * @param[In]  path 
 * @param[In]  architecture 
 * @param[In]  compressionType
 * @param[Out] handleOut
 * @return int
 */
extern int vafs_create(
    const char*              path, 
    enum VaFsArchitecture    architecture, 
    enum VaFsCompressionType compressionType,
    void**                   handleOut);

/**
 * @brief 
 * 
 * @param handle 
 * @return int 
 */
extern int vafs_close(
    void* handle);

/**
 * @brief 
 * 
 * @param handle
 * @param path 
 * @param handleOut 
 * @return int 
 */
extern int vafs_opendir(
    void*       handle,
    const char* path,
    void**      handleOut);

/**
 * @brief 
 * 
 * @param handle 
 * @param entry 
 * @return int 
 */
extern int vafs_dir_read(
    void*          handle,
    struct dirent* entry);

/**
 * @brief 
 * 
 * @param handle 
 * @param name 
 * @param content 
 * @param size 
 * @return int 
 */
extern int vafs_dir_write_file(
    void*       handle,
    const char* name,
    void*       content,
    size_t      size);

/**
 * @brief 
 * 
 * @param handle 
 * @param name 
 * @param handleOut 
 * @return int 
 */
extern int vafs_dir_write_directory(
    void*       handle,
    const char* name,
    void**      handleOut);

/**
 * @brief 
 * 
 * @param handle 
 * @return int 
 */
extern int vafs_dir_close(
    void* handle);

#endif //!__VAFS_H__
