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

#define __TRACE

#define __need_quantity
#include <ddk/utils.h>
#include <ddk/service.h>
#include <fs/common.h>
#include <gracht/link/vali.h>
#include <internal/_utils.h>
#include <os/handle.h>
#include <os/services/process.h>
#include <os/shm.h>
#include <os/types/file.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <vafs/reader.h>
#include <vafs/stat.h>
#include <vafs/vafs.h>
#include <sys_file_service_client.h>

struct __ValiFSContext {
    struct FSBaseContext        BaseContext;
    struct VFSStorageParameters Storage;
    OSHandle_t                  Buffer;
    mtx_t                       BufferLock;
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
        struct VaFsDirectoryReader* Directory;
        struct VaFsObjectReader*    File;
        struct VaFsObjectReader*    Symlink;
        void*                       Raw;
    } Value;
};

extern void __ValiFSConfigureReader(struct VaFsReaderConfiguration*, struct VaFsCodec[2]);

static int __ValiFS_ReadAt(void*, uint64_t, void*, size_t, size_t*);
static int __ValiFS_GetSize(void*, uint64_t*);

static mstring_t g_label = mstr_const("valifs");
static struct VaFsReaderBackendOps g_vafsOperations = {
    .readAt = __ValiFS_ReadAt,
    .getSize = __ValiFS_GetSize,
    .close = NULL, // The filesystem context owns the storage resources.
};

static oserr_t __ValiFSError(int error)
{
    switch (error) {
        case ENOENT: return OS_ENOENT;
        case ENOMEM: return OS_EOOM;
        case EISDIR: return OS_EISDIR;
        case ENOTDIR: return OS_ENOTDIR;
        case EINVAL: return OS_EINVALPARAMS;
        default:     return OS_EUNKNOWN;
    }
}

static struct __ValiFSContext* __ValiFSContextNew(
    _In_ struct VFSStorageParameters* storageParameters,
    _In_ StorageDescriptor_t*         storageStats)
{
    struct __ValiFSContext* context;
    OSHandle_t              shm;
    oserr_t                 oserr;

    oserr = SHMCreate(
        &(SHM_t) {
            .Flags = SHM_DEVICE,
            .Conformity = OSMEMORYCONFORMITY_LOW, // TODO: Use conformity from device
            .Size = MB(1),
            .Access = SHM_ACCESS_READ | SHM_ACCESS_WRITE
        },
        &shm
    );
    if (oserr != OS_EOK) {
        return NULL;
    }

    context = malloc(sizeof(struct __ValiFSContext));
    if (context == NULL) {
        OSHandleDestroy(&shm);
        return NULL;
    }
    
    if (mtx_init(&context->BufferLock, mtx_plain) != thrd_success) {
        OSHandleDestroy(&shm);
        free(context);
        return NULL;
    }

    memcpy(&context->Storage, storageParameters,sizeof(struct VFSStorageParameters));
    memcpy(&context->Stats, storageStats, sizeof(StorageDescriptor_t));
    memcpy(&context->Buffer, &shm, sizeof(OSHandle_t));
    context->ValiFS = NULL;
    return context;
}

static void __ValiFSContextDelete(
    _In_ struct __ValiFSContext* context)
{
    if (context->ValiFS) {
        (void)vafs_reader_close(context->ValiFS);
    }
    OSHandleDestroy(&context->Buffer);
    mtx_destroy(&context->BufferLock);
    free(context);
}

oserr_t
FsInitialize(
    _In_  struct VFSStorageParameters* storageParameters,
    _Out_ void**                       instanceData)
{
    struct VaFsReaderConfiguration configuration;
    struct VaFsCodec               codecs[2];
    StorageDescriptor_t            stats;
    struct __ValiFSContext*        context;
    int                            status;
    oserr_t                        oserr;

    // Start out by stat'ing the storage device, we don't need
    // to do any cleanup if this should fail.
    oserr = FSStorageStat(storageParameters, &stats);
    if (oserr != OS_EOK) {
        return oserr;
    }

    context = __ValiFSContextNew(storageParameters, &stats);
    if (context == NULL) {
        return OS_EOOM;
    }

    // Initialize the base context before any operations
    oserr = FSBaseContextInitialize(&context->BaseContext, storageParameters);
    if (oserr != OS_EOK) {
        ERROR("FsInitialize failed to initialize base context");
        __ValiFSContextDelete(context);
        return oserr;
    }

    __ValiFSConfigureReader(&configuration, codecs);
    status = vafs_reader_open_ops(
        &g_vafsOperations,
        context,
        &configuration,
        &context->ValiFS
    );
    if (status) {
        ERROR("FsInitialize failed to load vafs: %i [%i]", status, errno);
        __ValiFSContextDelete(context);
        return OS_EDEVFAULT;
    }

    *instanceData = context;
    return OS_EOK;
}

