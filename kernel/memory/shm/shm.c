/**
 * Copyright, Philip Meulengracht
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

#define __need_minmax
#include <arch/utils.h>
#include <debug.h>
#include <ddk/barrier.h>
#include <ds/streambuffer.h>
#include <handles/global.h>
#include <handles/handle.h>
#include <handles/paths.h>
#include <heap.h>
#include <machine.h>
#include <threading.h>
#include <shm/shm.h>
#include <shm/view.h>
#include <string.h>

#include "private.h"

static void __BuildSG(struct SHMBuffer* shmBuffer, int* sgCountOut, SHMSG_t* sgOut);

static oserr_t
__UnmapPageMapping(
    _In_ struct SHMBuffer* buffer)
{
    oserr_t status;

    if (buffer->PageMapping == 0) {
        return OS_EOK;
    }

    status = MemorySpaceUnmap(
        GetCurrentMemorySpace(),
        buffer->PageMapping,
        GetMemorySpacePageSize()
    );
    if (status == OS_EOK) {
        buffer->PageMapping = 0;
    }
    return status;
}

static void
__SHMBufferDelete(
    _In_ void* resource)
{
    struct SHMBuffer* shmBuffer = resource;
    oserr_t           oserr;

    if (shmBuffer == NULL) {
        return;
    }

    oserr = __UnmapPageMapping(shmBuffer);
    if (oserr != OS_EOK) {
        ERROR("__SHMBufferDelete: page unmap id %u failed: %u", shmBuffer->ID, oserr);
        return;
    }

    if (shmBuffer->KernelMapping != 0) {
        oserr = MemorySpaceUnmap(
            GetCurrentMemorySpace(),
            shmBuffer->KernelMapping,
            shmBuffer->Length
        );
        if (oserr != OS_EOK) {
            ERROR("__SHMBufferDelete: kernel unmap id %u failed: %u", shmBuffer->ID, oserr);
            // The handle destructor cannot report failure. Keep the buffer and
            // its backing allocated while the kernel mapping still exists.
            return;
        }
        shmBuffer->KernelMapping = 0;
    }

    // Reserved backing returns to the memory component recorded at allocation.
    if (shmBuffer->Allocation.Owner != NULL) {
        (void)SystemMemoryReleaseAllocation(&shmBuffer->Allocation);
    } else {
        FreePhysicalMemory(shmBuffer->PageCount, &shmBuffer->Pages[0]);
    }

    MutexDestruct(&shmBuffer->Mutex);
    kfree(shmBuffer);
}

static struct SHMBuffer*
__SHMBufferNew(
    _In_ size_t       offset,
    _In_ size_t       size,
    _In_ unsigned int flags)
{
    struct SHMBuffer* buffer;
    size_t            pageSize  = GetMemorySpacePageSize();
    size_t            pageCount = DIVUP(offset + size, pageSize);
    size_t            structSize = sizeof(struct SHMBuffer) + (pageCount * sizeof(uintptr_t));

    buffer = kmalloc(structSize);
    if (buffer == NULL) {
        return NULL;
    }
    memset(buffer, 0, structSize);

    MutexConstruct(&buffer->Mutex, MUTEX_FLAG_PLAIN);
    buffer->ID = CreateHandle(
            HandleTypeSHM,
            NULL,
            __SHMBufferDelete,
            buffer
    );
    if (buffer->ID == UUID_INVALID) {
        MutexDestruct(&buffer->Mutex);
        kfree(buffer);
        return NULL;
    }

    buffer->Owner = GetCurrentMemorySpace();
    buffer->PageCount = (int)pageCount;
    buffer->Offset = offset;
    buffer->Length = size;
    buffer->Flags = flags;

    return buffer;
}

static unsigned int
__MapFlagsForIPC(
    _In_  SHM_t* shm)
{
    unsigned int flags = MAPPING_USERSPACE | MAPPING_PERSISTENT | MAPPING_COMMIT;
    if (shm->Flags & MAPPING_CLEAN) {
        flags |= MAPPING_CLEAN;
    }
    return flags;
}

static oserr_t
__CreateIPCBuffer(
    _In_  struct SHMBuffer* buffer,
    _In_  SHM_t*            shm,
    _Out_ void**            userMappingOut)
{
    MemorySpace_t* memorySpace    = GetCurrentMemorySpace();
    unsigned int   userMapFlags   = __MapFlagsForIPC(shm);
    unsigned int   kernelMapFlags = MAPPING_COMMIT | MAPPING_PERSISTENT;
    oserr_t        oserr;
    vaddr_t        userMapping;
    vaddr_t        kernelMapping;

    TRACE("__CreateIPCBuffer(id=%u, size=%zu)", buffer->ID, shm->Size);
    
    // IPC buffers need userspace and kernel mappings. The kernel mapping lets
    // another process send messages without mapping the buffer into its own
    // address space.
    oserr = MemorySpaceMap(
        memorySpace,
        &(struct MemorySpaceMapOptions) {
            .SHMTag = buffer->ID,
            .Pages = &buffer->Pages[0],
            .Length = shm->Size,
            .Mask = __MASK,
            .Flags = userMapFlags,
            .PlacementFlags = MAPPING_VIRTUAL_PROCESS
        },
        &userMapping
    );
    if (oserr != OS_EOK) {
        ERROR("__CreateIPCBuffer: MemorySpaceMap user mapping id %u, size %zu failed: %u",
              buffer->ID, shm->Size, oserr);
        return oserr;
    }
    
    *userMappingOut = (void*)userMapping;

    oserr = MemorySpaceMap(
        memorySpace,
        &(struct MemorySpaceMapOptions) {
            .SHMTag = buffer->ID,
            .Pages = &buffer->Pages[0],
            .Length = shm->Size,
            .Mask = __MASK,
            .Flags = kernelMapFlags,
            .PlacementFlags = MAPPING_PHYSICAL_FIXED | MAPPING_VIRTUAL_GLOBAL
        },
        &kernelMapping
    );
    if (oserr != OS_EOK) {
        ERROR("__CreateIPCBuffer: MemorySpaceMap kernel mapping id %u, size %zu failed: %u",
              buffer->ID, shm->Size, oserr);
        return oserr;
    }

    // SHMCreate publishes the endpoint name after this function returns.
    // Initialize the stream first so another service can send immediately.
    streambuffer_construct(
        (streambuffer_t*)kernelMapping,
        shm->Size - sizeof(streambuffer_t),
        STREAMBUFFER_GLOBAL | STREAMBUFFER_MULTIPLE_WRITERS
    );

    buffer->KernelMapping = kernelMapping;
    TRACE("__CreateIPCBuffer: id %u stream initialized with capacity %zu",
          buffer->ID, shm->Size - sizeof(streambuffer_t));
    return OS_EOK;
}

static unsigned int
__MapFlagsForTrap(
    _In_  SHM_t* shm)
{
    unsigned int flags = MAPPING_USERSPACE | MAPPING_PERSISTENT | MAPPING_TRAPPAGE;
    if (shm->Flags & MAPPING_CLEAN) {
        flags |= MAPPING_CLEAN;
    }
    return flags;
}

static oserr_t
__CreateTrapBuffer(
    _In_  struct SHMBuffer* buffer,
    _In_  SHM_t*            shm,
    _Out_ void**            userMappingOut)
{
    unsigned int mapFlags = __MapFlagsForTrap(shm);
    oserr_t      oserr;
    vaddr_t      mapping;

    // Reserve the range without physical pages. Access to an unmapped page
    // triggers a signal.
    oserr = MemorySpaceMap(
        GetCurrentMemorySpace(),
        &(struct MemorySpaceMapOptions) {
            .SHMTag = buffer->ID,
            .Length = shm->Size,
            .Flags = mapFlags,
            .PlacementFlags = MAPPING_VIRTUAL_PROCESS
        },
        &mapping
    );
    if (oserr != OS_EOK) {
        return oserr;
    }
    *userMappingOut = (void*)mapping;
    return OS_EOK;
}

static unsigned int
__MapFlagsForRegular(
        _In_  SHM_t* shm)
{
    unsigned int flags = MAPPING_PERSISTENT | MAPPING_USERSPACE;
    if (shm->Flags & SHM_CLEAN) {
        flags |= MAPPING_CLEAN;
    }
    if (shm->Flags & SHM_COMMIT) {
        flags |= MAPPING_COMMIT;
    }
    if (!(shm->Access & SHM_ACCESS_WRITE)) {
        flags |= MAPPING_READONLY;
    }
    if (shm->Access & SHM_ACCESS_EXECUTE) {
        flags |= MAPPING_EXECUTABLE;
    }
    return flags;
}

static oserr_t
__CreateRegularMappedBuffer(
    _In_  struct SHMBuffer* buffer,
    _In_  SHM_t*            shm,
    _Out_ void**            userMappingOut)
{
    unsigned int mapFlags = __MapFlagsForRegular(shm);
    oserr_t      oserr;
    vaddr_t      mapping;

    oserr = MemorySpaceMap(
        GetCurrentMemorySpace(),
        &(struct MemorySpaceMapOptions) {
            .SHMTag = buffer->ID,
            .Pages = &buffer->Pages[0],
            .Length = shm->Size,
            .Mask = __MASK,
            .Flags = mapFlags,
            .PlacementFlags = MAPPING_VIRTUAL_PROCESS
        },
        &mapping
    );
    if (oserr != OS_EOK) {
        return oserr;
    }
    *userMappingOut = (void*)mapping;
    return OS_EOK;
}

static oserr_t
__CreateRegularUnmappedBuffer(
    _In_  struct SHMBuffer* buffer,
    _In_  SHM_t*            shm,
    _Out_ void**            userMappingOut)
{
    unsigned int mapFlags = __MapFlagsForRegular(shm);
    oserr_t      oserr;
    vaddr_t      mapping;

    // Reserve the virtual range without committing physical pages.
    oserr = MemorySpaceMap(
        GetCurrentMemorySpace(),
        &(struct MemorySpaceMapOptions) {
            .SHMTag = buffer->ID,
            .Length = shm->Size,
            .Mask = __MASK,
            .Flags = mapFlags,
            .PlacementFlags = MAPPING_VIRTUAL_PROCESS
        },
        &mapping
    );
    if (oserr != OS_EOK) {
        return oserr;
    }
    *userMappingOut = (void*)mapping;
    return OS_EOK;
}

static oserr_t
__CreateRegularBuffer(
        _In_  struct SHMBuffer* buffer,
        _In_  SHM_t*            shm,
        _Out_ void**            userMappingOut)
{
    if (shm->Flags & SHM_COMMIT) {
        return __CreateRegularMappedBuffer(buffer, shm, userMappingOut);
    }
    return __CreateRegularUnmappedBuffer(buffer, shm, userMappingOut);
}

static void
__ConstructSHMHandle(
        _In_ SHMHandle_t* handle,
        _In_ uuid_t       shmID,
        _In_ size_t       capacity,
        _In_ size_t       length,
        _In_ size_t       offset,
        _In_ void*        mapping)
{
    // Set up the initial SHM handle.
    handle->ID = shmID;
    handle->Capacity = capacity;
    handle->Length = length;
    handle->Offset = offset;
    handle->Buffer = mapping;
}

oserr_t
SHMCreate(
    _In_ SHM_t*       shm,
    _In_ SHMHandle_t* handle)
{
    struct SHMBuffer* buffer;
    oserr_t           oserr;
    oserr_t           cleanupStatus;
    void*             mapping = NULL;

    TRACE("SHMCreate(shm=%p, handle=%p)", shm, handle);
    if (shm == NULL || handle == NULL || shm->Size == 0) {
        WARNING("SHMCreate: missing request, output or buffer size");
        return OS_EINVALPARAMS;
    }

    // Only regular, trap and IPC storage can be created through this API.
    // The retired device kind must not fall through to ordinary allocation.
    if (SHM_KIND(shm->Flags) > SHM_IPC) {
        return OS_ENOTSUPPORTED;
    }

    // An IPC buffer needs room for the stream header and at least one data
    // byte. Reject smaller buffers before creating a handle or mapping storage.
    if (SHM_KIND(shm->Flags) == SHM_IPC && shm->Size <= sizeof(streambuffer_t)) {
        WARNING("SHMCreate: IPC size %zu must exceed stream header size %zu", shm->Size, sizeof(streambuffer_t));
        return OS_EINVALPARAMS;
    }

    buffer = __SHMBufferNew(0, shm->Size, shm->Flags);
    if (buffer == NULL) {
        ERROR("SHMCreate: __SHMBufferNew size %zu, flags 0x%x failed", shm->Size, shm->Flags);
        return OS_EOOM;
    }

    // Select the buffer type from the flags.
    if (SHM_KIND(shm->Flags) == SHM_IPC) {
        oserr = __CreateIPCBuffer(buffer, shm, &mapping);
    } else if (SHM_KIND(shm->Flags) == SHM_TRAP) {
        oserr = __CreateTrapBuffer(buffer, shm, &mapping);
    } else {
        oserr = __CreateRegularBuffer(buffer, shm, &mapping);
    }

    if (oserr != OS_EOK) {
        ERROR("SHMCreate: buffer setup id %u, kind 0x%x failed: %u", buffer->ID, SHM_KIND(shm->Flags), oserr);
        goto error;
    }

    // Register the buffer under the requested key.
    if (shm->Key != NULL) {
        oserr = RegisterHandlePath(buffer->ID, shm->Key);
        if (oserr != OS_EOK) {
            ERROR("SHMCreate: RegisterHandlePath id %u failed: %u", buffer->ID, oserr);
            goto error;
        }
    }

    __ConstructSHMHandle(
        handle,
        buffer->ID,
        shm->Size,
        shm->Size,
        0,
        mapping
    );
    TRACE("SHMCreate: published id %u, size %zu", buffer->ID, shm->Size);
    return OS_EOK;

error:
    if (mapping != NULL) {
        cleanupStatus = MemorySpaceUnmap(GetCurrentMemorySpace(), (vaddr_t)mapping, shm->Size);
        if (cleanupStatus != OS_EOK) {
            ERROR("SHMCreate: user unmap id %u failed during rollback: %u", buffer->ID, cleanupStatus);
            // Keep the creator's reference while its pages remain mapped. Group
            // teardown removes the address spaces before releasing their grants.
            return oserr;
        }
    }

    // The registered handle schedules kernel-mapping and backing cleanup once
    // its final reference ends, including any concurrent IPC send references.
    DestroyHandle(buffer->ID);
    return oserr;
}

oserr_t
SHMCreateDeviceBuffer(
    _In_  size_t       size,
    _In_  size_t       alignment,
    _In_  size_t       boundary,
    _Out_ SHMHandle_t* handle)
{
    struct SystemMemoryAllocation allocation = { 0 };
    struct SHMBuffer*             buffer;
    size_t                        pageSize = GetMemorySpacePageSize();
    unsigned int                  flags = MAPPING_USERSPACE | MAPPING_PERSISTENT | MAPPING_COMMIT | MAPPING_CLEAN;
    vaddr_t                       mapping;
    oserr_t                       oserr;

    if (handle == NULL || size == 0 || size % pageSize != 0) {
        return OS_EINVALPARAMS;
    }

    oserr = SystemMemoryAllocateDevice(
            &GetMachine()->PhysicalMemory,
            size,
            alignment,
            boundary,
            &allocation
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    buffer = __SHMBufferNew(0, size, SHM_COMMIT | SHM_CLEAN);
    if (buffer == NULL) {
        (void)SystemMemoryReleaseAllocation(&allocation);
        return OS_EOOM;
    }

    // Store the allocation in the buffer before creating any mappings.
    // The buffer destructor releases the allocation.
    buffer->Allocation = allocation;
    memset(&allocation, 0, sizeof(allocation));
    for (int i = 0; i < buffer->PageCount; i++) {
        buffer->Pages[i] = buffer->Allocation.PhysicalBase + ((paddr_t)i * pageSize);
    }

    // MAPPING_CLEAN zeroes the pages through this view, so a device never
    // sees data an earlier user left behind.
    if (SHMBufferUncached(buffer)) {
        flags |= MAPPING_NOCACHE;
    }
    oserr = MemorySpaceMap(
            GetCurrentMemorySpace(),
            &(struct MemorySpaceMapOptions) {
                .SHMTag = buffer->ID,
                .Pages = &buffer->Pages[0],
                .Length = size,
                .Mask = __MASK,
                .Flags = flags,
                .PlacementFlags = MAPPING_PHYSICAL_FIXED | MAPPING_VIRTUAL_PROCESS
            },
            &mapping
    );
    if (oserr != OS_EOK) {
        (void)DestroyHandle(buffer->ID);
        return oserr;
    }

    __ConstructSHMHandle(handle, buffer->ID, size, size, 0, (void*)mapping);
    return OS_EOK;
}

static size_t
__ClampLength(
    _In_ struct SHMBuffer* shmBuffer,
    _In_ size_t            offset,
    _In_ size_t            length)
{
    return MIN(length, (shmBuffer->Length - offset));
}

oserr_t
SHMAttach(
    _In_  uuid_t       shmID,
    _Out_ SHMHandle_t* handle)
{
    SHMReference_t source;
    oserr_t        status;

    if (handle == NULL) {
        return OS_EINVALPARAMS;
    }

    status = SHMResolve(shmID, &source);
    if (status != OS_EOK) {
        return status;
    }

    if (source.Buffer->Flags & SHM_PRIVATE) {
        status = AreMemorySpacesRelated(source.Buffer->Owner, GetCurrentMemorySpace());
        if (status != OS_EOK) {
            ReleaseGlobalHandle(source.GlobalHandleID);
            return OS_EPERMISSIONS;
        }
    }

    status = AcquireHandle(shmID, source.Type, NULL);
    if (status == OS_EOK) {
        __ConstructSHMHandle(handle, shmID, source.Length, 0, 0, NULL);
    }

    ReleaseGlobalHandle(source.GlobalHandleID);
    return status;
}

oserr_t
SHMDetach(
    _InOut_ SHMHandle_t* handle)
{
    oserr_t status;

    if (handle == NULL) {
        return OS_EINVALPARAMS;
    }

    if (handle->ID != UUID_INVALID) {
        status = SHMViewCleanup(handle->ID);
        if (status != OS_EOK) {
            return status;
        }
    }

    if (handle->Buffer != NULL) {
        status = SHMUnmap(handle, (vaddr_t)handle->Buffer, handle->Length);
        if (status != OS_EOK) {
            return status;
        }
    }

    // Clear the handle only after its reference has been released.
    if (handle->ID != UUID_INVALID) {
        status = DestroyHandle(handle->ID);
        if (status != OS_EOK && status != OS_EINCOMPLETE) {
            return status;
        }
        handle->ID = UUID_INVALID;
    }
    return OS_EOK;
}

static oserr_t
__AllocateSHMPage(
    _In_  struct SHMBuffer* buffer,
    _Out_ paddr_t*          pageOut)
{
    paddr_t page;
    vaddr_t mapping;
    oserr_t status;

    status = AllocatePhysicalMemory(__MASK, 1, &page);
    if (status != OS_EOK) {
        return status;
    }

    if (buffer->Flags & SHM_CLEAN) {
        // Zero through a writable kernel mapping before any view can use the page.
        status = MemorySpaceMap(
            GetCurrentMemorySpace(),
            &(struct MemorySpaceMapOptions) {
                .Pages = &page,
                .Length = GetMemorySpacePageSize(),
                .Mask = __MASK,
                .Flags = MAPPING_COMMIT | MAPPING_PERSISTENT | MAPPING_CLEAN,
                .PlacementFlags = MAPPING_PHYSICAL_FIXED | MAPPING_VIRTUAL_GLOBAL
            },
            &mapping
        );
        if (status != OS_EOK) {
            FreePhysicalMemory(1, &page);
            return status;
        }
        buffer->PageMapping = mapping;
    }

    // Views may read the page array without the buffer mutex. Make the zeroes
    // visible before publishing the physical address.
    smp_wmb();
    // If unmap fails, the initialized page and its mapping must stay together.
    *pageOut = page;
    return __UnmapPageMapping(buffer);
}

oserr_t
SHMEnsurePhysicalPages(
    _In_ struct SHMBuffer* shmBuffer,
    _In_ int               pageStart,
    _In_ int               pageCount)
{
    oserr_t oserr;

    MutexLock(&shmBuffer->Mutex);
    oserr = __UnmapPageMapping(shmBuffer);
    if (oserr != OS_EOK) {
        goto exit;
    }

    for (int i = 0; i < pageCount; i++) {
        if (shmBuffer->Pages[pageStart + i]) {
            continue;
        }

        oserr = __AllocateSHMPage(
            shmBuffer,
            &shmBuffer->Pages[pageStart + i]
        );
        if (oserr != OS_EOK) {
            goto exit;
        }

        TRACE("SHMEnsurePhysicalPages: allocated page %i=0x%llx",
              i, (unsigned long long)shmBuffer->Pages[pageStart + i]);
    }
exit:
    MutexUnlock(&shmBuffer->Mutex);
    return oserr;
}

static unsigned int
__RecalculateFlags(
    _In_ const struct SHMBuffer* shmBuffer,
    _In_ unsigned int            accessFlags)
{
    // All mappings must use MAPPING_USERSPACE and MAPPING_PERSISTENT so the
    // virtual memory system leaves the physical pages allocated until the SHM
    // buffer is freed.
    unsigned int flags = MAPPING_USERSPACE | MAPPING_PERSISTENT;

    // Views of the same pages must use the same cache settings to avoid
    // inconsistent data.
    if (SHMBufferUncached(shmBuffer)) {
        flags |= MAPPING_NOCACHE;
    }

    // The initial stack and trap buffer mappings have MAPPING_TRAPPAGE and
    // MAPPING_GUARDPAGE where required. Leave these attributes out when mapping
    // the buffer for another process.
    // TODO: Check whether omitting these attributes causes issues.

    // Handle access modifiers last.
    if (!(accessFlags & SHM_ACCESS_WRITE)) {
        flags |= MAPPING_READONLY;
    }
    if (accessFlags & SHM_ACCESS_EXECUTE) {
        flags |= MAPPING_EXECUTABLE;
    }
    return flags;
}

static oserr_t
__UpdateMapping(
    _In_ struct SHMBuffer* shmBuffer,
    _In_ SHMHandle_t*      handle,
    _In_ size_t            length,
    _In_ unsigned int      flags)
{
    size_t  pageSize  = GetMemorySpacePageSize();
    size_t  pageCount = DIVUP(length, pageSize);
    size_t  pageIndex = (shmBuffer->Offset + handle->Offset) / pageSize;
    oserr_t oserr;

    // Allocate missing pages for MAPPING_PHYSICAL_FIXED only when
    // SHM_ACCESS_COMMIT is set.
    if (flags & SHM_ACCESS_COMMIT) {
        oserr = SHMEnsurePhysicalPages(shmBuffer, (int)pageIndex, (int)pageCount);
        if (oserr != OS_EOK) {
            return oserr;
        }
    }

    return MemorySpaceMap(
        GetCurrentMemorySpace(),
        &(struct MemorySpaceMapOptions) {
            .SHMTag = shmBuffer->ID,
            .VirtualStart = (vaddr_t)handle->Buffer,
            .Pages = &shmBuffer->Pages[0],
            .Length = length,
            .Mask = __MASK,
            .Flags = __RecalculateFlags(shmBuffer, flags),
            .PlacementFlags = MAPPING_PHYSICAL_FIXED | MAPPING_VIRTUAL_FIXED
        },
        (vaddr_t*)&handle->Buffer
    );
}

static oserr_t
__CreateMapping(
    _In_  struct SHMBuffer* shmBuffer,
    _In_  size_t            offset,
    _In_  size_t            length,
    _In_  unsigned int      flags,
    _Out_ void**            mappingOut)
{
    size_t  pageSize     = GetMemorySpacePageSize();
    size_t  actualOffset = shmBuffer->Offset + offset;
    size_t  pageIndex    = actualOffset / pageSize;
    size_t  pageCount    = DIVUP(length, pageSize);
    oserr_t oserr;
    
    TRACE("__CreateMapping(length=0x%zx, index=0x%zx, count=0x%zx)", length, pageIndex, pageCount);

    // pageIndex must refer to a page in the buffer.
    if (pageIndex >= shmBuffer->PageCount) {
        return OS_EINVALPARAMS;
    }

    // Allocate missing pages for MAPPING_PHYSICAL_FIXED only when
    // SHM_ACCESS_COMMIT is set.
    if (flags & SHM_ACCESS_COMMIT) {
        oserr = SHMEnsurePhysicalPages(shmBuffer, (int)pageIndex, (int)pageCount);
        if (oserr != OS_EOK) {
            return oserr;
        }
    }

    return MemorySpaceMap(
        GetCurrentMemorySpace(),
        &(struct MemorySpaceMapOptions) {
            .SHMTag = shmBuffer->ID,
            .Pages = &shmBuffer->Pages[pageIndex],
            // Include the offset within the first page in the mapping length.
            // A 32-byte range at 0xFFF0 crosses a page boundary, so the
            // mapping must cover both pages.
            .Length = (length + (actualOffset % pageSize)),
            .Mask = __MASK,
            .Flags = __RecalculateFlags(shmBuffer, flags),
            .PlacementFlags = MAPPING_PHYSICAL_FIXED | MAPPING_VIRTUAL_PROCESS
        },
        (vaddr_t*)mappingOut
    );
}


static oserr_t
__UnmapBuffer(
    _InOut_ SHMHandle_t* handle,
    _In_    vaddr_t      address,
    _In_    size_t       length)
{
    oserr_t oserr;

    oserr = MemorySpaceUnmap(
            GetCurrentMemorySpace(),
            address,
            length
    );
    if (oserr != OS_EOK) {
        // OS_EINCOMPLETE means part of the original mapping was unmapped.
        // Return it to the caller without clearing the handle.
        return oserr;
    }

    handle->Buffer = NULL;
    handle->Offset = 0;
    handle->Length = 0;
    return OS_EOK;
}

oserr_t
SHMMap(
    _In_ SHMHandle_t* handle,
    _In_ size_t       offset,
    _In_ size_t       length,
    _In_ unsigned int flags)
{
    SHMReference_t    source;
    struct SHMBuffer* shmBuffer;
    oserr_t           oserr;
    void*             mapping;
    size_t            clampedLength;
    TRACE("SHMMap(offset=0x%zx, length=0x%zx, flags=0x%x)", offset, length, flags);

    if (handle == NULL || length == 0) {
        return OS_EINVALPARAMS;
    }

    oserr = SHMResolve(handle->ID, &source);
    if (oserr != OS_EOK) {
        return oserr;
    }

    if (source.View != NULL) {
        oserr = SHMViewMap(source.GlobalHandleID, source.View, handle, offset, length, flags);
        goto exit;
    }

    shmBuffer = source.Buffer;

    // SHM_PRIVATE requires the current memory space to be related to
    // shmBuffer->Owner.
    if (shmBuffer->Flags & SHM_PRIVATE) {
        oserr = AreMemorySpacesRelated(shmBuffer->Owner, GetCurrentMemorySpace());
        if (oserr != OS_EOK) {
            oserr = OS_EPERMISSIONS;
            goto exit;
        }
    }

    // Calculate the actual length of the mapping.
    clampedLength = __ClampLength(shmBuffer, offset, length);
    if (handle->Buffer != NULL) {
        // When the offset matches, update the existing mapping's length or
        // access flags.
        if (handle->Offset == offset) {
            oserr = __UpdateMapping(shmBuffer, handle, clampedLength, flags);
            goto exit;
        }

        // Keep the previous mapping until the new mapping has been created.
    }

    oserr = __CreateMapping(shmBuffer, offset, clampedLength, flags, &mapping);
    if (oserr != OS_EOK) {
        goto exit;
    }
    TRACE("SHMMap: mapping: 0x%p\n", mapping);

    if (handle->Buffer != NULL) {
        oserr = __UnmapBuffer(handle, (vaddr_t)handle->Buffer, handle->Length);
        if (oserr != OS_EOK) {
            (void)MemorySpaceUnmap(
                GetCurrentMemorySpace(),
                (vaddr_t)mapping,
                clampedLength
            );
            goto exit;
        }
    }

    handle->Buffer = mapping;
    handle->Offset = offset;
    handle->Length = clampedLength;
    oserr = OS_EOK;

exit:
    ReleaseGlobalHandle(source.GlobalHandleID);
    return oserr;
}

oserr_t
SHMUnmap(
    _In_ SHMHandle_t* handle,
    _In_ vaddr_t      address,
    _In_ size_t       length)
{
    oserr_t        oserr;
    SHMReference_t source;
    size_t         offset = address % GetMemorySpacePageSize();

    if (handle == NULL || handle->Buffer == NULL) {
        return OS_EINVALPARAMS;
    }

    oserr = SHMResolve(handle->ID, &source);
    if (oserr != OS_EOK) {
        return oserr;
    }

    if (source.View != NULL) {
        ReleaseGlobalHandle(source.GlobalHandleID);
        if (length == 0 || length > SIZE_MAX - offset) {
            return OS_EINVALPARAMS;
        }

        oserr = MemorySpaceUnmap(
            GetCurrentMemorySpace(),
            address - offset,
            length + offset
        );
        if (oserr == OS_EOK) {
            handle->Buffer = NULL;
            handle->Offset = 0;
            handle->Length = 0;
        }
        return oserr;
    }
    ReleaseGlobalHandle(source.GlobalHandleID);

    return __UnmapBuffer(handle, address, length);
}


oserr_t
SHMCommit(
    _In_ uuid_t handle,
    _In_ void*  memoryBase,
    _In_ void*  memory,
    _In_ size_t length)
{
    SHMReference_t    source;
    struct SHMBuffer* shmBuffer;
    oserr_t           oserr;
    size_t            pageSize = GetMemorySpacePageSize();
    uintptr_t         baseAddress = (uintptr_t)memoryBase;
    uintptr_t         address = (uintptr_t)memory;
    size_t            clampedLength;
    int               i;
    
    TRACE("SHMCommit(memoryBase=%p, memory=%p, length=0x%zx)",
          memoryBase, memory, length);

    oserr = SHMResolve(handle, &source);
    if (oserr != OS_EOK) {
        return oserr;
    }

    if (source.View != NULL) {
        oserr = SHMViewCommit(source.View, address, length);
        goto exit;
    }
    shmBuffer = source.Buffer;

    // Align the address to a page boundary.
    address &= ~(pageSize - 1);

    // Clamp the length.
    i = (int)((address - baseAddress) / pageSize);
    clampedLength = __ClampLength(shmBuffer, i * pageSize, length);

    // Allocate any missing physical pages in the requested range.
    oserr = SHMEnsurePhysicalPages(shmBuffer, i, (int)(clampedLength / pageSize));
    if (oserr != OS_EOK) {
        ERROR("SHMCommit failed to allocate pages: %u", oserr);
        goto exit;
    }

    // IPC kernel mappings are committed at creation and are never imported.
    // This call only commits the mapping in the current memory space.
    oserr = MemorySpaceCommit(
        GetCurrentMemorySpace(),
        address,
        &shmBuffer->Pages[i],
        clampedLength,
        __MASK,
        MAPPING_PHYSICAL_FIXED
    );
    if (oserr != OS_EOK) {
        ERROR("SHMCommit failed to commit mapping: %u", oserr);
    }

exit:
    ReleaseGlobalHandle(source.GlobalHandleID);
    return oserr;
}

#define SG_IS_SAME_REGION(memory_region, idx, idx2, pageSize) \
    (((memory_region)->Pages[idx] + (pageSize) == (memory_region)->Pages[idx2]) || \
     ((memory_region)->Pages[idx] == 0 && (memory_region)->Pages[idx2] == 0))

static void
__BuildSG(
    _In_ struct SHMBuffer* shmBuffer,
    _Out_ int*             sgCountOut,
    _Out_ SHMSG_t*         sgOut)
{
    size_t pageSize = GetMemorySpacePageSize();

    // With sgOut == NULL, count the scatter-gather entries without filling a list.
    if (!sgOut) {
        int sgCount = 0;
        for (int i = 0; i < shmBuffer->PageCount; i++) {
            if (i == 0 || !SG_IS_SAME_REGION(shmBuffer, i - 1, i, pageSize)) {
                sgCount++;
            }
        }
        *sgCountOut = sgCount;
    }

    // The caller must supply the list capacity in *sgCountOut.
    if (sgOut) {
        int sgCount = *sgCountOut;
        for (int i = 0, j = 0; (i < sgCount) && (j < shmBuffer->PageCount); i++) {
            SHMSG_t* sg = &sgOut[i];

            sg->Address = shmBuffer->Pages[j++];
            sg->Length  = pageSize;

            while ((j < shmBuffer->PageCount) && SG_IS_SAME_REGION(shmBuffer, j - 1, j, pageSize)) {
                sg->Length += pageSize;
                j++;
            }
        }

        // Adjust the first scatter-gather entry for shmBuffer->Offset.
        sgOut[0].Address += shmBuffer->Offset;
        sgOut[0].Length  -= shmBuffer->Offset;
    }
}

static void
__BuildViewSG(
    _In_    const SHMReference_t* source,
    _InOut_ int*                  count,
    _Out_   SHMSG_t*              entries)
{
    size_t  pageSize = GetMemorySpacePageSize();
    size_t  position = source->Buffer->Offset + source->Offset;
    size_t  remaining = source->Length;
    size_t  pageOffset;
    size_t  bytes;
    paddr_t address;
    SHMSG_t segment = { 0 };
    int     capacity = entries != NULL ? *count : 0;
    int     used = 0;
    bool    contiguous;

    while (remaining != 0) {
        pageOffset = position % pageSize;
        bytes = MIN(remaining, pageSize - pageOffset);
        address = source->Buffer->Pages[position / pageSize];
        
        if (address != 0) {
            address += pageOffset;
        }
        
        contiguous = segment.Length != 0 &&
            ((segment.Address == 0 && address == 0) ||
             (segment.Address != 0 && address == segment.Address + segment.Length));
        if (!contiguous) {
            if (segment.Length != 0) {
                if (entries != NULL && used < capacity) {
                    entries[used] = segment;
                }
                used++;
            }

            segment.Address = address;
            segment.Length = 0;
        }

        segment.Length += bytes;
        position += bytes;
        remaining -= bytes;
    }

    if (entries != NULL && used < capacity) {
        entries[used] = segment;
    }

    used++;

    // Update the count with the number of 
    // scatter-gather entries used.
    *count = entries == NULL ? used : MIN(used, capacity);
}

oserr_t
SHMBuildSG(
    _In_    uuid_t   handle,
    _InOut_ int*     sgCountOut,
    _Out_   SHMSG_t* sgOut)
{
    SHMReference_t source;
    oserr_t        status;

    if (sgCountOut == NULL || (sgOut != NULL && *sgCountOut <= 0)) {
        return OS_EINVALPARAMS;
    }

    status = SHMResolve(handle, &source);
    if (status != OS_EOK) {
        return status;
    }

    MutexLock(&source.Buffer->Mutex);
    if (source.View != NULL) {
        __BuildViewSG(&source, sgCountOut, sgOut);
    } else {
        __BuildSG(source.Buffer, sgCountOut, sgOut);
    }
    MutexUnlock(&source.Buffer->Mutex);

    ReleaseGlobalHandle(source.GlobalHandleID);
    return OS_EOK;
}

oserr_t
SHMKernelMapping(
    _In_  uuid_t handle,
    _Out_ void** bufferOut)
{
    struct SHMBuffer* shmBuffer;

    if (!bufferOut) {
        return OS_EINVALPARAMS;
    }

    if (LookupHandle(handle, HandleTypeSHMView) != NULL) {
        return OS_ENOTSUPPORTED;
    }
    
    shmBuffer = LookupHandle(handle, HandleTypeSHM);
    if (!shmBuffer) {
        return OS_ENOENT;
    }

    *bufferOut = (void*)shmBuffer->KernelMapping;
    return OS_EOK;
}

static oserr_t
__GetIPCBufferMapping(
    _In_  struct SHMBuffer* shmBuffer,
    _Out_ void**            bufferOut)
{
    if (SHM_KIND(shmBuffer->Flags) != SHM_IPC || shmBuffer->KernelMapping == 0) {
        // Both callers supply a global reference. Rejecting the buffer ends it.
        ReleaseGlobalHandle(shmBuffer->ID);
        return OS_EINVALPARAMS;
    }

    *bufferOut = (void*)shmBuffer->KernelMapping;
    return OS_EOK;
}

oserr_t
SHMAcquireIPCBufferReference(
    _In_  uuid_t  handleID,
    _Out_ void** bufferOut)
{
    struct SHMBuffer* shmBuffer;
    oserr_t           oserr;

    if (bufferOut == NULL) {
        return OS_EINVALPARAMS;
    }

    oserr = AcquireHandleReference(handleID, HandleTypeSHM, (void**)&shmBuffer);
    if (oserr != OS_EOK) {
        return oserr;
    }
    return __GetIPCBufferMapping(shmBuffer, bufferOut);
}

oserr_t
SHMAcquireGlobalIPCBuffer(
    _In_  uuid_t  globalHandleID,
    _Out_ void** bufferOut)
{
    struct SHMBuffer* shmBuffer;
    oserr_t           oserr;

    if (bufferOut == NULL) {
        return OS_EINVALPARAMS;
    }

    oserr = AcquireGlobalHandle(globalHandleID, HandleTypeSHM, (void**)&shmBuffer);
    if (oserr != OS_EOK) {
        return oserr;
    }
    return __GetIPCBufferMapping(shmBuffer, bufferOut);
}
