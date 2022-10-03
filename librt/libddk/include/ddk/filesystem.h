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
#include <ds/mstring.h>
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

enum VFSStorageType {
    VFSSTORAGE_TYPE_DEVICE,
    VFSSTORAGE_TYPE_FILE
};

/**
 * @brief The data in VFSStorageParameters will be supplied once upon initialization
 * of the filesystem. It is expected that the FS itself stores these parameters to
 * use for storage operations.
 */
struct VFSStorageParameters {
    unsigned int StorageType;
    union {
        struct {
            uuid_t HandleID;
            // size_t SectorSize;
        } File;
        struct {
            uuid_t DriverID;
            uuid_t DeviceID;
        } Device;
    } Storage;
    unsigned int Flags;
    UInteger64_t SectorStart;
};

struct VFSStat {
    // These are filled in by the VFS
    uuid_t ID;
    uuid_t StorageID;

    mstring_t* Name;
    mstring_t* LinkTarget;
    uint32_t   Owner;
    uint32_t   Permissions; // Permissions come from os/file/types.h
    uint32_t   Flags;       // Flags come from os/file/types.h
    uint64_t   Size;

    struct timespec Accessed;
    struct timespec Modified;
    struct timespec Created;
};

struct VFSStatFS {
    // These are filled in by the VFS
    uuid_t     ID;
    mstring_t* Label;
    mstring_t* Serial;

    // These should be filled in by the underlying FS.
    uint32_t MaxFilenameLength;
    uint32_t BlockSize;
    uint32_t BlocksPerSegment;
    uint64_t SegmentsTotal;
    uint64_t SegmentsFree;
};

/**
 * @brief Initializes a new instance of the given filesystem.
 * @param setupParameters The parameters for how the filesystem can interact with the underlying storage device.
 * @param instanceData    This should be set to whatever context the fileystem expects to be passed for each operation
 *                        done on the filesystem.
 * @return Status of the operation
 */
__FSAPI oserr_t
__FSDECL(FsInitialize)(
        _In_  struct VFSStorageParameters* storageParameters,
        _Out_ void**                       instanceData);

/* FsDestroy 
 * Destroys the given filesystem descriptor and cleans
 * up any resources allocated by the filesystem instance */
__FSAPI oserr_t
__FSDECL(FsDestroy)(
        _In_ void*         instanceData,
        _In_ unsigned int  unmountFlags);

__FSAPI oserr_t
__FSDECL(FsOpen)(
        _In_      void*      instanceData,
        _In_      mstring_t* path,
        _Out_Opt_ void**     dataOut);

__FSAPI oserr_t
__FSDECL(FsCreate)(
        _In_  void*      instanceData,
        _In_  void*      data,
        _In_  mstring_t* name,
        _In_  uint32_t   owner,
        _In_  uint32_t   flags,
        _In_  uint32_t   permissions,
        _Out_ void**     dataOut);

__FSAPI oserr_t
__FSDECL(FsClose)(
        _In_ void* instanceData,
        _In_ void* data);

__FSAPI oserr_t
__FSDECL(FsStat)(
        _In_ void*             instanceData,
        _In_ struct VFSStatFS* stat);

__FSAPI oserr_t
__FSDECL(FsLink)(
        _In_ void*      instanceData,
        _In_ void*      data,
        _In_ mstring_t* linkName,
        _In_ mstring_t* linkTarget,
        _In_ int        symbolic);

__FSAPI oserr_t
__FSDECL(FsUnlink)(
        _In_ void*      instanceData,
        _In_ mstring_t* path);

__FSAPI oserr_t
__FSDECL(FsReadLink)(
        _In_ void*      instanceData,
        _In_ mstring_t* path,
        _In_ mstring_t* pathOut);

__FSAPI oserr_t
__FSDECL(FsMove)(
        _In_ void*      instanceData,
        _In_ mstring_t* from,
        _In_ mstring_t* to,
        _In_ int        copy);

__FSAPI oserr_t
__FSDECL(FsRead)(
        _In_  void*   instanceData,
        _In_  void*   data,
        _In_  uuid_t  bufferHandle,
        _In_  void*   buffer,
        _In_  size_t  bufferOffset,
        _In_  size_t  unitCount,
        _Out_ size_t* unitsRead);

__FSAPI oserr_t
__FSDECL(FsWrite)(
        _In_  void*   instanceData,
        _In_  void*   data,
        _In_  uuid_t  bufferHandle,
        _In_  void*   buffer,
        _In_  size_t  bufferOffset,
        _In_  size_t  unitCount,
        _Out_ size_t* unitsWritten);

__FSAPI oserr_t
__FSDECL(FsTruncate)(
        _In_ void*    instanceData,
        _In_ void*    data,
        _In_ uint64_t size);

__FSAPI oserr_t
__FSDECL(FsSeek)(
        _In_  void*     instanceData,
        _In_  void*     data,
        _In_  uint64_t  absolutePosition,
        _Out_ uint64_t* absolutePositionOut);

#endif //!__DDK_FILESYSTEM_H_
