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
 */

#ifndef __VFS_STORAGE_H__
#define __VFS_STORAGE_H__

#include <ddk/filesystem.h>
#include <ds/guid.h>
#include <ds/list.h>
#include <os/usched/mutex.h>

struct FileSystem;

#define __FILEMANAGER_MAXDISKS 64

enum VFSStorageState {
    VFSSTORAGE_STATE_INITIALIZING,
    VFSSTORAGE_STATE_FAILED,
    VFSSTORAGE_STATE_DISCONNECTED,
    VFSSTORAGE_STATE_CONNECTED
};

struct VFSStorage;

struct VFSStorageOperations {
    void    (*Destroy)(void*);
    oserr_t (*Read)(void*, uuid_t, size_t, UInteger64_t*, size_t, size_t*);
    oserr_t (*Write)(void*, uuid_t, size_t, UInteger64_t*, size_t, size_t*);
};

struct VFSStorageProtocol {
    int StorageType;
    union {
        struct {
            uuid_t HandleID;
        } File;
        struct {
            uuid_t DeviceID;
            uuid_t DriverID;
        } Device;
        struct {
            uuid_t bufferHandle;
            size_t bufferOffset;
            void*  buffer;
            size_t size;
        } Memory;
    } Storage;
};

struct VFSStorage {
    element_t                   ListHeader;
    uuid_t                      ID;
    struct usched_mtx           Lock;
    enum VFSStorageState        State;
    struct VFSStorageProtocol   Protocol;
    struct VFSStorageOperations Operations;
    StorageDescriptor_t         Stats;
    void*                       Data;
    list_t                      Filesystems;
};

/**
 * Initializes the storage subsystem of the VFS manager
 */
extern void VFSStorageInitialize(void);

/**
 * @brief
 * @param operations
 * @return
 */
extern struct VFSStorage*
VFSStorageNew(
        _In_ struct VFSStorageOperations* operations);

/**
 * @brief
 * @param storage
 */
extern void
VFSStorageDelete(
        _In_ struct VFSStorage* storage);

/**
 * @brief
 * @param path
 * @return
 */
extern struct VFSStorage*
VFSStorageCreateFileBacked(
        _In_ uuid_t fileHandleID);

/**
 * @brief
 * @param deviceID
 * @param driverID
 * @param flags
 * @return
 */
extern struct VFSStorage*
VFSStorageCreateDeviceBacked(
        _In_ uuid_t       deviceID,
        _In_ uuid_t       driverID,
        _In_ unsigned int flags);

/**
 * @brief
 * @param bufferHandle
 * @param bufferOffset
 * @param buffer
 * @param size
 * @return
 */
extern struct VFSStorage*
VFSStorageCreateMemoryBacked(
        _In_ uuid_t bufferHandle,
        _In_ size_t bufferOffset,
        _In_ void*  buffer,
        _In_ size_t size);

/**
 * @brief Registers a new partiton on the storage provided. This will create and
 * add a new partition to the list of partitions. This does not initialize or setup
 * the partition.
 * @param storage
 * @param partitionIndex
 * @param sector
 * @param guid
 * @param fileSystemOut
 * @return
 */
extern oserr_t
VFSStorageRegisterPartition(
        _In_  struct VFSStorage*  storage,
        _In_  int                 partitionIndex,
        _In_  UInteger64_t*       sector,
        _In_  guid_t*             guid,
        _Out_ struct FileSystem** fileSystemOut);

/**
 * @brief
 * @param storage
 * @param partitionIndex
 * @param sector
 * @param guid
 * @param typeHint
 * @param typeGuid
 * @param interfaceDriverID
 * @param mountPoint
 * @return
 */
extern oserr_t
VFSStorageRegisterAndSetupPartition(
        _In_ struct VFSStorage*  storage,
        _In_ int                 partitionIndex,
        _In_ UInteger64_t*       sector,
        _In_ guid_t*             guid,
        _In_ const char*         typeHint,
        _In_ guid_t*             typeGuid,
        _In_ uuid_t              interfaceDriverID,
        _In_ mstring_t*          mountPoint);

/**
 * @brief Detects the kind of layout on the disk, be it MBR or GPT layout, if there is no layout it returns
 * OsError to indicate the entire disk is a FS
 * @param Disk
 * @return
 */
extern oserr_t
VFSStorageParse(
        _In_ struct VFSStorage* storage);

/**
 * @brief
 * @param storage
 * @param bufferHandle
 * @param buffer
 * @param sector
 * @param fsHintOut
 * @return
 */
extern oserr_t
VFSStorageDeriveFileSystemType(
        _In_  struct VFSStorage* storage,
        _In_  uuid_t             bufferHandle,
        _In_  void*              buffer,
        _In_  UInteger64_t*      sector,
        _Out_ const char**       fsHintOut);

/**
 * @brief Detectes the kind of filesystem at the given absolute sector
 * with the given sector count. It then loads the correct driver
 * and installs it
 */
extern oserr_t
VFSStorageDetectFileSystem(
        _In_ struct VFSStorage* storage,
        _In_ uuid_t             bufferHandle,
        _In_ void*              buffer,
        _In_ UInteger64_t*      sector);

/**
 * @brief Allocates a new disk identifier.
 * 
 * @param disk [In] The disk that should have an identifier allocated.
 * @return          UUID_INVALID if system is out of identifiers.
 */
extern uuid_t
VFSIdentifierAllocate(
    _In_ struct VFSStorage* storage);

/**
 * @brief Frees an existing identifier that has been allocated
 * 
 * @param disk [In] The disk that was allocated the identifier
 * @param id   [In] The disk identifier to be freed
 */
extern void
VFSIdentifierFree(
        _In_ struct VFSStorage* storage,
        _In_ uuid_t             id);

#endif //!__VFS_STORAGE_H__
