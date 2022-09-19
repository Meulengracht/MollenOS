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
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Virtual File Definitions & Structures
 * - This header describes the base virtual file-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __VFS_STORAGE_H__
#define __VFS_STORAGE_H__

#include <ddk/filesystem.h>
#include <ds/guid.h>
#include <ds/list.h>
#include <os/usched/mutex.h>
#include "filesystem_types.h"

#define __FILEMANAGER_MAXDISKS 64

enum StorageState {
    STORAGE_STATE_INITIALIZING,
    STORAGE_STATE_FAILED,
    STORAGE_STATE_DISCONNECTED,
    STORAGE_STATE_CONNECTED
};

typedef struct FileSystemStorage {
    element_t           Header;
    StorageDescriptor_t Storage;
    enum StorageState   State;
    struct usched_mtx   Lock;
    list_t              Filesystems;
} FileSystemStorage_t;

/**
 * Initializes the storage subsystem of the VFS manager
 */
extern void VFSStorageInitialize(void);

/**
 * @brief Registers a new filesystem of the given type, on the given disk with the given position on the disk
 * and assigns it an identifier.
 */
extern oserr_t
VFSStorageRegisterFileSystem(
        _In_ FileSystemStorage_t* storage,
        _In_ uint64_t             sector,
        _In_ uint64_t             sectorCount,
        _In_ enum FileSystemType  type,
        _In_ guid_t*              typeGuid,
        _In_ guid_t*              guid);

/**
 * @brief Detects the kind of layout on the disk, be it MBR or GPT layout, if there is no layout it returns
 * OsError to indicate the entire disk is a FS
 *__VFS_H__
 * @param Disk
 * @return
 */
extern oserr_t
VFSStorageParse(
        _In_ FileSystemStorage_t* fsStorage);

/**
 * @brief Detectes the kind of filesystem at the given absolute sector
 * with the given sector count. It then loads the correct driver
 * and installs it
 */
extern oserr_t
VfsStorageDetectFileSystem(
        _In_ FileSystemStorage_t* storage,
        _In_ uuid_t               bufferHandle,
        _In_ void*                buffer,
        _In_ uint64_t             sector,
        _In_ uint64_t             sectorCount);

/**
 * @brief Allocates a new disk identifier.
 * 
 * @param disk [In] The disk that should have an identifier allocated.
 * @return          UUID_INVALID if system is out of identifiers.
 */
extern uuid_t
VFSIdentifierAllocate(
    _In_ FileSystemStorage_t* fsStorage);

/**
 * @brief Frees an existing identifier that has been allocated
 * 
 * @param disk [In] The disk that was allocated the identifier
 * @param id   [In] The disk identifier to be freed
 */
extern void
VFSIdentifierFree(
        _In_ FileSystemStorage_t* storage,
        _In_ uuid_t               id);

/**
 *
 * @param storage
 * @param bufferHandle
 * @param sector
 * @param sectorCount
 * @param sectorsRead
 * @return
 */
extern oserr_t
VfsStorageReadHelper(
        _In_  FileSystemStorage_t* storage,
        _In_  uuid_t               bufferHandle,
        _In_  uint64_t             sector,
        _In_  size_t               sectorCount,
        _Out_ size_t*              sectorsRead);

#endif //!__VFS_STORAGE_H__
