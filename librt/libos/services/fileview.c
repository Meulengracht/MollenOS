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

#define __need_minmax
#include <ddk/convert.h>
#include <ds/list.h>
#include <gracht/link/vali.h>
#include <internal/_utils.h>
#include <os/services/file.h>
#include <os/handle.h>
#include <os/grants.h>
#include <string.h>
#include <os/memory.h>
#include <os/mutex.h>
#include <os/shm.h>
#include <sys_file_service_client.h>

struct __FileView {
    element_t    Header;
    OSHandle_t   SHM;
    OSHandle_t   FileHandle;
    unsigned int Flags;
    // Offset is a page-aligned offset into the file that is mapped.
    size_t Offset;
    // Length is the page-aligned length of the file mapping. It may or
    // may not spand the entire file.
    size_t Length;
};

static list_t  g_fileViews     = LIST_INIT;
static Mutex_t g_fileViewsLock = MUTEX_INIT(MUTEX_PLAIN);

static struct __FileView*
__GetFileView(
        _In_ uintptr_t virtualBase)
{
    if (!virtualBase) {
        return NULL;
    }

    foreach(element, &g_fileViews) {
        struct __FileView* fileView = element->value;
        void*              buffer   = SHMBuffer(&fileView->SHM);
        if (ISINRANGE(virtualBase, (uintptr_t)buffer, (uintptr_t)buffer + fileView->Length)) {
            return fileView;
        }
    }
    return NULL;
}

static unsigned int
__AccessFlags(
        _In_ unsigned int flags)
{
    unsigned int access = 0;
    if (flags & FILEVIEW_READ) {
        access |= SHM_ACCESS_READ;
    }
    if (flags & FILEVIEW_WRITE) {
        access |= SHM_ACCESS_WRITE;
    }
    if (flags & FILEVIEW_EXECUTE) {
        access |= SHM_ACCESS_EXECUTE;
    }
    return access;
}

static unsigned int
__SHMFlags(
        _In_ unsigned int flags)
{
    unsigned int shmflags = SHM_PRIVATE | SHM_CLEAN | SHM_TRAP;

    if (flags & FILEVIEW_SHARED) {
        shmflags &= ~(SHM_PRIVATE);
    }

    if (flags & FILEVIEW_COPYONWRITE) {
        // TODO: Implement support for copy-on-write mappings?
    }

    if (flags & FILEVIEW_32BIT) {
        // No-op, not really supported for default SHM mappings. Originally
        // this was designed for 64 bit mode where some older CPUs have
        // performance issues with higher memory.
    }

    if (flags & FILEVIEW_BIGPAGES) {
        if ((flags & FILEVIEW_BIGPAGES_2MB) == FILEVIEW_BIGPAGES_2MB) {
            shmflags |= SHM_BIGPAGES_2MB;
        } else if ((flags & FILEVIEW_BIGPAGES_1GB) == FILEVIEW_BIGPAGES_1GB) {
            shmflags |= SHM_BIGPAGES_1GB;
        }
    }
    return shmflags;
}

static struct __FileView*
__FileViewCreate(
        _In_  OSHandle_t*  handle,
        _In_  unsigned int flags,
        _In_  uint64_t     offset,
        _In_  size_t       length)
{
    struct __FileView* fileView;

    fileView = malloc(sizeof(struct __FileView));
    if (fileView == NULL) {
        return NULL;
    }

    ELEMENT_INIT(&fileView->Header, 0, fileView);
    memcpy(&fileView->FileHandle, handle, sizeof(OSHandle_t));
    fileView->Offset = offset;
    fileView->Length = length;
    fileView->Flags  = flags;
    return fileView;
}

