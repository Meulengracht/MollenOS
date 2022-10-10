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
#include <os/dmabuf.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <vafs/directory.h>
#include <vafs/file.h>
#include <vafs/symlink.h>
#include <vafs/stat.h>
#include <vafs/vafs.h>

struct __ValiFSContext {
    struct VFSStorageParameters Storage;
    UInteger64_t                Position;
    DMAAttachment_t             Buffer;
    StorageDescriptor_t         Stats;
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

extern int __ValiFSHandleFilter(struct VaFs* vafs);

static long __ValiFS_Seek(void* userData, long offset, int whence);
static int  __ValiFS_Read(void* userData, void*, size_t, size_t*);

static struct VaFsOperations g_vafsOperations = {
        .seek = __ValiFS_Seek,
        .read = __ValiFS_Read,
        .write = NULL,  // We only support reading from this driver
        .close = NULL,  // We handle close ourselves
};

static struct __ValiFSContext* __ValiFSContextNew(
        _In_ struct VFSStorageParameters* storageParameters,
        _In_ StorageDescriptor_t*         storageStats)
{
    struct __ValiFSContext* context;
    DMABuffer_t             dma;
    DMAAttachment_t         dmaAttachment;
    oserr_t                 oserr;

    dma.name = "vafs_transfer_buffer";
    dma.type = DMA_TYPE_DRIVER_32LOW;
    dma.flags = 0;
    dma.length = MB(1);
    dma.capacity = MB(1);
    oserr = DmaCreate(&dma, &dmaAttachment);
    if (oserr != OsOK) {
        return NULL;
    }

    context = malloc(sizeof(struct __ValiFSContext));
    if (context == NULL) {
        return NULL;
    }

    memcpy(&context->Storage, storageParameters,sizeof(struct VFSStorageParameters));
    memcpy(&context->Stats, storageStats, sizeof(StorageDescriptor_t));
    memcpy(&context->Buffer, &dmaAttachment, sizeof(DMAAttachment_t));
    context->Position.QuadPart = 0;
    context->ValiFS = NULL;
    return context;
}

static void __ValiFSContextDelete(
        _In_ struct __ValiFSContext* context)
{
    if (context->ValiFS) {
        (void)vafs_close(context->ValiFS);
    }
    if (context->Buffer.buffer) {
        DmaAttachmentUnmap(&context->Buffer);
        DmaDetach(&context->Buffer);
    }
    free(context);
}

oserr_t
FsInitialize(
        _In_  struct VFSStorageParameters* storageParameters,
        _Out_ void**                       instanceData)
{
    StorageDescriptor_t     stats;
    struct __ValiFSContext* context;
    int                     status;
    oserr_t                 oserr;

    // Start out by stat'ing the storage device, we don't need
    // need to do any cleanup if this should fail.
    oserr = FSStorageStat(storageParameters, &stats);
    if (oserr != OsOK) {
        return oserr;
    }

    context = __ValiFSContextNew(storageParameters, &stats);
    if (context == NULL) {
        return OsOutOfMemory;
    }

    status = vafs_open_ops(
            &g_vafsOperations,
            context,
            &context->ValiFS
    );
    if (status) {
        __ValiFSContextDelete(context);
        return OsDeviceError;
    }

    status = __ValiFSHandleFilter(context->ValiFS);
    if (status) {
        __ValiFSContextDelete(context);
        return OsNotSupported;
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

    __ValiFSContextDelete(context);
    return OsOK;
}

static struct __ValiFSHandle* __ValiFSHandleNew(
        _In_ int   type,
        _In_ void* handleValue)
{
    struct __ValiFSHandle* handle;

    handle = malloc(sizeof(struct __ValiFSHandle));
    if (handle == NULL) {
        return NULL;
    }

    handle->Type = type;
    handle->Value.Raw = handleValue;
    return handle;
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
            free(cpath);
            if (status) {
                return OsInvalidParameters;
            }
            *dataOut = __ValiFSHandleNew(VALIFS_HANDLE_TYPE_DIR, dirHandle);
            return OsOK;
        } else if (errno == ENOENT) {
            return OsNotExists;
        } else {
            return OsError;
        }
    }
    free(cpath);
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
    _CRT_UNUSED(instanceData);
    _CRT_UNUSED(data);
    _CRT_UNUSED(name);
    _CRT_UNUSED(owner);
    _CRT_UNUSED(flags);
    _CRT_UNUSED(permissions);
    _CRT_UNUSED(dataOut);
    return OsNotSupported;
}