oserr_t
FsDestroy(
    _In_ void*         instanceData,
    _In_ unsigned int  unmountFlags)
{
    struct __ValiFSContext* context = instanceData;
    _CRT_UNUSED(unmountFlags);

    __ValiFSContextDelete(context);
    return OS_EOK;
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
    struct VaFsObjectReader*    fileHandle;
    struct VaFsDirectoryReader* dirHandle;
    struct VaFsMetadata         metadata;
    struct __ValiFSHandle*      handle;
    int                         status;
    char*                       cpath;

    cpath = mstr_u8(path);
    if (cpath == NULL) {
        return OS_EOOM;
    }

    // Object readers also open directories; inspect the type explicitly.
    status = vafs_object_reader_open(context->ValiFS, cpath, VaFsLookup_None, &fileHandle);
    if (status) {
        free(cpath);
        return __ValiFSError(errno);
    }

    status = vafs_object_reader_stat(fileHandle, &metadata);
    if (status) {
        vafs_object_reader_close(fileHandle);
        free(cpath);
        return __ValiFSError(errno);
    }
    
    if (metadata.Type == VaFsEntryType_Directory) {
        vafs_object_reader_close(fileHandle);
        status = vafs_directory_reader_open(context->ValiFS, cpath, VaFsLookup_None, &dirHandle);
        free(cpath);
        if (status) {
            return __ValiFSError(errno);
        }
        
        handle = __ValiFSHandleNew(VALIFS_HANDLE_TYPE_DIR, dirHandle);
        if (!handle) {
            vafs_directory_reader_close(dirHandle);
            return OS_EOOM;
        }
    } else {
        free(cpath);
        if (metadata.Type != VaFsEntryType_File && metadata.Type != VaFsEntryType_Hardlink) {
            vafs_object_reader_close(fileHandle);
            return OS_ENOTSUPPORTED;
        }
        
        handle = __ValiFSHandleNew(VALIFS_HANDLE_TYPE_FILE, fileHandle);
        if (!handle) {
            vafs_object_reader_close(fileHandle);
            return OS_EOOM;
        }
    }

    *dataOut = handle;
    return OS_EOK;
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
    return OS_ENOTSUPPORTED;
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
            vafs_object_reader_close(handle->Value.File);
            free(handle);
            return OS_EOK;
        case VALIFS_HANDLE_TYPE_DIR:
            vafs_directory_reader_close(handle->Value.Directory);
            free(handle);
            return OS_EOK;
        case VALIFS_HANDLE_TYPE_SYMLINK:
            vafs_object_reader_close(handle->Value.Symlink);
            free(handle);
            return OS_EOK;
    }
    return OS_ENOTSUPPORTED;
}

oserr_t
FsStat(
    _In_ void*             instanceData,
    _In_ struct VFSStatFS* stat)
{
    struct __ValiFSContext* context = instanceData;

    stat->Label = &g_label;
    stat->MaxFilenameLength = VAFS_NAME_MAX;
    stat->BlockSize = context->Stats.SectorSize;
    stat->BlocksPerSegment = 1;
    stat->SegmentsTotal = 0;
    stat->SegmentsFree = 0;
    return OS_EOK;
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
    return OS_ENOTSUPPORTED;
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
    return OS_ENOTSUPPORTED;
}

