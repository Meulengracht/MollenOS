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
#include <os/usched/mutex.h>
#include <ddk/filesystem.h>

struct VFSStorage;
struct VFSInterface;

enum FileSystemState {
    FileSystemState_NO_INTERFACE,
    FileSystemState_CONNECTED,
    FileSystemState_MOUNTED,
};

typedef struct FileSystem {
    element_t             Header;
    uuid_t                ID;
    guid_t                GUID;
    struct VFSStorage*    Storage;
    int                   PartitionIndex;
    UInteger64_t          SectorStart;
    void*                 Data;
    enum FileSystemState  State;
    struct VFS*           VFS;
    struct VFSInterface*  Interface;
    uuid_t                MountHandle;
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
 * @param storage     The storage descriptor.
 * @param id          A numeric id of the filesystem.
 * @param guid        The unique identifier (GUID) of the filesystem.
 * @param sector      The start sector of the filesystem on the disk.
 * @param sectorCount The number of sectors (size) the filesystem spans.
 * @return
 */
extern FileSystem_t*
FileSystemNew(
        _In_ struct VFSStorage* storage,
        _In_ int                partitionIndex,
        _In_ UInteger64_t*      sector,
        _In_ uuid_t             id,
        _In_ guid_t*            guid);

/**
 * @brief
 *
 * @param fileSystem
 */
extern void
FileSystemDestroy(
        _In_ FileSystem_t* fileSystem);

/**
 * @brief
 *
 * @param guid
 * @return
 */
extern const char*
FileSystemParseGuid(
        _In_ guid_t* guid);

/**
 * @brief
 *
 * @param fileSystem
 * @param interface
 * @return
 */
extern oserr_t
VFSFileSystemConnectInterface(
        _In_ FileSystem_t*        fileSystem,
        _In_ struct VFSInterface* interface);

/**
 * @brief
 *
 * @param fileSystem
 * @param flags
 * @return
 */
extern oserr_t
VfsFileSystemDisconnectInterface(
        _In_ FileSystem_t* fileSystem,
        _In_ unsigned int  flags);

/**
 * @brief Mounts a previously registered filesystem at the provided mount point. If no mount point is provided
 * the default semantic for mount points is used, or default system mounts are automatically loaded.
 *
 * @param fileSystem A pointer to the filesystem that should be mounted.
 * @param mountPoint The path where the filesystem should be mounted.
 */
extern oserr_t
VFSFileSystemMount(
        _In_ FileSystem_t* fileSystem,
        _In_ mstring_t*    mountPoint);

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
