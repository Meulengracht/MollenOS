/**
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
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

typedef struct FileSystemBase {
    MString_t*          Label;
    StorageDescriptor_t Storage;
    unsigned int        Flags;
    uint64_t            SectorStart;
    uint64_t            SectorCount;
    void*               Data;
} FileSystemBase_t;

struct VFSStat {
    MString_t* Name;
    uint32_t   Owner;
    uint32_t   Permissions; // Permissions come from os/file/types.h
    uint32_t   Flags;       // Flags come from os/file/types.h
    uint64_t   Size;
};

struct VFSStatFS {
    MString_t* Label;
    uint64_t   BlocksTotal;
    uint64_t   BlocksFree;
};

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

__FSAPI OsStatus_t
__FSDECL(FsStat)(
        _In_ FileSystemBase_t* fileSystemBase,
        _In_ struct VFSStatFS* stat);

__FSAPI OsStatus_t
__FSDECL(FsOpen)(
        _In_      FileSystemBase_t* fileSystemBase,
        _In_      MString_t*        path,
        _Out_Opt_ void**            dataOut);

__FSAPI OsStatus_t
__FSDECL(FsCreate)(
        _In_  FileSystemBase_t* fileSystemBase,
        _In_  void*             data,
        _In_  MString_t*        name,
        _In_  uint32_t          owner,
        _In_  uint32_t          flags,
        _In_  uint32_t          permissions,
        _Out_ void**            dataOut);

__FSAPI OsStatus_t
__FSDECL(FsClose)(
        _In_ FileSystemBase_t* fileSystemBase,
        _In_ void*             data);

__FSAPI OsStatus_t
__FSDECL(FsLink)(
        _In_ FileSystemBase_t* fileSystemBase,
        _In_ void*             data,
        _In_ MString_t*        linkName,
        _In_ MString_t*        linkTarget,
        _In_ int               symbolic);

__FSAPI OsStatus_t
__FSDECL(FsUnlink)(
        _In_ FileSystemBase_t* fileSystemBase,
        _In_ MString_t*        path);

__FSAPI OsStatus_t
__FSDECL(FsMove)(
        _In_ FileSystemBase_t* fileSystemBase,
        _In_ MString_t*        from,
        _In_ MString_t*        to,
        _In_ int               copy);

__FSAPI OsStatus_t
__FSDECL(FsRead)(
        _In_  FileSystemBase_t* fileSystemBase,
        _In_  void*             data,
        _In_  UUId_t            bufferHandle,
        _In_  void*             buffer,
        _In_  size_t            bufferOffset,
        _In_  size_t            unitCount,
        _Out_ size_t*           unitsRead);

__FSAPI OsStatus_t
__FSDECL(FsWrite)(
        _In_  FileSystemBase_t* fileSystemBase,
        _In_  void*             data,
        _In_  UUId_t            bufferHandle,
        _In_  void*             buffer,
        _In_  size_t            bufferOffset,
        _In_  size_t            unitCount,
        _Out_ size_t*           unitsWritten);

__FSAPI OsStatus_t
__FSDECL(FsTruncate)(
        _In_ FileSystemBase_t* fileSystemBase,
        _In_ void*             data,
        _In_ uint64_t          size);

__FSAPI OsStatus_t
__FSDECL(FsSeek)(
        _In_  FileSystemBase_t* fileSystemBase,
        _In_  void*             data,
        _In_  uint64_t          absolutePosition,
        _Out_ uint64_t*         absolutePositionOut);

#endif //!__DDK_FILESYSTEM_H_