static oserr_t
__TransferPrivateView(
    _In_ struct __FileView* fileView,
    _In_ size_t             offset,
    _In_ size_t             length,
    _In_ bool               write)
{
    OSHandle_t    temporary;
    OSHandle_t    local;
    SHMTransfer_t transfer = { .RangeMode = SHMTransferRangeSubrange };
    UInteger64_t position;
    size_t        capacity = MIN(length, 64 * 1024);
    size_t        pageSize = MemoryPageSize();
    size_t        completed = 0;
    size_t        requested;
    size_t        bytes;
    char*         localBuffer;
    char*         shared;
    oserr_t       status;

    capacity = ((capacity + pageSize - 1) / pageSize) * pageSize;
    
    status = SHMAttach(fileView->SHM.ID, &local);
    if (status != OS_EOK) {
        return status;
    }

    // Fill through a separate local mapping so a read-only file view keeps
    // its protection. The private backing is never included in a token.
    status = SHMMap(&local, offset, length, write ? SHM_ACCESS_READ :
                    SHM_ACCESS_READ | SHM_ACCESS_WRITE | SHM_ACCESS_COMMIT);
    if (status != OS_EOK) {
        goto releaseLocal;
    }
    
    localBuffer = SHMBuffer(&local);
    status = SHMCreate(
        &(SHM_t) {
            .Flags = SHM_COMMIT | SHM_CLEAN,
            .Access = SHM_ACCESS_READ | SHM_ACCESS_WRITE,
            .Size = capacity
        },
        &temporary
    );
    if (status != OS_EOK) {
        goto releaseLocal;
    }

    status = OSGrantTokenCreate(
        temporary.ID,
        &(GrantTokenOptions_t) {
            .ResourceType = GrantTokenResourceTypeSHM,
            .TargetType = GrantTokenTargetTypeAny,
            .TargetGlobalHandleID = UUID_INVALID,
            .Scope = write ? GrantTokenScopeRead : GrantTokenScopeRead | GrantTokenScopeWrite,
            .Data.SHM = { .Offset = 0, .Length = (uint32_t)capacity }
        },
        &transfer.Token
    );
    if (status != OS_EOK) {
        goto releaseTemporary;
    }

    shared = SHMBuffer(&temporary);

    while (completed < length) {
        requested = MIN(capacity, length - completed);
        
        memset(shared, 0, capacity);
        if (write) {
            memcpy(shared, localBuffer + completed, requested);
        }

        position.QuadPart = fileView->Offset + offset + completed;
        transfer.RequestedLength = (uint32_t)requested;
        
        status = OSTransferFileBufferAt(
            fileView->FileHandle.ID,
            &position,
            &transfer,
            write,
            &bytes
        );
        if (status != OS_EOK) {
            break;
        }

        if (!write) {
            memcpy(localBuffer + completed, shared, requested);
        }

        completed += requested;
        if (bytes < requested) {
            if (write) {
                status = OS_EINCOMPLETE;
            } else {
                memset(localBuffer + completed, 0, length - completed);
            }
            break;
        }
    }

    OSGrantTokenRevoke(transfer.Token.ControlHandleID);

releaseTemporary:
    OSHandleDestroy(&temporary);

releaseLocal:
    OSHandleDestroy(&local);
    return status;
}

static oserr_t
__TransferView(
    _In_ struct __FileView* fileView,
    _In_ size_t             offset,
    _In_ size_t             length,
    _In_ bool               write)
{
    UInteger64_t position;
    size_t       bytes;
    oserr_t      status;

    if (offset > fileView->Length || length > fileView->Length - offset) {
        return OS_EINVALPARAMS;
    }

    if (length == 0) {
        return OS_EOK;
    }

    if (fileView->Offset > UINT64_MAX - offset ||
        length > UINT64_MAX - (fileView->Offset + offset)) {
        return OS_EINVALPARAMS;
    }

    if (!(fileView->Flags & FILEVIEW_SHARED)) {
        return __TransferPrivateView(fileView, offset, length, write);
    }

    position.QuadPart = fileView->Offset + offset;
    
    status = OSTransferFileAt(
        fileView->FileHandle.ID,
        &position,
        fileView->SHM.ID,
        offset,
        write,
        length,
        &bytes
    );
    if (status == OS_EOK && write && bytes != length) {
        return OS_EINCOMPLETE;
    }
    return status;
}

