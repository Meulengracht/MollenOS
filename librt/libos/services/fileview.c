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
#include <internal/_io.h>
#include <internal/_utils.h>
#include <os/services/file.h>
#include <os/handle.h>
#include <os/memory.h>
#include <os/shm.h>
#include <sys_file_service_client.h>

struct __FileView {
    element_t    header;
    OSHandle_t   shm;
    uuid_t       file_handle;
    unsigned int flags;
    size_t       offset;
    size_t       length;
};

static list_t g_fileViews = LIST_INIT;
static size_t g_pageSize  = 0;

static struct __FileView*
__GetFileView(
        _In_ uintptr_t virtualBase)
{
    if (!virtualBase) {
        return NULL;
    }

    foreach(element, &g_fileViews) {
        struct __FileView* fileView = element->value;
        void*             buffer   = SHMBuffer(&fileView->shm);
        if (ISINRANGE(virtualBase, (uintptr_t)buffer, (uintptr_t)buffer + fileView->length)) {
            return fileView;
        }
    }
    return NULL;
}

static inline uintptr_t
__GetPageSize(void)
{
    if (!g_pageSize) {
        SystemDescriptor_t descriptor;
        SystemQuery(&descriptor);

        g_pageSize = descriptor.PageSizeBytes;
    }
    return g_pageSize;
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

oserr_t
OSFileViewCreate(
        _In_  OSHandle_t*  handle,
        _In_  unsigned int flags,
        _In_  uint64_t     offset,
        _In_  size_t       length,
        _Out_ void**       mappingOut)
{
    oserr_t            oserr;
    struct __FileView*  fileView;
    size_t             fileOffset = offset & (__GetPageSize() - 1);
    SHM_t              shm;

    // Sanitize that the handle is valid
    if (handle->Type != OSHANDLE_FILE) {
        return OS_EINVALPARAMS;
    }

    fileView = malloc(sizeof(struct __FileView));
    if (fileView == NULL) {
        return OS_EOOM;
    }

    shm.Key    = NULL;
    shm.Type   = 0;
    shm.Access = __AccessFlags(flags);
    shm.Flags  = SHM_CLEAN | SHM_TRAP;
    shm.Size   = length;

    oserr = SHMCreate(&shm, &fileView->shm);
    if (oserr != OS_EOK) {
        free(fileView);
        return oserr;
    }

    ELEMENT_INIT(&fileView->header, 0, fileView);
    fileView->file_handle = handle->ID;
    fileView->offset = fileOffset;
    fileView->length = length;
    fileView->flags  = flags;

    list_append(&g_fileViews, &fileView->header);
    *mappingOut = SHMBuffer(&fileView->shm);
    return oserr;
}

oserr_t
OSFileViewFlush(
        _In_ void*        mapping,
        _In_ size_t       length,
        _In_ unsigned int flags)
{
    struct __FileView* fileView = __GetFileView((uintptr_t)mapping);
    UInteger64_t      fileOffset;
    oserr_t           oserr;
    int               pageCount;
    unsigned int*     attributes;

    if (fileView == NULL) {
        return OS_EINVALPARAMS;
    }

    // If the fileview was mapped as read-only then this makes no sense,
    // abort the operation with an error
    if (!(fileView->flags & SHM_ACCESS_WRITE)) {
        return OS_EPERMISSIONS;
    }

    pageCount  = DIVUP(MIN(fileView->length, length), __GetPageSize());
    attributes = malloc(sizeof(unsigned int) * pageCount);
    if (!attributes) {
        oserr = OS_EOOM;
        goto exit;
    }

    oserr = MemoryQueryAttributes(
            SHMBuffer(&fileView->shm),
            fileView->length,
            &attributes[0]
    );
    if (oserr != OS_EOK) {
        goto exit;
    }

    fileOffset.QuadPart = fileView->offset;
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

            sys_file_transfer_absolute(GetGrachtClient(), &msg.base, __crt_process_id(), fileView->file_handle,
                                       1, fileOffset.u.LowPart, fileOffset.u.HighPart, fileView->shm.ID,
                                       i * __GetPageSize(), dirtyPageCount * __GetPageSize());
            gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
            sys_file_transfer_absolute_result(GetGrachtClient(), &msg.base, &oserr, &bytesTransferred);

            // update iterator values and account for the auto inc
            i = j;
            fileOffset.QuadPart += (uint64_t)dirtyPageCount * (uint64_t)__GetPageSize();
        }
        else { i++; fileOffset.QuadPart += __GetPageSize(); }
    }

exit:
    return oserr;
}

oserr_t
OSFileViewUnmap(
        _In_ void*  mapping,
        _In_ size_t length)
{
    struct __FileView* fileView = __GetFileView((uintptr_t)mapping);
    oserr_t           oserr;
    if (!fileView) {
        return OS_EINVALPARAMS;
    }

    // is the mapping write enabled, then flush pages
    if (fileView->flags & FILEVIEW_WRITE) {
        (void)OSFileViewFlush(mapping, fileView->length, 0);
    }

    oserr = SHMUnmap(&fileView->shm);
    if (oserr != OS_EOK) {
        // ignore for now
    }

    OSHandleDestroy(&fileView->shm);
    list_remove(&g_fileViews, &fileView->header);
    free(fileView);
    return oserr;
}

oserr_t
HandleMemoryMappingEvent(
        _In_ int   signal,
        _In_ void* vaddressPtr)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetFileService());
    struct __FileView*        fileView       = __GetFileView((uintptr_t)vaddressPtr);
    uintptr_t                virtualAddress = (uintptr_t)vaddressPtr;
    UInteger64_t             fileOffset;
    oserr_t                  oserr;
    size_t                   bytesTransferred;

    if (!fileView) {
        return OS_ENOENT;
    }

    // Prepare the memory that we want to fill
    virtualAddress &= (__GetPageSize() - 1);

    fileOffset.QuadPart = fileView->offset + (virtualAddress - (uintptr_t)SHMBuffer(&fileView->shm));
    oserr            = SHMCommit(
            &fileView->shm,
            (vaddr_t)vaddressPtr,
            __GetPageSize()
    );
    if (oserr != OS_EOK) {
        return OS_ENOENT;
    }

    // Now we perform the actual filling on the memory area
    sys_file_transfer_absolute(GetGrachtClient(), &msg.base, __crt_process_id(), fileView->file_handle,
                               0, fileOffset.u.LowPart, fileOffset.u.HighPart, fileView->shm.ID,
                               virtualAddress - (uintptr_t)SHMBuffer(&fileView->shm), __GetPageSize());
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    sys_file_transfer_absolute_result(GetGrachtClient(), &msg.base, &oserr, &bytesTransferred);
    return oserr;
}
