/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Virtual File Definitions & Structures
 * - This header describes the base virtual file-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _VFS_INTERFACE_H_
#define _VFS_INTERFACE_H_

#include <ddk/filesystem.h>
#include <ds/mstring.h>
#include <ds/list.h>
#include <os/types/path.h>
#include <os/mollenos.h>

#define __FILEMANAGER_MAXDISKS 64

#define __FILE_OPERATION_NONE  0x00000000
#define __FILE_OPERATION_READ  0x00000001
#define __FILE_OPERATION_WRITE 0x00000002

typedef enum FileSystemType {
    FSUnknown = 0,
    FSFAT,
    FSEXFAT,
    FSNTFS,
    FSHFS,
    FSHPFS,
    FSMFS,
    FSEXT
} FileSystemType_t;

typedef enum FileSystemState {
    FSCreated,
    FSLoaded,
    FSUnloaded,
    FSError
} FileSystemState_t;

typedef struct FileSystemModule {
    element_t           header;
    FileSystemType_t    type;
    int                 references;
    Handle_t            handle;

    FsInitialize_t      Initialize;
    FsDestroy_t         Destroy;
    FsOpenEntry_t       OpenEntry;
    FsCreatePath_t      CreatePath;
    FsCloseEntry_t      CloseEntry;
    FsDeleteEntry_t     DeleteEntry;
    FsChangeFileSize_t  ChangeFileSize;
    FsOpenHandle_t      OpenHandle;
    FsCloseHandle_t     CloseHandle;
    FsReadEntry_t       ReadEntry;
    FsWriteEntry_t      WriteEntry;
    FsSeekInEntry_t     SeekInEntry;
} FileSystemModule_t;

typedef struct FileSystem {
    element_t              header;
    UUId_t                 id;
    FileSystemType_t       type;
    FileSystemState_t      state;
    MString_t*             identifier;
    FileSystemDescriptor_t descriptor;
    FileSystemModule_t*    module;
} FileSystem_t;

/**
 * Initializes the cache subsystem.
 */
__EXTERN void VfsCacheInitialize();

/**
 * Retrieves a file entry from cache, otherwise it is opened or created depending on options passed.
 * @param path    [In]  Path of the entry to open/create
 * @param options [In]  Open/creation options.
 * @param fileOut [Out] Pointer to storage where the pointer will be stored.
 * @return        Status of the operation
 */
__EXTERN OsStatus_t
VfsCacheGetFile(
        _In_  MString_t*          path,
        _In_  unsigned int        options,
        _Out_ FileSystemEntry_t** fileOut);

/**
 * Removes a file path from the cache if it exists.
 * @param path [In] The path of the file to remove.
 */
__EXTERN void
VfsCacheRemoveFile(
        _In_ MString_t* path);

/* DiskRegisterFileSystem 
 * Registers a new filesystem of the given type, on
 * the given disk with the given position on the disk 
 * and assigns it an identifier */
__EXTERN OsStatus_t
DiskRegisterFileSystem(
    _In_ FileSystemDisk_t*  disk,
    _In_ uint64_t           sector,
    _In_ uint64_t           sectorCount,
    _In_ FileSystemType_t   type);

/* DiskDetectFileSystem
 * Detectes the kind of filesystem at the given absolute sector 
 * with the given sector count. It then loads the correct driver
 * and installs it */
__EXTERN OsStatus_t DiskDetectFileSystem(FileSystemDisk_t *Disk,
    UUId_t BufferHandle, void* Buffer, uint64_t Sector, uint64_t SectorCount);

/* DiskDetectLayout
 * Detects the kind of layout on the disk, be it
 * MBR or GPT layout, if there is no layout it returns
 * OsError to indicate the entire disk is a FS */
__EXTERN OsStatus_t DiskDetectLayout(FileSystemDisk_t *Disk);

/**
 * Loads the appropriate filesystem driver for given type.
 * @param type [In] The type of filesystem to load.
 * @return     A handle for the given filesystem driver.
 */
__EXTERN FileSystemModule_t*
VfsLoadModule(
        _In_ FileSystemType_t type);

/**
 * Unloads the given module if its reference count reaches 0.
 * @param module [In] The module to release a reference on.
 */
__EXTERN void
VfsUnloadModule(
        _In_ FileSystemModule_t* module);

/* VfsGetFileSystems
 * Retrieves a list of all the current filesystems
 * and provides access for manipulation */
__EXTERN list_t* VfsGetFileSystems(void);

/**
 * Allocates a new disk identifier.
 * @param disk [In] The disk that should have an identifier allocated.
 * @return          UUID_INVALID if system is out of identifiers.
 */
__EXTERN UUId_t
VfsIdentifierAllocate(
    _In_ FileSystemDisk_t* disk);

/**
 * Frees an existing identifier that has been allocated
 * @param disk [In] The disk that was allocated the identifier
 * @param id   [In] The disk identifier to be freed
 */
__EXTERN void
VfsIdentifierFree(
    _In_ FileSystemDisk_t* disk,
    _In_ UUId_t            id);

#endif //!_VFS_INTERFACE_H_