static oserr_t
__FillData(
    _In_ struct __FileView* fileView,
    _In_ uint64_t           offset,
    _In_ size_t             length)
{
    oserr_t status;

    if (offset > SIZE_MAX) {
        return OS_EINVALPARAMS;
    }
    
    status = __TransferView(fileView, (size_t)offset, length, false);
    if (status != OS_EOK) {
        return status;
    }

    // Attach the populated backing pages to the original trap mapping.
    return SHMCommit(
        &fileView->SHM,
        (vaddr_t)SHMBuffer(&fileView->SHM) + (size_t)offset,
        length
    );
}

oserr_t
OSFileViewCreate(
    _In_  OSHandle_t*  handle,
    _In_  unsigned int flags,
    _In_  uint64_t     offset,
    _In_  size_t       length,
    _Out_ void**       mappingOut)
{
    size_t             pageSize = MemoryPageSize();
    size_t             alignedLength = length;
    oserr_t            oserr;
    struct __FileView* fileView;

    // Imported file references use the generic handle type. Filed validates
    // the reference and access on each transfer, including lazy page fills.

    // Error on offsets that are not page-size aligned.
    if (offset & (pageSize - 1)) {
        return OS_EINVALPARAMS;
    }

    // Make sure length is page-aligned
    if (alignedLength & (pageSize - 1)) {
        alignedLength = (alignedLength + (pageSize - 1)) & ~(pageSize - 1);
    }

    fileView = __FileViewCreate(handle, flags, offset, alignedLength);
    if (fileView == NULL) {
        return OS_EOOM;
    }

    oserr = SHMCreate(
        &(SHM_t) {
            .Access = __AccessFlags(flags),
            .Flags  = __SHMFlags(flags),
            .Size   = alignedLength
        },
        &fileView->SHM
    );
    if (oserr != OS_EOK) {
        free(fileView);
        return oserr;
    }

    MutexLock(&g_fileViewsLock);
    list_append(&g_fileViews, &fileView->Header);
    MutexUnlock(&g_fileViewsLock);

    // Increase the local reference count on the file handle to avoid
    // our own process closing the underlying file while we have mappings
    // for it
    oserr = OSHandlesAcquire(handle);
    if (oserr != OS_EOK) {
        (void)OSFileViewUnmap(SHMBuffer(&fileView->SHM), alignedLength);
        return oserr;
    }

    // Support read-ahead for the file by pre-filling the entire file.
    if (flags & FILEVIEW_POPULATE) {
        oserr = __FillData(fileView, 0, alignedLength);
        if (oserr != OS_EOK) {
            (void)OSFileViewUnmap(SHMBuffer(&fileView->SHM), alignedLength);
            return oserr;
        }
    }

    *mappingOut = SHMBuffer(&fileView->SHM);
    return oserr;
}