oserr_t
FsReadLink(
    _In_ void*       instanceData,
    _In_ mstring_t*  path,
    _In_ mstring_t** pathOut)
{
    struct __ValiFSContext*  context = instanceData;
    struct VaFsMetadata      metadata;
    struct VaFsObjectReader* handle;
    int                      status;
    char*                    cpath;
    uint64_t                 length;

    cpath = mstr_u8(path);
    if (cpath == NULL) {
        return OS_EOOM;
    }

    status = vafs_object_reader_open(context->ValiFS, cpath, VaFsLookup_NoFollow, &handle);
    free(cpath);
    if (status) {
        if (errno == ENOENT) {
            return OS_ENOENT;
        } else if (errno == EISDIR) {
            return OS_EISDIR;
        } else if (errno == ENOTDIR) {
            return OS_ENOTDIR;
        }
        return OS_EUNKNOWN;
    }

    cpath = malloc(VAFS_PATH_MAX);
    if (cpath == NULL) {
        vafs_object_reader_close(handle);
        return OS_EOOM;
    }

    length = vafs_object_reader_length(handle);
    if (vafs_object_reader_stat(handle, &metadata) != 0 ||
        metadata.Type != VaFsEntryType_Symlink || length >= VAFS_PATH_MAX ||
        vafs_object_reader_read(handle, cpath, length) != length) {
        vafs_object_reader_close(handle);
        free(cpath);
        return OS_EINVALPARAMS;
    }
    cpath[length] = '\0';
    vafs_object_reader_close(handle);

    *pathOut = mstr_new_u8(cpath);
    free(cpath);
    return OS_EOK;
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
    return OS_ENOTSUPPORTED;
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

static size_t
__WriteDirectoryEntry(
    _In_ uint8_t*          buffer,
    _In_ size_t            bufferSize,
    _In_ struct VaFsEntry* in)
{
    struct VFSDirectoryEntry* entry = (struct VFSDirectoryEntry*)buffer;
    char*                     data  = (char*)(buffer + sizeof(struct VFSDirectoryEntry));

    memset(entry, 0, sizeof(struct VFSDirectoryEntry));
    entry->NameLength = strlen(in->Name) + 1;
    entry->Flags = __ConvertTypeToFlags(in->Type);

    // guard against not enough space
    if (sizeof(struct VFSDirectoryEntry) + entry->NameLength > bufferSize) {
        return 0;
    }

    if (entry->NameLength) {
        memcpy(data, in->Name, entry->NameLength);
        data += entry->NameLength;
    }
    return sizeof(struct VFSDirectoryEntry) + entry->NameLength;
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
            *unitsRead = vafs_object_reader_read(
                    handle->Value.File,
                    ((char*)buffer + bufferOffset),
                    unitCount
            );
            if (*unitsRead == (size_t)-1) {
                *unitsRead = 0;
                return __ValiFSError(errno);
            }
            return OS_EOK;
        case VALIFS_HANDLE_TYPE_DIR:
            size_t   bytesToRead = unitCount;
            uint8_t* bufferPointer = buffer;
            while (bytesToRead > sizeof(struct VFSDirectoryEntry)) {
                struct VaFsEntry entry;
                size_t           bytesWritten;
                int status = vafs_directory_reader_next(
                        handle->Value.Directory,
                        &entry
                );
                if (status) {
                    break;
                }

                bytesWritten = __WriteDirectoryEntry(bufferPointer, bytesToRead, &entry);
                if (bytesWritten == 0) {
                    break;
                }
                bufferPointer += bytesWritten;
                bytesToRead   -= bytesWritten;
            }
            *unitsRead = (size_t)(bufferPointer - (uint8_t*)buffer);
            return OS_EOK;
    }
    return OS_ENOTSUPPORTED;
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
    return OS_ENOTSUPPORTED;
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
    return OS_ENOTSUPPORTED;
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
            if (absolutePosition > INT64_MAX) {
                return OS_EINVALPARAMS;
            }
            if (vafs_object_reader_seek(handle->Value.File, (int64_t)absolutePosition, SEEK_SET) != 0) {
                return __ValiFSError(errno);
            }
            *absolutePositionOut = absolutePosition;
            return OS_EOK;
            // TODO VALIFS_HANDLE_TYPE_DIR:
    }
    return OS_ENOTSUPPORTED;
}

static int __ValiFS_GetSize(
    _In_  void*     userData,
    _Out_ uint64_t* sizeOut)
{
    struct __ValiFSContext* context = userData;
    uint64_t                sectors;
    
    if (context->Storage.StorageType == VFSSTORAGE_TYPE_FILE) {
        struct vali_link_message   msg = VALI_MSG_INIT_HANDLE(GetFileService());
        struct sys_file_descriptor descriptor;
        oserr_t                    oserr;
        
        sys_file_fstat(
            GetGrachtClient(), &msg.base,
            OSProcessCurrentID(),
            context->Storage.Storage.File.HandleID
        );
        gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
        sys_file_fstat_result(GetGrachtClient(), &msg.base, &oserr, &descriptor);
        if (oserr != OS_EOK) {
            errno = OsErrToErrNo(oserr);
            return -1;
        }
        
        // File images need their exact byte size, including a partial sector.
        if (context->Storage.SectorStart.QuadPart > descriptor.size / 512) {
            errno = EINVAL;
            return -1;
        }
        *sizeOut = descriptor.size - context->Storage.SectorStart.QuadPart * 512;
        return 0;
    }

    if (!context->Stats.SectorSize || context->Storage.SectorStart.QuadPart > context->Stats.SectorCount) {
        errno = EINVAL;
        return -1;
    }
    
    sectors = context->Stats.SectorCount - context->Storage.SectorStart.QuadPart;
    if (sectors > UINT64_MAX / context->Stats.SectorSize) {
        errno = EOVERFLOW;
        return -1;
    }
    *sizeOut = sectors * context->Stats.SectorSize;
    return 0;
}

