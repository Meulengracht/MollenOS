/**
 * MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 * Filesystem Definitions & Structures
 * - This header describes the filesystem-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __DDK_FILESYSTEM_H_
#define __DDK_FILESYSTEM_H_

#include <ddk/storage.h>
#include <ds/list.h>
#include <os/mollenos.h>

/* FileSystem Export 
 * This is define the interface between user (filemanager)
 * and the implementer (the filesystem) */
#ifdef __FILEMANAGER_IMPL
#define __FSAPI            typedef
#define __FSDECL(Function) (*Function##_t)
#else
#define __FSAPI            CRTEXPORT
#define __FSDECL(Function) Function
#endif

typedef struct MString MString_t;

/**
 * FileSystem definitions
 * Used the describe the various possible flags for the given filesystem
 */
#define __FILESYSTEM_NONE 0           // disk is a standard storage drive
#define __FILESYSTEM_BOOT 0x00000001  // disk is system drive
#define __FILESYSTEM_DATA 0x00000002  // disk is system data drive
#define __FILESYSTEM_USER 0x00000004  // disk is user data drive

typedef struct FileSystemDisk {
    UUId_t              driver_id;
    UUId_t              device_id;
    unsigned int        flags;
    StorageDescriptor_t descriptor;
} FileSystemDisk_t;

typedef struct FileSystemBase {
    unsigned int     Flags;
    FileSystemDisk_t Disk;
    uint64_t         SectorStart;
    uint64_t         SectorCount;
    uintptr_t*       ExtensionData;
} FileSystemBase_t;

/* The shared filesystem entry structure
 * Used as a file-definition by the filemanager and the loaded filesystem modules */
typedef struct FileSystemEntryBase {
    OsFileDescriptor_t Descriptor;
    MString_t*         Name;
} FileSystemEntryBase_t;

/* This is the per-handle entry instance
 * structure, so multiple handles can be opened
 * on just a single entry, it refers to an entry structure */
typedef struct FileSystemHandleBase {
    unsigned int Access;
    unsigned int Options;
    uint64_t     Position;
    void*        OutBuffer;
    size_t       OutBufferPosition;
} FileSystemHandleBase_t;

/* FsInitialize 
 * Initializes a new instance of the file system
 * and allocates resources for the given descriptor */
__FSAPI OsStatus_t
__FSDECL(FsInitialize)(
        _In_ FileSystemBase_t* fileSystemBase);

/* FsDestroy 
 * Destroys the given filesystem descriptor and cleans
 * up any resources allocated by the filesystem instance */
__FSAPI OsStatus_t
__FSDECL(FsDestroy)(
        _In_ FileSystemBase_t* fileSystemBase,
        _In_ unsigned int      unmountFlags);

/* FsOpenEntry 
 * Fills the entry structure with information needed to access and manipulate the given path.
 * The entry can be any given type, file, directory, link etc. */
__FSAPI OsStatus_t
__FSDECL(FsOpenEntry)(
        _In_  FileSystemBase_t*       fileSystemBase,
        _In_  MString_t*              path,
        _Out_ FileSystemEntryBase_t** baseOut);

/* FsCreatePath 
 * Creates the path specified and fills the entry structure with similar information as
 * FsOpenEntry. This function (if success) acts like FsOpenEntry. The entry type is specified
 * by options and can be any type. */
__FSAPI OsStatus_t
__FSDECL(FsCreatePath)(
        _In_  FileSystemBase_t*       fileSystemBase,
        _In_  MString_t*              path,
        _In_  unsigned int            options,
        _Out_ FileSystemEntryBase_t** baseOut);

/* FsCloseEntry 
 * Releases resources allocated in the Open/Create function. If entry was opened in
 * exclusive access this is now released. */
__FSAPI OsStatus_t
__FSDECL(FsCloseEntry)(
        _In_ FileSystemBase_t*      fileSystemBase,
        _In_ FileSystemEntryBase_t* entryBase);

/* FsDeleteEntry 
 * Deletes the entry specified. If the entry is a directory it must be opened in
 * exclusive access to lock all subentries. Otherwise, this can result in zombie handles.
 * This also acts as a FsCloseHandle and FsCloseEntry. */
__FSAPI OsStatus_t
__FSDECL(FsDeleteEntry)(
        _In_ FileSystemBase_t*       fileSystemBase,
        _In_ FileSystemEntryBase_t*  entryBase);

/* FsOpenHandle 
 * Opens a new handle to a entry, this allows various interactions with the base entry, 
 * like read and write. Neccessary resources and initialization of the Handle
 * should be done here too */
__FSAPI OsStatus_t
__FSDECL(FsOpenHandle)(
        _In_  FileSystemBase_t*        fileSystemBase,
        _In_  FileSystemEntryBase_t*   entryBase,
        _Out_ FileSystemHandleBase_t** handleBaseOut);

/* FsCloseHandle 
 * Closes the entry handle and cleans up any resources allocated by the FsOpenHandle equivelent. 
 * Handle is not released by this function but should be cleaned up. */
__FSAPI OsStatus_t
__FSDECL(FsCloseHandle)(
        _In_ FileSystemBase_t*       fileSystemBase,
        _In_ FileSystemHandleBase_t* handleBase);

/* FsReadEntry 
 * Reads the requested number of units from the entry handle into the supplied buffer. This
 * can be handled differently based on the type of entry. */
__FSAPI OsStatus_t
__FSDECL(FsReadEntry)(
        _In_  FileSystemBase_t*       fileSystemBase,
        _In_  FileSystemEntryBase_t*  entryBase,
        _In_  FileSystemHandleBase_t* handleBase,
        _In_  UUId_t                  bufferHandle,
        _In_  void*                   buffer,
        _In_  size_t                  bufferOffset,
        _In_  size_t                  unitCount,
        _Out_ size_t*                 unitsRead);

/* FsWriteEntry
 * Writes the requested number of bytes to the given
 * file handle and outputs the number of bytes actually written */
__FSAPI OsStatus_t
__FSDECL(FsWriteEntry)(
        _In_  FileSystemBase_t*       fileSystemBase,
        _In_  FileSystemEntryBase_t*  entryBase,
        _In_  FileSystemHandleBase_t* handleBase,
        _In_  UUId_t                  bufferHandle,
        _In_  void*                   buffer,
        _In_  size_t                  bufferOffset,
        _In_  size_t                  unitCount,
        _Out_ size_t*                 unitsWritten);

/* FsSeekInEntry 
 * Seeks in the given entry-handle to the absolute position
 * given, must be within boundaries otherwise a seek won't take a place */
__FSAPI OsStatus_t
__FSDECL(FsSeekInEntry)(
        _In_ FileSystemBase_t*       fileSystemBase,
        _In_ FileSystemEntryBase_t*  entryBase,
        _In_ FileSystemHandleBase_t* handleBase,
        _In_ uint64_t                absolutePosition);

/* FsChangeFileSize
 * Either expands or shrinks the allocated space for the given
 * file-handle to the requested size. */
__FSAPI OsStatus_t
__FSDECL(FsChangeFileSize)(
        _In_ FileSystemBase_t*      fileSystemBase,
        _In_ FileSystemEntryBase_t* entryBase,
        _In_ uint64_t               size);

#endif //!__DDK_FILESYSTEM_H_