oserr_t
OSFileViewFlush(
    _In_ void*        mapping,
    _In_ size_t       length,
    _In_ unsigned int flags)
{
    struct __FileView* fileView;
    size_t             bufferOffset;
    size_t             pageSize = MemoryPageSize();
    size_t             alignedLength = length;
    oserr_t            oserr;
    int                pageCount;
    unsigned int*      attributes = NULL;

    // Error on addresses that are not page-size aligned.
    if ((uintptr_t)mapping & (pageSize - 1)) {
        return OS_EINVALPARAMS;
    }

    // Make sure length is page-aligned
    if (alignedLength & (pageSize - 1)) {
        alignedLength = (alignedLength + (pageSize - 1)) & ~(pageSize - 1);
    }

    MutexLock(&g_fileViewsLock);
    fileView = __GetFileView((uintptr_t)mapping);
    if (fileView == NULL) {
        MutexUnlock(&g_fileViewsLock);
        return OS_EINVALPARAMS;
    }

    if (alignedLength > fileView->Length - ((uintptr_t)mapping - (uintptr_t)SHMBuffer(&fileView->SHM))) {
        MutexUnlock(&g_fileViewsLock);
        return OS_EINVALPARAMS;
    }

    // If the fileview was mapped as read-only then this makes no sense,
    // abort the operation with an error
    if (!(fileView->Flags & SHM_ACCESS_WRITE)) {
        MutexUnlock(&g_fileViewsLock);
        return OS_EPERMISSIONS;
    }

    // TODO: In theory we should extract everything here we need or keep reference count...
    bufferOffset = ((uintptr_t)mapping - (uintptr_t)SHMBuffer(&fileView->SHM));
    MutexUnlock(&g_fileViewsLock);

    pageCount  = (int)(alignedLength / pageSize);
    attributes = malloc(sizeof(unsigned int) * pageCount);
    if (!attributes) {
        oserr = OS_EOOM;
        goto exit;
    }

    oserr = MemoryQueryAttributes(
            mapping,
            alignedLength,
            &attributes[0]
    );
    if (oserr != OS_EOK) {
        goto exit;
    }

    oserr = OS_EOK;
    for (int i = 0; i < pageCount;) {
        int dirtyPageCount = 0;
        int j              = i;

        // detect sequential dirty pages to batch writes
        while (j < pageCount && (attributes[j] & MEMORY_DIRTY)) {
            dirtyPageCount++;
            j++;
        }

        if (dirtyPageCount) {
            oserr = __TransferView(fileView, bufferOffset + (i * pageSize),
                                  dirtyPageCount * pageSize, true);
            if (oserr != OS_EOK) {
                goto exit;
            }

            // update iterator values and account for the auto inc
            i = j;
        } else {
            i++;
        }
    }

exit:
    free(attributes);
    return oserr;
}

oserr_t
OSFileViewUnmap(
        _In_ void*  mapping,
        _In_ size_t length)
{
    struct __FileView* fileView;
    oserr_t            oserr;

    MutexLock(&g_fileViewsLock);
    fileView = __GetFileView((uintptr_t)mapping);
    if (!fileView) {
        MutexUnlock(&g_fileViewsLock);
        return OS_EINVALPARAMS;
    }

    MutexUnlock(&g_fileViewsLock);

    // is the mapping write enabled, then flush pages
    if (fileView->Flags & FILEVIEW_WRITE) {
        oserr = OSFileViewFlush(mapping, length, 0);
        if (oserr != OS_EOK) {
            return oserr;
        }
    }

    oserr = SHMUnmap(&fileView->SHM, mapping, length);
    if (oserr != OS_EOK) {
        // We *may* receive a OS_EINCOMPLETE on partial frees of a memory region
        // and this is OK, this means we still have memory in play for the file, however
        // this freed memory is now inaccessible (completely). Accessing it will generate
        // a SIGSEGV.
        if (oserr == OS_EINCOMPLETE) {
            return OS_EOK;
        }
        return oserr;
    }

    MutexLock(&g_fileViewsLock);
    list_remove(&g_fileViews, &fileView->Header);
    MutexUnlock(&g_fileViewsLock);

    OSHandleDestroy(&fileView->SHM);
    OSHandleDestroy(&fileView->FileHandle);
    free(fileView);
    return oserr;
}

oserr_t
HandleMemoryMappingEvent(
        _In_ int   signal,
        _In_ void* faultAddress)
{
    struct __FileView* fileView;
    uintptr_t          address  = (uintptr_t)faultAddress;
    size_t             pageSize = MemoryPageSize();

    // Retrieve the file-mapping, but do so without the lock in this case
    fileView = __GetFileView((uintptr_t)faultAddress);
    if (!fileView) {
        return OS_ENOENT;
    }

    // Ensure page-aligned address before filling
    address &= ~(pageSize - 1);

    return __FillData(
        fileView,
        (address - (uintptr_t)SHMBuffer(&fileView->SHM)),
        pageSize
    );
}
