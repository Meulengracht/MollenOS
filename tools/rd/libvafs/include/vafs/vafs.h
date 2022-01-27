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

#include <stdint.h>
#include <stddef.h>

struct VaFs;
struct VaFsDirectoryHandle;
struct VaFsFileHandle;

struct VaFsGuid {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t  Data4[8];
};

/**
 * List of builtin features for the filesystem
 * VA_FS_FEATURE_FILTER     - Data filters can be applied for data streams
 * VA_FS_FEATURE_FILTER_OPS - Filter operations (Not persistant)
 */
#define VA_FS_FEATURE_FILTER     { 0x99C25D91, 0xFA99, 0x4A71, {0x9C, 0xB5, 0x96, 0x1A, 0xA9, 0x3D, 0xDF, 0xBB } }
#define VA_FS_FEATURE_FILTER_OPS { 0x17BC0212, 0x7DF3, 0x4BDD, {0x99, 0x24, 0x5A, 0xC8, 0x13, 0xBE, 0x72, 0x49 } }

enum VaFsLogLevel {
    VaFsLogLevel_Error,
    VaFsLogLevel_Warning,
    VaFsLogLevel_Info,
    VaFsLogLevel_Debug
};

enum VaFsArchitecture {
    VaFsArchitecture_X86 = 0x8086,
    VaFsArchitecture_X64 = 0x8664,
    VaFsArchitecture_ARM = 0xA12B,
    VaFsArchitecture_ARM64 = 0xAA64,
    VaFsArchitecture_RISCV32 = 0x5032,
    VaFsArchitecture_RISCV64 = 0x5064,
};

struct VaFsFeatureHeader {
    struct VaFsGuid Guid;
    uint32_t        Length; // Length of the entire feature data including this header
};

enum VaFsEntryType {
    VaFsEntryType_Unknown,
    VaFsEntryType_File,
    VaFsEntryType_Directory
};

struct VaFsEntry {
    const char*        Name;
    enum VaFsEntryType Type;
};

/**
 * @brief The filter feature must be installed both when creating the image, and
 * when loading the image. The feature is used by the underlying streams to apply data manipulation 
 * while loading/writing. This means the user must supply the filter operations to use,
 * as there is no predefined way of compressing or encrypting data.
 * 
 * This feature data is not transferred to the disk image, but rather used if present.
 */

/**
 * @brief It is expected of the encode function to allocate a buffer of the size of the data and provide
 * the size of the allocated buffer for the encoded data in the Output/OutputLength parameters.
 */
typedef int(*VaFsFilterEncodeFunc)(void* Input, uint32_t InputLength, void** Output, uint32_t* OutputLength);

/**
 * @brief The decode function will be provided with a buffer of the encoded data, and the size of the encoded data. The
 * decode function will also be provided with a buffer of the size of the decoded data, and the maximum size of the decoded data.
 * If the decoded data size varies from the maximum size provided, the size should be set to the actual decoded data size. 
 */
typedef int(*VaFsFilterDecodeFunc)(void* Input, uint32_t InputLength, void* Output, uint32_t* OutputLength);

struct VaFsFeatureFilterOps {
    struct VaFsFeatureHeader Header;
    VaFsFilterEncodeFunc     Encode;
    VaFsFilterDecodeFunc     Decode;
};

/**
 * @brief Control the log level of the library. This is useful for debugging. The default
 * log level is set to VaFsLogLevel_Warning.
 * 
 * @param[In] level The level of log output to enable
 */
extern void vafs_log_initalize(
    enum VaFsLogLevel level);

/**
 * @brief Creates a new filesystem image. The image handle only permits operations that write
 * to the image. This means that reading from the image will fail.
 * 
 * @param[In]  path         The path the image file should be created at.
 * @param[In]  architecture The architecture of the image platform.
 * @param[Out] vafsOut      A pointer where the handle of the filesystem instance will be stored.
 * @return int 0 on success, -1 on failure.
 */
extern int vafs_create(
    const char*              path, 
    enum VaFsArchitecture    architecture,
    struct VaFs**            vafsOut);

/**
 * @brief Opens an existing filesystem image. The image handle only permits operations that read
 * from the image. All images that are created by this library are read-only.
 * 
 * @param[In]  path    Path to the filesystem image. 
 * @param[Out] vafsOut A pointer where the handle of the filesystem instance will be stored.
 * @return int 0 on success, -1 on failure. See errno for more details.
 */
extern int vafs_open_file(
    const char*   path,
    struct VaFs** vafsOut);

/**
 * @brief Opens an existing filesystem image buffer. The image handle only permits operations that read
 * from the image. All images that are created by this library are read-only. The image buffer needs to stay
 * valid for duration of the time the vafs handle is used.
 *
 * @param[In]  buffer  Pointer to the filesystem image buffer.
 * @param[In]  size    Size of the filesystem image buffer.
 * @param[Out] vafsOut A pointer where the handle of the filesystem instance will be stored.
 * @return int 0 on success, -1 on failure. See errno for more details.
 */
extern int vafs_open_memory(
        const void*   buffer,
        size_t        size,
        struct VaFs** vafsOut);

/**
 * @brief Closes the filesystem handle. If the image was just created, the data streams are kept in 
 * memory at this point and will not be written to disk before this function is called.
 * 
 * @param[In] vafs The filesystem handle to close. 
 * @return int Returns 0 on success, -1 on failure.
 */
extern int vafs_close(
    struct VaFs* vafs);

/**
 * @brief This installs a feature into the filesystem. The features must be installed after
 * creating or opening the image, before any other operations are performed.
 * 
 * @param[In] vafs    The filesystem to install the feature into.
 * @param[In] feature The feature to install. The feature data is copied, so no need to keep the feature around.
 * @return int Returns -1 if the feature is already installed, 0 on success. 
 */
extern int vafs_feature_add(
    struct VaFs*              vafs,
    struct VaFsFeatureHeader* feature);

/**
 * @brief Checks if a specific feature is present in the filesystem image. 
 * 
 * @param[In]  vafs       The filesystem image to check.
 * @param[In]  guid       The GUID of the feature to check for.
 * @param[Out] featureOut A pointer to a feature header pointer which will be set to the feature.
 * @return int Returns -1 if the feature is not present, 0 if it is present.
 */
extern int vafs_feature_query(
    struct VaFs*               vafs,
    struct VaFsGuid*           guid,
    struct VaFsFeatureHeader** featureOut);

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
 * @brief Reads an entry from the directory handle.
 * 
 * @param[In]  handle The directory handle to read an entry from.
 * @param[Out] entry  A pointer to a struct VaFsEntry that is filled with information if an entry is available. 
 * @return int Returns -1 on error or if no more entries are available (errno is set accordingly), 0 on success
 */
extern int vafs_directory_read(
    struct VaFsDirectoryHandle* handle,
    struct VaFsEntry*           entry);

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
    long                   offset,
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
