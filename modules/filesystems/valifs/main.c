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
 */

//#define __TRACE

#include <ddk/utils.h>
#include <fs/common.h>
#include <stdlib.h>
#include <string.h>
#include <vafs/directory.h>
#include <vafs/file.h>
#include <vafs/vafs.h>

struct __ValiFSContext {
    struct VFSStorageParameters Storage;
    UInteger64_t                Position;
    struct VaFs*                ValiFS;
};

enum {
    VALIFS_HANDLE_TYPE_FILE,
    VALIFS_HANDLE_TYPE_DIR,
    VALIFS_HANDLE_TYPE_SYMLINK
};

struct __ValiFSHandle {
    int Type;
    union {
        struct VaFsDirectoryHandle* Directory;
        struct VaFsFileHandle*      File;
        struct VaFsSymlinkHandle*   Symlink;
        void*                       Raw;
    } Value;
};

static long __ValiFS_Seek(void* userData, long offset, int whence);
static int  __ValiFS_Read(void* userData, void*, size_t, size_t*);

static struct VaFsOperations g_vafsOperations = {
        .seek = __ValiFS_Seek,
        .read = __ValiFS_Read,
        .write = NULL,  // We only support reading from this driver
        .close = NULL,  // We handle close ourselves
};

static struct __ValiFSContext* __ValiFSContextNew(
        _In_  struct VFSStorageParameters* storageParameters)
{
    struct __ValiFSContext* context;

    context = malloc(sizeof(struct __ValiFSContext));
    if (context == NULL) {
        return NULL;
    }
    memcpy(
            &context->Storage,
            storageParameters,
            sizeof(struct VFSStorageParameters)
    );
    context->Position.QuadPart = 0;
    return context;
}

oserr_t
FsInitialize(
        _In_  struct VFSStorageParameters* storageParameters,
        _Out_ void**                       instanceData)
{
    struct __ValiFSContext* context;
    int                     status;

    context = __ValiFSContextNew(storageParameters);
    if (context == NULL) {
        return OsOutOfMemory;
    }

    status = vafs_open_ops(
            &g_vafsOperations,
            context,
            &context->ValiFS
    );
    if (status) {
        return OsDeviceError;
    }

    *instanceData = context;
    return OsOK;
}

oserr_t
FsDestroy(
        _In_ void*         instanceData,
        _In_ unsigned int  unmountFlags)
{
    struct __ValiFSContext* context = instanceData;
    _CRT_UNUSED(unmountFlags);

    (void)vafs_close(context->ValiFS);
    free(context);
    return OsOK;
}

static struct __ValiFSHandle* __ValiFSHandleNew(
        _In_ int   type,
        _In_ void* handleValue)
{

}

oserr_t
FsOpen(
        _In_      void*      instanceData,
        _In_      mstring_t* path,
        _Out_Opt_ void**     dataOut)
{
    struct __ValiFSContext*     context = instanceData;
    struct VaFsFileHandle*      fileHandle;
    struct VaFsDirectoryHandle* dirHandle;
    int                         status;
    char*                       cpath;

    // Ok we use the same open call for both files and directories. So what we will
    // try to, is to open it as a file first, if that fails with EISDIR, then we open
    // it as a directory.
    // VaFS luckily tells us the difference with EEXIST and EISDIR.
    cpath = mstr_u8(path);
    if (cpath == NULL) {
        return OsOutOfMemory;
    }

    status = vafs_file_open(context->ValiFS, cpath, &fileHandle);
    if (status) {
        if (errno == EISDIR) {
            status = vafs_directory_open(context->ValiFS, cpath, &dirHandle);
            if (status) {
                return OsInvalidParameters;
            }
            *dataOut = __ValiFSHandleNew(VALIFS_HANDLE_TYPE_DIR, dirHandle);
            return OsOK;
        } else if (errno == EEXIST) {
            return OsNotExists;
        } else {
            return OsError;
        }
    }
    *dataOut = __ValiFSHandleNew(VALIFS_HANDLE_TYPE_FILE, fileHandle);
    return OsOK;
}

oserr_t
FsCreate(
        _In_  void*      instanceData,
        _In_  void*      data,
        _In_  mstring_t* name,
        _In_  uint32_t   owner,
        _In_  uint32_t   flags,
        _In_  uint32_t   permissions,
        _Out_ void**     dataOut)
{
    // File creation is not supported when reading ValiFS's. The only case this
    // is supported is during image creation.
    return OsNotSupported;
}

oserr_t
FsClose(
        _In_ void* instanceData,
        _In_ void* data)
{

}

oserr_t
FsStat(
        _In_ void*             instanceData,
        _In_ struct VFSStatFS* stat)
{

}

oserr_t
FsLink(
        _In_ void*      instanceData,
        _In_ void*      data,
        _In_ mstring_t* linkName,
        _In_ mstring_t* linkTarget,
        _In_ int        symbolic)
{
    // File creation is not supported when reading ValiFS's. The only case this
    // is supported is during image creation.
    return OsNotSupported;
}

oserr_t
FsUnlink(
        _In_ void*      instanceData,
        _In_ mstring_t* path)
{
    // File-removal is not supported on ValiFS's. The images are read-only
    // and the only supported operation is reading. During image creation
    // removal is still not supported.
    return OsNotSupported;
}

oserr_t
FsReadLink(
        _In_ void*      instanceData,
        _In_ mstring_t* path,
        _In_ mstring_t* pathOut)
{

}

oserr_t
FsMove(
        _In_ void*      instanceData,
        _In_ mstring_t* from,
        _In_ mstring_t* to,
        _In_ int        copy)
{
    // File creation is not supported when reading ValiFS's. The only case this
    // is supported is during image creation.
    return OsNotSupported;
}

oserr_t
FsRead(
        _In_  void*   instanceData,
        _In_  void*   data,
        _In_  uuid_t  bufferHandle,
        _In_  void*   buffer,
        _In_  size_t  bufferOffset,
        _In_  size_t  unitCount,
        _Out_ size_t* unitsRead)
{

}

oserr_t
FsWrite(
        _In_  void*   instanceData,
        _In_  void*   data,
        _In_  uuid_t  bufferHandle,
        _In_  void*   buffer,
        _In_  size_t  bufferOffset,
        _In_  size_t  unitCount,
        _Out_ size_t* unitsWritten)
{
    // Writing to files are not supported, in this driver we only support read-only
    // images of ValiFS. The write operation is not implemented as it's not possible
    // to either create images or in general to modify them.
    return OsNotSupported;
}

oserr_t
FsTruncate(
        _In_ void*    instanceData,
        _In_ void*    data,
        _In_ uint64_t size)
{
    // File modifications are not supported when reading ValiFS's. The only case this
    // is supported is during image creation.
    return OsNotSupported;
}

oserr_t
FsSeek(
        _In_  void*     instanceData,
        _In_  void*     data,
        _In_  uint64_t  absolutePosition,
        _Out_ uint64_t* absolutePositionOut)
{

}


static long __ValiFS_Seek(void* userData, long offset, int whence)
{

}

static int  __ValiFS_Read(void* userData, void*, size_t, size_t*)
{

}
