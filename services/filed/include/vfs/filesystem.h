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
#include "requests.h"
#include "vfs.h"
#include "vfs_module.h"

enum FileSystemState {
    FileSystemState_CREATED,
    FileSystemState_MOUNTED,
    FileSystemState_UNMOUNTED,
    FileSystemState_ERROR
};

typedef struct FileSystem {
    element_t             Header;
    struct VFSCommonData  CommonData;
    enum FileSystemType   Type;
    enum FileSystemState  State;
    struct VFS*           VFS;
    struct VFSModule*     Module;
    struct VFSNode*       MountNode;
    struct usched_mtx     Lock;
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
FileSystemNew(
        _In_ StorageDescriptor_t* storage,
        _In_ uuid_t               id,
        _In_ guid_t*              guid,
        _In_ uint64_t             sector,
        _In_ uint64_t             sectorCount,
        _In_ struct VFSModule*    module);

/**
 *
 * @param guid
 * @return
 */
extern enum FileSystemType
FileSystemParseGuid(
        _In_ guid_t* guid);

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
extern oserr_t
VfsFileSystemUnmount(
        _In_ FileSystem_t* fileSystem,
        _In_ unsigned int  flags);

#endif //!__VFS_FILESYSTEM_H__