oserr_t
FsClose(
        _In_ void* instanceData,
        _In_ void* data)
{
    struct __ValiFSContext* context = instanceData;
    struct __ValiFSHandle*  handle  = data;
    _CRT_UNUSED(context);

    switch (handle->Type) {
        case VALIFS_HANDLE_TYPE_FILE:
            vafs_file_close(handle->Value.File);
            return OsOK;
        case VALIFS_HANDLE_TYPE_DIR:
            vafs_directory_close(handle->Value.Directory);
            return OsOK;
        case VALIFS_HANDLE_TYPE_SYMLINK:
            vafs_symlink_close(handle->Value.Symlink);
            return OsOK;
    }
    return OsNotSupported;
}

oserr_t
FsStat(
        _In_ void*             instanceData,
        _In_ struct VFSStatFS* stat)
{
    struct __ValiFSContext* context = instanceData;

    stat->MaxFilenameLength = VAFS_NAME_MAX;
    stat->BlockSize = context->Stats.SectorSize;
    stat->BlocksPerSegment = 1;
    stat->SegmentsTotal = 0;
    stat->SegmentsFree = 0;
    return OsOK;
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
    _CRT_UNUSED(instanceData);
    _CRT_UNUSED(data);
    _CRT_UNUSED(linkName);
    _CRT_UNUSED(linkTarget);
    _CRT_UNUSED(symbolic);
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
    _CRT_UNUSED(instanceData);
    _CRT_UNUSED(path);
    return OsNotSupported;
}

oserr_t
FsReadLink(
        _In_ void*       instanceData,
        _In_ mstring_t*  path,
        _In_ mstring_t** pathOut)
{
    struct __ValiFSContext*   context = instanceData;
    struct VaFsSymlinkHandle* handle;
    int                       status;
    char*                     cpath;

    cpath = mstr_u8(path);
    if (cpath == NULL) {
        return OsOutOfMemory;
    }

    status = vafs_symlink_open(context->ValiFS, cpath, &handle);
    free(cpath);
    if (status) {
        if (errno == ENOENT) {
            return OsNotExists;
        } else if (errno == EISDIR) {
            return OsPathIsDirectory;
        } else if (errno == ENOTDIR) {
            return OsPathIsNotDirectory;
        }
        return OsError;
    }

    cpath = malloc(VAFS_PATH_MAX);
    if (cpath == NULL) {
        vafs_symlink_close(handle);
        return OsOutOfMemory;
    }

    (void)vafs_symlink_target(handle, cpath, VAFS_PATH_MAX);
    vafs_symlink_close(handle);

    *pathOut = mstr_new_u8(cpath);
    free(cpath);
    return OsOK;
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
    _CRT_UNUSED(instanceData);
    _CRT_UNUSED(from);
    _CRT_UNUSED(to);
    _CRT_UNUSED(copy);
    return OsNotSupported;
}

static uint32_t __ConvertTypeToFlags(
        _In_ enum VaFsEntryType type)
{
    switch (type) {
        case VaFsEntryType_Directory: return FILE_FLAG_DIRECTORY;
        case VaFsEntryType_Symlink: return FILE_FLAG_LINK;
        default: break;
    }
    return 0;
}

