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
__FillData(
        _In_ struct __FileView* fileView,
        _In_ uint64_t           offset,
        _In_ size_t             length)
{
    UInteger64_t             fileOffset = { .QuadPart = fileView->Offset + offset };
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetFileService());
    size_t                   bytesTransferred;
    oserr_t                  oserr;

    sys_file_transfer_absolute(
            GetGrachtClient(),
            &msg.base,
            __crt_process_id(),
            fileView->FileHandle.ID,
            0,
            fileOffset.u.LowPart,
            fileOffset.u.HighPart,
            fileView->SHM.ID,
            offset,
            length
    );
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_file_transfer_absolute_result(
            GetGrachtClient(),
            &msg.base,
            &oserr,
            &bytesTransferred
    );
    return oserr;
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

    // Sanitize that the handle is valid
    if (handle->Type != OSHANDLE_FILE) {
        return OS_EINVALPARAMS;
    }

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
    UInteger64_t       fileOffset;
    size_t             bufferOffset;
    size_t             pageSize = MemoryPageSize();
    size_t             alignedLength = length;
    oserr_t            oserr;
    int                pageCount;
    unsigned int*      attributes;

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

    // If the fileview was mapped as read-only then this makes no sense,
    // abort the operation with an error
    if (!(fileView->Flags & SHM_ACCESS_WRITE)) {
        MutexUnlock(&g_fileViewsLock);
        return OS_EPERMISSIONS;
    }

    // TODO: In theory we should extract everything here we need or keep reference count...
    bufferOffset = ((uintptr_t)mapping - (uintptr_t)SHMBuffer(&fileView->SHM));
    fileOffset.QuadPart = fileView->Offset + bufferOffset;
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

    for (int i = 0; i < pageCount;) {
        int dirtyPageCount = 0;
        int j              = i;

        // detect sequential dirty pages to batch writes
        while (attributes[j] & MEMORY_DIRTY) {
            dirtyPageCount++;
            j++;
        }

        if (dirtyPageCount) {
            struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetFileService());
            size_t                   bytesTransferred;

            sys_file_transfer_absolute(
                    GetGrachtClient(),
                    &msg.base,
                    __crt_process_id(),
                    fileView->FileHandle.ID,
                    1,
                    fileOffset.u.LowPart,
                    fileOffset.u.HighPart,
                    fileView->SHM.ID,
                    bufferOffset + (i * pageSize),
                    dirtyPageCount * pageSize
            );
            gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
            sys_file_transfer_absolute_result(
                    GetGrachtClient(),
                    &msg.base,
                    &oserr,
                    &bytesTransferred
            );

            // update iterator values and account for the auto inc
            i = j;
            fileOffset.QuadPart += (uint64_t)dirtyPageCount * (uint64_t)pageSize;
        } else {
            i++;
            fileOffset.QuadPart += pageSize;
        }
    }

exit:
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

    // is the mapping write enabled, then flush pages
    if (fileView->Flags & FILEVIEW_WRITE) {
        MutexUnlock(&g_fileViewsLock);
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