static int __ValiFS_ReadAtUnlocked(
    _In_  void*    userData,
    _In_  uint64_t offset,
    _In_  void*    buffer,
    _In_  size_t   length,
    _Out_ size_t*  bytesRead)
{
    struct __ValiFSContext* context = userData;
    uint64_t                imageSize;
    size_t                  sectorSize = context->Stats.SectorSize;
    
    *bytesRead = 0;
    
    if (__ValiFS_GetSize(context, &imageSize) != 0) {
        return -1;
    }

    if (offset >= imageSize || !length) {
        return 0;
    }
    
    if (sectorSize > MB(1)) {
        errno = EINVAL;
        return -1;
    }
    
    if (imageSize - offset < length) {
        length = (size_t)(imageSize - offset);
    }

    if (context->Storage.StorageType == VFSSTORAGE_TYPE_FILE) {
        while (*bytesRead < length) {
            struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetFileService());
            UInteger64_t             position = { .QuadPart = context->Storage.SectorStart.QuadPart * 512 + offset };
            size_t                   count = MIN(length - *bytesRead, MB(1));
            size_t                   transferred;
            oserr_t                  oserr;

            sys_file_transfer_absolute(
                GetGrachtClient(), &msg.base,
                OSProcessCurrentID(),
                context->Storage.Storage.File.HandleID,
                SYS_TRANSFER_DIRECTION_READ,
                position.u.LowPart,
                position.u.HighPart,
                context->Buffer.ID,
                0,
                count
            );
            gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
            sys_file_transfer_absolute_result(GetGrachtClient(), &msg.base, &oserr, &transferred);
            if (oserr != OS_EOK || !transferred || transferred > count) {
                errno = oserr == OS_EOK ? EIO : OsErrToErrNo(oserr);
                return -1;
            }
            
            memcpy((uint8_t*)buffer + *bytesRead, SHMBuffer(&context->Buffer), transferred);
            *bytesRead += transferred;
            offset += transferred;
        }
        return 0;
    }

    // Read in chunks that fit the shared sector buffer, including an unaligned
    // first sector. readAt must not depend on a cursor left by another stream.
    while (*bytesRead < length) {
        UInteger64_t sector = { .QuadPart = offset / sectorSize };
        size_t       skip = (size_t)(offset % sectorSize);
        size_t       capacity = (MB(1) / sectorSize) * sectorSize;
        size_t       count = MIN(length - *bytesRead, capacity - skip);
        size_t       sectors = (skip + count + sectorSize - 1) / sectorSize;
        size_t       sectorsRead;
        oserr_t      oserr;

        oserr = FSStorageRead(
            &context->Storage,
            context->Buffer.ID,
            0,
            &sector,
            sectors,
            &sectorsRead
        );
        if (oserr != OS_EOK) {
            errno = OsErrToErrNo(oserr);
            return -1;
        }
        if (sectorsRead > sectors || sectorsRead * sectorSize <= skip) {
            errno = EIO;
            return -1;
        }
        
        count = MIN(count, sectorsRead * sectorSize - skip);
        memcpy(
            (uint8_t*)buffer + *bytesRead,
            (uint8_t*)SHMBuffer(&context->Buffer) + skip,
            count
        );
        *bytesRead += count;
        offset += count;
    }
    return 0;
}

static int __ValiFS_ReadAt(
    _In_  void*    userData,
    _In_  uint64_t offset,
    _In_  void*    buffer,
    _In_  size_t   length,
    _Out_ size_t*  bytesRead)
{
    struct __ValiFSContext* context = userData;
    int                     status;
    
    if (mtx_lock(&context->BufferLock) != thrd_success) {
        *bytesRead = 0;
        errno = EBUSY;
        return -1;
    }
    
    status = __ValiFS_ReadAtUnlocked(userData, offset, buffer, length, bytesRead);
    mtx_unlock(&context->BufferLock);
    return status;
}
