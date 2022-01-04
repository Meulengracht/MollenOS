/**
 * Copyright 2021, Philip Meulengracht
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
 * You should have received a copy of the GNU General Public LicenseFileSystemCreate
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Virtual FileSystem Definitions & Structures
 * - This header describes the base virtual filesystem-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __VFS_FILESYSTEM_H__
#define __VFS_FILESYSTEM_H__

#include <ds/guid.h>
#include <ds/hashtable.h>
#include <ds/mstring.h>
#include <os/usched/usched.h>
#include <ddk/filesystem.h>
#include "filesystem_types.h"
#include "filesystem_module.h"
#include "requests.h"

#define __FILE_OPERATION_NONE  0x00000000
#define __FILE_OPERATION_READ  0x00000001
#define __FILE_OPERATION_WRITE 0x00000002

enum FileSystemState {
    FileSystemState_CREATED,
    FileSystemState_MOUNTED,
    FileSystemState_UNMOUNTED,
    FileSystemState_ERROR
};

typedef struct FileSystem {
    FileSystemBase_t base;
    element_t        header;

    UUId_t                 id;
    guid_t                 guid;
    enum FileSystemType    type;
    enum FileSystemState   state;

    MString_t*             mount_point;
    FileSystemModule_t*    module;

    struct usched_mtx      lock;
    hashtable_t            requests;
    hashtable_t            cache;
} FileSystem_t;

/**
 * Initializes the filesystem interface.
 */
extern void VfsFileSystemInitialize(void);

/**
 * @brief Creates a new filesystem instance from the parameters provided. This does not register
 * or mount the filesystem.
 *
 * @param disk        The (shared) filesystem descriptor.
 * @param id          A numeric id of the filesystem.
 * @param sector      The start sector of the filesystem on the disk.
 * @param sectorCount The number of sectors (size) the filesystem spans.
 * @param type        The filesystem type id.
 * @return
 */
extern FileSystem_t*
VfsFileSystemCreate(
        _In_ FileSystemDisk_t*   disk,
        _In_ UUId_t              id,
        _In_ uint64_t            sector,
        _In_ uint64_t            sectorCount,
        _In_ enum FileSystemType type,
        _In_ guid_t*             typeGuid,
        _In_ guid_t*             guid);

/**
 * @brief Mounts a previously registered filesystem at the provided mount point. If no mount point is provided
 * the default semantic for mount points is used, or default system mounts are automatically loaded.
 *
 * @param fileSystem A pointer to the filesystem that should be mounted.
 * @param mountPoint The path where the filesystem should be mounted.
 */
extern void
VfsFileSystemMount(
        _In_ FileSystem_t* fileSystem,
        _In_ MString_t*    mountPoint);

/**
 * @brief Unmounts the given filesystem. The flags can specify the type of unmount that is occuring.
 *
 * @param fileSystem A pointer to the filesystem that should be unmounted.
 * @param flags      The type of unmount that is occuring.
 */
extern void
VfsFileSystemUnmount(
        _In_ FileSystem_t* fileSystem,
        _In_ unsigned int  flags);

/**
 * @brief Registers a new filesystem request with the filesystem, so in case a filesystem becomes
 * unavailable it will automatically be cancelled.
 *
 * @param fileSystem The filesystem that the request is bound to.
 * @param request    The request that should be registered.
 */
extern void
VfsFileSystemRegisterRequest(
        _In_ FileSystem_t*        fileSystem,
        _In_ FileSystemRequest_t* request);

/**
 * @brief Unregisters a filesystem request.
 *
 * @param fileSystem The filesystem that the request is bound to.
 * @param request    The request that should be unregistered.
 */
extern void
VfsFileSystemUnregisterRequest(
        _In_ FileSystem_t*        fileSystem,
        _In_ FileSystemRequest_t* request);

/**
 * @brief Registers a new filesystem entry with the filesystem, so in case a filesystem becomes
 * unavailable it will automatically be cleaned.
 *
 * @param fileSystem The filesystem that the request is bound to.
 * @param handle     The file handle that should be registered.
 */
extern void
VfsFileSystemRegisterOpenEntry(
        _In_ FileSystem_t*          fileSystem,
        _In_ FileSystemEntryBase_t* entryBase);

/**
 * @brief Unregisters a filesystem file entry.
 *
 * @param fileSystem The filesystem that the request is bound to.
 * @param handle     The file handle that should be unregistered.
 */
extern void
VfsFileSystemUnregisterOpenEntry(
        _In_ FileSystem_t*          fileSystem,
        _In_ FileSystemEntryBase_t* entryBase);

/**
 * @brief Retrieves filesystem information about a currently open file handle.
 *
 * @param fileHandle  A file handle to query filesystem information from.
 * @param fileSystem  A pointer to where the filesystem pointer will be stored.
 * @return OsStatus_t Status of the lookup operation
 */
extern OsStatus_t
VfsFileSystemGetByFileHandle(
        _In_  UUId_t         fileHandle,
        _Out_ FileSystem_t** fileSystem);

/**
 * @brief Retrieves the filesystem that is associated with the path. It also returns the sub-path
 * that is relative to the start of that filesystem
 * @param path       Absolute path to resolve.
 * @param subPathOut Returns the remainder of the path after the initial mount is resolved.
 * @return           A pointer to the relevant filesystem.
 */
extern FileSystem_t*
VfsFileSystemGetByPath(
        _In_ MString_t*  path,
        _In_ MString_t** subPathOut);

/**
 * @brief Queries filesystem information from a filesystem path.
 *
 * @param path        A zero terminated string
 * @param fileSystem  A pointer to where the filesystem pointer will be stored.
 * @return OsStatus_t Status of the lookup operation
 */
extern OsStatus_t
VfsFileSystemGetByPathSafe(
        _In_ const char*    path,
        _In_ FileSystem_t** fileSystem);

/**
 * @brief Retrieves a system partition [vali-system, vali-data, vali-user] by specifying the flags
 * the partition should match. This is a speciality function.
 * @param partitionFlags The flags that the partition must match.
 * @return               A pointer to the relevant filesystem.
 */
extern FileSystem_t*
VfsFileSystemGetByFlags(
        _In_ unsigned int partitionFlags);

#endif //!__VFS_FILESYSTEM_H__
