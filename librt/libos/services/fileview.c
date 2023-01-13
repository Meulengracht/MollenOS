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
#include <internal/_io.h>
#include <io.h>
#include <os/services/file.h>
#include <os/memory.h>
#include <os/shm.h>

struct file_view {
    element_t       header;
    SHMHandle_t     shm;
    uuid_t          file_handle;
    unsigned int    flags;
    size_t          offset;
    size_t          length;
};

static list_t g_fileViews = LIST_INIT;
static size_t g_pageSize  = 0;

static struct file_view* __GetFileView(uintptr_t virtualBase)
{
    if (!virtualBase) {
        return NULL;
    }

    foreach(element, &g_fileViews) {
        struct file_view* fileView = element->value;
        if (ISINRANGE(virtualBase,
                      (uintptr_t)fileView->shm.Buffer,
                      (uintptr_t)fileView->shm.Buffer + fileView->length)) {
            return fileView;
        }
    }
    return NULL;
}

static inline uintptr_t __GetPageSize(void)
{
    if (!g_pageSize) {
        SystemDescriptor_t descriptor;
        SystemQuery(&descriptor);

        g_pageSize = descriptor.PageSizeBytes;
    }
    return g_pageSize;
}

oserr_t
CreateFileMapping(
    _In_  int      FileDescriptor,
    _In_  int      Flags,
    _In_  uint64_t Offset,
    _In_  size_t   Length,
    _Out_ void**   MemoryPointer)
{
    stdio_handle_t*    handle = stdio_handle_get(FileDescriptor);
    oserr_t            osStatus;
    struct file_view*  fileView;
    size_t             fileOffset = Offset & (__GetPageSize() - 1);
    SHM_t              shm;

    // Sanitize that the descritor is valid
    if (!handle || handle->object.type != STDIO_HANDLE_FILE) {
        return OS_EINVALPARAMS;
    }

    fileView = malloc(sizeof(struct file_view));
    if (!fileView) {
        return OS_EOOM;
    }

    // parse flags
    if (Flags & FILE_MAPPING_READ) { }
    if (Flags & FILE_MAPPING_WRITE) { }
    if (Flags & FILE_MAPPING_EXECUTE) { }

    shm.Key    = NULL;
    shm.Type   = 0;
    shm.Access = SHM_ACCESS_READ | SHM_ACCESS_WRITE;
    shm.Flags  = SHM_CLEAN | SHM_TRAP;
    shm.Size   = Length;

    osStatus = SHMCreate(&shm, &fileView->shm);
    if (osStatus != OS_EOK) {
        free(fileView);
        return osStatus;
    }

    ELEMENT_INIT(&fileView->header, 0, fileView);
    fileView->file_handle = handle->object.handle;
    fileView->offset = fileOffset;
    fileView->length = Length;
    fileView->flags  = Flags;

    list_append(&g_fileViews, &fileView->header);

    *MemoryPointer = fileView->shm.Buffer;
    return osStatus;
}

oserr_t FlushFileMapping(
        _In_ void*  MemoryPointer,
        _In_ size_t Length)
{
    struct file_view* fileView = __GetFileView((uintptr_t)MemoryPointer);
    UInteger64_t   fileOffset;
    oserr_t        osStatus;
    int               pageCount;
    unsigned int*     attributes;
    if (!fileView) {
        return OS_EINVALPARAMS;
    }

    if (!(fileView->flags & FILE_MAPPING_WRITE)) {
        return OS_ENOTSUPPORTED;
    }

    pageCount  = DIVUP(MIN(fileView->length, Length), __GetPageSize());
    attributes = malloc(sizeof(unsigned int) * pageCount);
    if (!attributes) {
        osStatus = OS_EOOM;
        goto exit;
    }

    osStatus = MemoryQueryAttributes(fileView->shm.Buffer, fileView->length, &attributes[0]);
    if (osStatus != OS_EOK) {
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
            sys_file_transfer_absolute_result(GetGrachtClient(), &msg.base, &osStatus, &bytesTransferred);

            // update iterator values and account for the auto inc
            i = j;
            fileOffset.QuadPart += (uint64_t)dirtyPageCount * (uint64_t)__GetPageSize();
        }
        else { i++; fileOffset.QuadPart += __GetPageSize(); }
    }

exit:
    return osStatus;
}

oserr_t
DestroyFileMapping(
        _In_ void* MemoryPointer)
{
    struct file_view* fileView = __GetFileView((uintptr_t)MemoryPointer);
    oserr_t        osStatus;
    if (!fileView) {
        return OS_EINVALPARAMS;
    }

    // is the mapping write enabled, then flush pages
    if (fileView->flags & FILE_MAPPING_WRITE) {
        (void)FlushFileMapping(MemoryPointer, fileView->length);
    }

    osStatus = SHMUnmap(&fileView->shm);
    if (osStatus != OS_EOK) {
        // ignore for now
    }

    osStatus = SHMDetach(&fileView->shm);
    if (osStatus != OS_EOK) {
        // ignore for now
    }

    list_remove(&g_fileViews, &fileView->header);
    free(fileView);
    return osStatus;
}

oserr_t
HandleMemoryMappingEvent(
        _In_ int   signal,
        _In_ void* vaddressPtr)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetFileService());
    struct file_view*        fileView       = __GetFileView((uintptr_t)vaddressPtr);
    uintptr_t                virtualAddress = (uintptr_t)vaddressPtr;
    UInteger64_t             fileOffset;
    oserr_t                  osStatus;
    size_t                   bytesTransferred;

    if (!fileView) {
        return OS_ENOENT;
    }

    // Prepare the memory that we want to fill
    virtualAddress &= (__GetPageSize() - 1);

    fileOffset.QuadPart = fileView->offset + (virtualAddress - (uintptr_t)fileView->shm.Buffer);
    osStatus            = SHMCommit(
            &fileView->shm,
            (vaddr_t)vaddressPtr,
            __GetPageSize()
    );
    if (osStatus != OS_EOK) {
        return OS_ENOENT;
    }

    // Now we perform the actual filling on the memory area
    sys_file_transfer_absolute(GetGrachtClient(), &msg.base, __crt_process_id(), fileView->file_handle,
                               0, fileOffset.u.LowPart, fileOffset.u.HighPart, fileView->shm.ID,
                               virtualAddress - (uintptr_t)fileView->shm.Buffer, __GetPageSize());
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    sys_file_transfer_absolute_result(GetGrachtClient(), &msg.base, &osStatus, &bytesTransferred);
    return osStatus;
}
