/**
 * Copyright 2023, Philip Meulengracht
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

// TODO
// Move reference keeping to OSHandle to ensure that OSHandle stays around enough
// Support shallow freeing of mappings in both shm and fileview
//

#include <errno.h>
#include <ds/list.h>
#include <os/handle.h>
#include <os/mollenos.h>
#include <os/shm.h>
#include <os/services/file.h>
#include <os/usched/mutex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

struct __mmap_item {
    element_t Header;

    // In the event that the fd get's closed while having mmap
    // mappings around, we must also keep track of this handle
    // so we can properly expose of it once there are no more.
    OSHandle_t OSHandle;

    // Also keep the io descriptor so we can verify whether or
    // not it's closed. -1 If the mapping is anonymous.
    int OriginalIOD;

    // The address and length of the mapping
    uintptr_t Address;
    size_t    Length;
};

static list_t            g_mmaps;
static struct usched_mtx g_mmapsLock;
static size_t            g_pageSize = 0;

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

void StdMmapInitialize(void)
{
    SystemDescriptor_t descriptor;
    SystemQuery(&descriptor);
    g_pageSize = descriptor.PageSizeBytes;
    list_construct(&g_mmaps);
    usched_mtx_init(&g_mmapsLock, USCHED_MUTEX_PLAIN);
}

static struct __mmap_item*
__get_mmap_from_addr(
        _In_ void* addr)
{
    struct __mmap_item* mi = NULL;
    usched_mtx_lock(&g_mmapsLock);
    foreach(i, &g_mmaps) {
        struct __mmap_item* item = i->value;
        if (ISINRANGE((uintptr_t)addr, item->Address, item->Address + item->Length)) {
            mi = item;
            break;
        }
    }
    usched_mtx_unlock(&g_mmapsLock);
    return mi;
}

static void
__add_mmap(
        _In_ OSHandle_t* handle,
        _In_ int         iod,
        _In_ uintptr_t   address,
        _In_ size_t      length)
{
    struct __mmap_item* mi = malloc(sizeof(struct __mmap_item));
    if (!mi) {
        return;
    }

    ELEMENT_INIT(&mi->Header, NULL, mi);
    memcpy(&mi->OSHandle, handle, sizeof(OSHandle_t));
    mi->OriginalIOD = iod;
    mi->Address = address;
    mi->Length = length;

    usched_mtx_lock(&g_mmapsLock);
    list_append(&g_mmaps, &mi->Header);
    usched_mtx_unlock(&g_mmapsLock);
}

static void
__get_anonymous_flags(
        _In_  int           prot,
        _In_  int           flags,
        _Out_ unsigned int* flagsOut,
        _Out_ unsigned int* typeOut,
        _Out_ unsigned int* accessOut)
{
    unsigned int mflags = SHM_PRIVATE | SHM_CLEAN;
    unsigned int type   = 0;
    unsigned int access = 0;

    // MAP_SHARED indicate that we create a mapping that can
    // be shared with others. This is the default of our SHM buffers
    // and can be turned off by the PRIVATE flag.
    if (flags & (MAP_SHARED | MAP_SHARED_VALIDATE)) {
        mflags &= ~(SHM_PRIVATE);
    }

    // MAP_PRIVATE indicates we get our own private copy-on-write copy
    // of the SHM buffer. However this makes no sense for anonymous files
    // so we simply no-op the flag for now
    if (flags & MAP_PRIVATE) {
        printf("mmap: MAP_PRIVATE unsupported in combination with MAP_ANONYMOUS");
    }

    if (flags & MAP_UNINITIALIZED) {
        mflags &= ~(SHM_CLEAN);
    }

    if (flags & MAP_POPULATE && !(flags & MAP_NONBLOCK)) {
        mflags |= SHM_COMMIT;
    }

    if (flags & MAP_GROWSDOWN) {
        // TODO: re-introduce SHM_STACK
    }

    if (flags & MAP_HUGETLB) {
        // TODO: support custom tlb sizes
    }

    if (flags & MAP_FIXED) {
        // TODO: support fixed addresses
    }

    if (flags & MAP_LOCKED) {
        // TODO: support locked memory when/if we introduce memory swaps.
        // Locked memory means that the memory can't be swapped.
    }

    if (flags & MAP_32BIT) {
        mflags |= SHM_DEVICE;
        type = SHM_TYPE_DRIVER_32;
    }

    if (prot & PROT_READ) {
        access |= SHM_ACCESS_READ;
    }
    if (prot & PROT_WRITE) {
        access |= SHM_ACCESS_WRITE;
    }
    if (prot & PROT_EXEC) {
        access |= SHM_ACCESS_EXECUTE;
    }

    *flagsOut = mflags;
    *typeOut = type;
    *accessOut = access;
}

static void*
__mmap_anonymous(
        _In_ void*  addr,
        _In_ size_t length,
        _In_ int    prot,
        _In_ int    flags)
{
    oserr_t    oserr;
    OSHandle_t osHandle;
    SHM_t      shm = {
            .Key = NULL,
            .Size = length
    };

    // TODO: when we support MAP_FIXED then we can use this parameter
    _CRT_UNUSED(addr);

    // Convert flags to SHM attributes
    __get_anonymous_flags(prot, flags, &shm.Flags, &shm.Type, &shm.Access);

    oserr = SHMCreate(&shm, &osHandle);
    if (oserr != OS_EOK) {
        (void)OsErrToErrNo(oserr);
        return NULL;
    }

    __add_mmap(&osHandle, -1, (uintptr_t)SHMBuffer(&osHandle), length);
    return SHMBuffer(&osHandle);
}

static void*
__mmap_file(
        _In_ void*  addr,
        _In_ size_t length,
        _In_ int    prot,
        _In_ int    flags,
        _In_ int    fd,
        _In_ off_t  offset)
{

}

// Unsupported flags in general
// MAP_FIXED + MAP_FIXED_NOREPLACE
// MAP_HUGETLB + MAP_HUGE_2MB/MAP_HUGE_1GB
// MAP_LOCKED
// MAP_NORESERVE
// MAP_SYNC
void*
mmap(
        _In_ void*  addr,
        _In_ size_t length,
        _In_ int    prot,
        _In_ int    flags,
        _In_ int    fd,
        _In_ off_t  offset)
{
    size_t pageSize = __GetPageSize();
    size_t alignedLength = length;

    // Error on offsets that are not page-size aligned.
    if (offset & (pageSize - 1)) {
        errno = EINVAL;
        return NULL;
    }

    // Align length with the page-size
    if (alignedLength & (pageSize - 1)) {
        alignedLength += pageSize - (alignedLength & (pageSize - 1));
    }

    if (flags & MAP_ANONYMOUS) {
        return __mmap_anonymous(addr, alignedLength, prot, flags);
    }
    return __mmap_file(addr, alignedLength, prot, flags, fd, offset);
}

int
munmap(
        _In_ void*  addr,
        _In_ size_t length)
{
    struct __mmap_item* mi;
    size_t pageSize = __GetPageSize();
    size_t alignedLength = length;

    // Error on addresses that are not page-size aligned.
    if ((uintptr_t)addr & (pageSize - 1)) {
        errno = EINVAL;
        return -1;
    }

    // Align length with the page-size
    if (alignedLength & (pageSize - 1)) {
        alignedLength += pageSize - (alignedLength & (pageSize - 1));
    }

    mi = __get_mmap_from_addr(addr);
    if (mi == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (mi->OriginalIOD) {

    }
}

int
msync(
        _In_ void*  addr,
        _In_ size_t length,
        _In_ int    flags)
{
    struct __mmap_item* mi;

    mi = __get_mmap_from_addr(addr);
    if (mi == NULL) {
        errno = ENOENT;
        return -1;
    }

    if (mi->OriginalIOD == -1) {
        // We don't flush to SHM regions, but we can invalidate mappings
        if (!(flags & MS_INVALIDATE)) {
            return 0;
        }
        return OsErrToErrNo(
                FlushHardwareCache(
                        CACHE_MEMORY,
                        addr,
                        length
                )
        );
    }

    return OsErrToErrNo(
            FlushFileMapping(
                    addr,
                    length,
                    (unsigned int)flags
            )
    );
}