static void __ConvertStatEntry(
        _In_ struct VFSStat*   out,
        _In_ struct VaFsEntry* in)
{
    memset(out, 0, sizeof(struct VFSStat));
    out->Name = mstr_new_u8(in->Name);
    out->Flags = __ConvertTypeToFlags(in->Type);
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
    struct __ValiFSContext* context = instanceData;
    struct __ValiFSHandle*  handle = data;
    _CRT_UNUSED(context);
    _CRT_UNUSED(bufferHandle);

    switch (handle->Type) {
        case VALIFS_HANDLE_TYPE_FILE:
            *unitsRead = vafs_file_read(
                    handle->Value.File,
                    ((char*)buffer + bufferOffset),
                    unitCount
            );
            return OsOK;
        case VALIFS_HANDLE_TYPE_DIR:
            size_t          entriesToRead = unitCount / sizeof(struct VFSStat);
            size_t          entriesLeft = entriesToRead;
            struct VFSStat* stat = buffer;
            while (entriesLeft) {
                struct VaFsEntry entry;
                int status = vafs_directory_read(
                        handle->Value.Directory,
                        &entry
                );
                if (status) {
                    break;
                }

                __ConvertStatEntry(stat, &entry);
                entriesLeft--;
                stat++;
            }
            *unitsRead = (entriesToRead - entriesLeft) * sizeof(struct VFSStat);
            return OsOK;
    }
    return OsNotSupported;
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
    _CRT_UNUSED(instanceData);
    _CRT_UNUSED(data);
    _CRT_UNUSED(bufferHandle);
    _CRT_UNUSED(buffer);
    _CRT_UNUSED(bufferOffset);
    _CRT_UNUSED(unitCount);
    _CRT_UNUSED(unitsWritten);
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
    _CRT_UNUSED(instanceData);
    _CRT_UNUSED(data);
    _CRT_UNUSED(size);
    return OsNotSupported;
}

oserr_t
FsSeek(
        _In_  void*     instanceData,
        _In_  void*     data,
        _In_  uint64_t  absolutePosition,
        _Out_ uint64_t* absolutePositionOut)
{
    struct __ValiFSContext* context = instanceData;
    struct __ValiFSHandle*  handle = data;
    _CRT_UNUSED(context);

    switch (handle->Type) {
        case VALIFS_HANDLE_TYPE_FILE:
            *absolutePositionOut = (uint64_t)vafs_file_seek(
                    handle->Value.File,
                    (long)absolutePosition,
                    SEEK_SET
            );
            return OsOK;
            // TODO VALIFS_HANDLE_TYPE_DIR:
    }
    return OsNotSupported;
}

static uint64_t __GetSize(struct __ValiFSContext* context) {
    uint64_t sectors = context->Stats.SectorCount - context->Storage.SectorStart.QuadPart;
    uint64_t bytes   = sectors * context->Stats.SectorSize;
    return bytes;
}

static long __ValiFS_Seek(void* userData, long offset, int whence)
{
    struct __ValiFSContext* context = userData;
    long                    pos     = (long)context->Position.QuadPart;

    if (offset == 0 && whence == SEEK_CUR) {
        return pos;
    }

    switch (whence) {
        case SEEK_CUR:
            pos += offset;
            break;
        case SEEK_SET:
            pos = offset;
            break;
        case SEEK_END:
            pos = (long)__GetSize(context);;
            pos += offset;
            break;
        default:
            break;
    }

    context->Position.QuadPart = (uint64_t)pos;
    return pos;
}

static int __ValiFS_Read(void* userData, void* buffer, size_t length, size_t* bytesRead)
{
    struct __ValiFSContext* context = userData;
    UInteger64_t            sector;
    size_t                  count;
    size_t                  sectorsRead;
    oserr_t                 oserr;

    // Translate position into sectors. If we are in the middle of a sector
    // according to the position, then we must read that amount of bytes extra
    // when translating
    sector.QuadPart = context->Position.QuadPart / context->Stats.SectorSize;

    // Translate the number of bytes to read into a sector count. Take into account
    // that we should include the offset into the current sector, and also make sure
    // to read an additional sector. Up to a limit of 1MB.
    count = (length + (context->Position.QuadPart % context->Stats.SectorSize));
    count += context->Stats.SectorSize;
    count /= context->Stats.SectorSize;
    count = MIN(count, MB(1));

    oserr = FSStorageRead(
            &context->Storage,
            context->Buffer.handle,
            0,
            &sector,
            count,
            &sectorsRead
    );
    if (oserr != OsOK) {
        return OsErrToErrNo(oserr);
    }

    // Translate the number of sectors read back into a byte-count.
    // We don't want to report more bytes read than requested, so adjust
    // for that.
    count = MIN(length, sectorsRead * context->Stats.SectorSize);
    memcpy(
            buffer,
            context->Buffer.buffer,
            count
    );
    return OsOK;
}
