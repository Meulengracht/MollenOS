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

//#define __TRACE

#define __need_minmax
#include <arch/utils.h>
#include <assert.h>
#include <ddk/io.h>
#include <debug.h>
#include <handle.h>
#include <heap.h>
#include <memoryspace.h>
#include <threading.h>
#include <shm.h>

typedef struct MemoryRegion {
    Mutex_t      SyncObject;
    uintptr_t    KernelMapping;
    size_t       Length;
    size_t       Capacity;
    size_t       PageMask;
    unsigned int Flags;
    int          PageCount;
    uintptr_t    Pages[];
} MemoryRegion_t;

static oserr_t __FillInMemoryRegion(
        _In_ MemoryRegion_t* memoryRegion,
        _In_ uintptr_t       userStart,
        _In_ int             pageCount)
{

    uintptr_t  kernelAddress = memoryRegion->KernelMapping;
    uintptr_t  userAddress   = userStart;
    oserr_t osStatus      = OS_EOK;

    for (int i = 0; i < pageCount; i++, userAddress += GetMemorySpacePageSize(), kernelAddress += GetMemorySpacePageSize()) {
        if (!memoryRegion->Pages[i]) {
            // handle kernel mapping first
            osStatus = MemorySpaceCommit(
                    GetCurrentMemorySpace(),
                    kernelAddress,
                    &memoryRegion->Pages[i],
                    GetMemorySpacePageSize(),
                    memoryRegion->PageMask,
                    0
            );
            if (osStatus != OS_EOK) {
                ERROR("MemoryRegionResize failed to commit kernel mapping at 0x%" PRIxIN ", i=%i", kernelAddress, i);
                break;
            }

            // then user mapping
            osStatus = MemorySpaceCommit(
                    GetCurrentMemorySpace(),
                    userAddress,
                    &memoryRegion->Pages[i],
                    GetMemorySpacePageSize(),
                    memoryRegion->PageMask,
                    MAPPING_PHYSICAL_FIXED
            );
            if (osStatus != OS_EOK) {
                ERROR("MemoryRegionResize failed to commit user mapping at 0x%" PRIxIN ", i=%i", userAddress, i);
                break;
            }
        }
    }
    return osStatus;
}

static oserr_t __ExpandMemoryRegion(
        _In_ MemoryRegion_t* memoryRegion,
        _In_ uintptr_t       userAddress,
        _In_ int             currentPageCount,
        _In_ size_t          newLength)
{
    oserr_t osStatus;
    uintptr_t  end;

    // The behaviour for scattered regions will be that when resize is called
    // then we now treat it as one continously buffered region, and this means
    // we fill in all blanks
    osStatus = __FillInMemoryRegion(memoryRegion, userAddress, currentPageCount);
    if (osStatus != OS_EOK) {
        return osStatus;
    }

    // commit the new pages as one continous operation, this we can do
    end      = memoryRegion->KernelMapping + (currentPageCount * GetMemorySpacePageSize());
    osStatus = MemorySpaceCommit(
            GetCurrentMemorySpace(),
            end,
            &memoryRegion->Pages[currentPageCount],
            newLength - memoryRegion->Length,
            memoryRegion->PageMask,
            0
    );
    if (osStatus != OS_EOK) {
        return osStatus;
    }

    // Calculate from where we should start committing new pages
    end      = userAddress + (currentPageCount * GetMemorySpacePageSize());
    osStatus = MemorySpaceCommit(
            GetCurrentMemorySpace(),
            end,
            &memoryRegion->Pages[currentPageCount],
            newLength - memoryRegion->Length,
            memoryRegion->PageMask,
            MAPPING_PHYSICAL_FIXED
    );
    return osStatus;
}

oserr_t
MemoryRegionResize(
        _In_ uuid_t handle,
        _In_ void*  memory,
        _In_ size_t newLength)
{
    MemoryRegion_t* memoryRegion;
    int             currentPages;
    int             newPages;
    oserr_t      osStatus;
    
    // Lookup region
    memoryRegion = LookupHandleOfType(handle, HandleTypeSHM);
    if (!memoryRegion) {
        return OS_ENOENT;
    }
    
    // Verify that the new length is not exceeding capacity
    if (newLength > memoryRegion->Capacity) {
        return OS_EINVALPARAMS;
    }
    
    MutexLock(&memoryRegion->SyncObject);
    currentPages = DIVUP(memoryRegion->Length, GetMemorySpacePageSize());
    newPages     = DIVUP(newLength, GetMemorySpacePageSize());
    
    // If we are shrinking (not supported atm) or equal then simply move on
    // and report success. We won't perform any unmapping
    if (currentPages >= newPages) {
        osStatus = OS_ENOTSUPPORTED;
        goto exit;
    }
    else {
        osStatus = __ExpandMemoryRegion(memoryRegion, (uintptr_t)memory, currentPages, newLength);
    }

    if (osStatus == OS_EOK) {
        memoryRegion->Length = newLength;
    }

exit:
    MutexUnlock(&memoryRegion->SyncObject);
    TRACE("MemoryRegionResize returns=%u", osStatus);
    return osStatus;
}

static oserr_t __RefreshMemoryRegion(
        _In_ MemoryRegion_t* memoryRegion,
        _In_ uintptr_t       userStart,
        _In_ int             pageCount)
{
    uintptr_t  userAddress   = userStart;
    oserr_t osStatus      = OS_EOK;

    for (int i = 0; i < pageCount; i++, userAddress += GetMemorySpacePageSize()) {
        if (memoryRegion->Pages[i] && IsMemorySpacePagePresent(GetCurrentMemorySpace(), userAddress) != OS_EOK) {
            osStatus = MemorySpaceCommit(
                    GetCurrentMemorySpace(),
                    userAddress,
                    &memoryRegion->Pages[i],
                    GetMemorySpacePageSize(),
                    memoryRegion->PageMask,
                    MAPPING_PHYSICAL_FIXED
            );
            if (osStatus != OS_EOK) {
                ERROR("__RefreshMemoryRegion failed to commit user mapping at 0x%" PRIxIN ", i=%i", userAddress, i);
                break;
            }
        }
    }
    return osStatus;
}

oserr_t
MemoryRegionRefresh(
        _In_  uuid_t  handle,
        _In_  void*   memory,
        _In_  size_t  currentLength,
        _Out_ size_t* newLength)
{
    MemoryRegion_t* memoryRegion;
    int             currentPages;
    int             newPages;
    uintptr_t       end;
    oserr_t      osStatus;
    
    // Lookup region
    memoryRegion = LookupHandleOfType(handle, HandleTypeSHM);
    if (!memoryRegion) {
        return OS_ENOENT;
    }
    
    MutexLock(&memoryRegion->SyncObject);
    
    // Update the out first
    *newLength = memoryRegion->Length;
    
    // Calculate the new number of pages that should be mapped,
    // but instead of using the provided argument as new, it must be the previous
    currentPages = DIVUP(currentLength, GetMemorySpacePageSize());
    newPages     = DIVUP(memoryRegion->Length, GetMemorySpacePageSize());

    // Before expanding or shrinking, we want to make sure we fill in any blanks in the existing region
    // if the region has been scattered. It's not that we need to do this, but it's the behaviour we want
    // the region to have
    osStatus = __RefreshMemoryRegion(memoryRegion, (uintptr_t)memory, currentPages);
    if (osStatus != OS_EOK) {
        goto exit;
    }
    
    // If we are shrinking (not supported atm) or equal then simply move on
    // and report success. We won't perform any unmapping
    if (currentPages >= newPages) {
        osStatus = OS_ENOTSUPPORTED;
        goto exit;
    }
    
    // Otherwise, commit mappings, but instead of doing like the Resize
    // operation we will tell that we provide them ourselves
    end    = (uintptr_t)memory + (currentPages * GetMemorySpacePageSize());
    osStatus = MemorySpaceCommit(
            GetCurrentMemorySpace(),
            end,
            &memoryRegion->Pages[currentPages],
            memoryRegion->Length - currentLength,
            memoryRegion->PageMask,
            MAPPING_PHYSICAL_FIXED
    );

exit:
    MutexUnlock(&memoryRegion->SyncObject);
    return osStatus;
}

struct SHMBuffer {
    uuid_t       ID;
    Mutex_t      Mutex;
    vaddr_t      KernelMapping;
    size_t       Length;
    size_t       PageMask;
    unsigned int Flags;
    int          PageCount;
    paddr_t      Pages[];
};

static void
__SHMBufferDelete(
        _In_ struct SHMBuffer* buffer)
{

}

static struct SHMBuffer*
__SHMBufferNew(
        _In_ size_t       size,
        _In_ unsigned int flags)
{
    struct SHMBuffer* buffer;
    size_t            pageSize  = GetMemorySpacePageSize();
    size_t            pageCount = DIVUP(size, pageSize);
    size_t            structSize = sizeof(struct SHMBuffer) + (pageCount * sizeof(uintptr_t));

    buffer = kmalloc(structSize);
    if (buffer == NULL) {
        return NULL;
    }
    memset(buffer, 0, structSize);

    MutexConstruct(&buffer->Mutex, MUTEX_FLAG_PLAIN);
    buffer->ID = CreateHandle(
            HandleTypeSHM,
            (HandleDestructorFn)__SHMBufferDelete,
            buffer
    );
    buffer->PageCount = (int)pageCount;
    buffer->Length = size;
    buffer->Flags = flags;
    buffer->PageMask = __MASK;
    return buffer;
}

static unsigned int
__MapFlagsForDevice(
        _In_ SHM_t* shm)
{
    unsigned int flags = MAPPING_USERSPACE | MAPPING_NOCACHE | MAPPING_COMMIT;
    if (!(shm->Access & SHM_ACCESS_WRITE)) {
        flags |= MAPPING_READONLY;
    }
    if (shm->Access & SHM_ACCESS_EXECUTE) {
        flags |= MAPPING_EXECUTABLE;
    }
    if (shm->Flags & SHM_CLEAN) {
        flags |= MAPPING_CLEAN;
    }
    return flags;
}

static oserr_t
__CreateDeviceBuffer(
        _In_  struct SHMBuffer* buffer,
        _In_  SHM_t*            shm,
        _Out_ void**            userMapping)
{
    unsigned int mapFlags = __MapFlagsForDevice(shm);
    oserr_t      oserr;
    size_t       pageMask;
    vaddr_t      mapping;

    ArchSHMTypeToPageMask(shm->Type, &pageMask);
    oserr = MemorySpaceMap(
            GetCurrentMemorySpace(),
            &mapping,
            &buffer->Pages[0],
            shm->Size,
            pageMask,
            mapFlags,
            MAPPING_VIRTUAL_PROCESS
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    buffer->PageMask = pageMask;
    *userMapping = (void*)mapping;
    return OS_EOK;
}

static unsigned int
__MapFlagsForStack(
        _In_ SHM_t* shm)
{
    unsigned int flags = MAPPING_USERSPACE | MAPPING_GUARDPAGE;
    if (!(shm->Access & SHM_ACCESS_WRITE)) {
        flags |= MAPPING_READONLY;
    }
    if (shm->Access & SHM_ACCESS_EXECUTE) {
        flags |= MAPPING_EXECUTABLE;
    }
    if (shm->Flags & SHM_CLEAN) {
        flags |= MAPPING_CLEAN;
    }
    return flags;
}

static oserr_t
__CreateStackBuffer(
        _In_  struct SHMBuffer* buffer,
        _In_  SHM_t*            shm,
        _Out_ void**            userMapping)
{
    MemorySpace_t* memorySpace = GetCurrentMemorySpace();
    unsigned int   mapFlags = __MapFlagsForStack(shm);
    size_t         pageSize = GetMemorySpacePageSize();
    oserr_t        oserr;
    vaddr_t        contextAddress;

    // Reserve the region and map only the top page. The returned
    // mapping should point to the end of the region. A guard page must
    // be reserved.
    oserr = MemorySpaceMapReserved(
            memorySpace,
            &contextAddress,
            shm->Size,
            mapFlags,
            MAPPING_VIRTUAL_PROCESS
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    // Adjust pointer to top of stack and then commit the first stack page
    oserr = MemorySpaceCommit(
            memorySpace,
            contextAddress + (shm->Size - pageSize),
            &buffer->Pages[buffer->PageCount - 1],
            pageSize,
            0, 0
    );
    if (oserr != OS_EOK) {
        MemorySpaceUnmap(memorySpace, contextAddress, shm->Size);
    }

    // Return a pointer to STACK_TOP
    *userMapping = (void*)(contextAddress + shm->Size);
    return oserr;
}

static unsigned int
__MapFlagsForIPC(
        _In_  SHM_t* shm)
{
    unsigned int flags = MAPPING_USERSPACE | MAPPING_COMMIT;
    if (shm->Flags & MAPPING_CLEAN) {
        flags |= MAPPING_CLEAN;
    }
    return flags;
}

static oserr_t
__CreateIPCBuffer(
        _In_  struct SHMBuffer* buffer,
        _In_  SHM_t*            shm,
        _Out_ void**            kernelMappingOut,
        _Out_ void**            userMappingOut)
{
    MemorySpace_t* memorySpace    = GetCurrentMemorySpace();
    unsigned int   userMapFlags   = __MapFlagsForIPC(shm);
    unsigned int   kernelMapFlags = MAPPING_PERSISTENT;
    oserr_t        oserr;
    vaddr_t        userMapping;
    vaddr_t        kernelMapping;

    // Needs both an userspace mapping and a kernel mapping. IPC buffers
    // are special in this sense, since we want to anonomously be able to
    // send messages, without having direct access to the buffer itself from
    // the view of the process.
    oserr = MemorySpaceMap(
            memorySpace,
            &userMapping,
            &buffer->Pages[0],
            shm->Size,
            0,
            userMapFlags,
            MAPPING_VIRTUAL_PROCESS
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = MemorySpaceMap(
            memorySpace,
            &kernelMapping,
            &buffer->Pages[0],
            shm->Size,
            0,
            kernelMapFlags,
            MAPPING_PHYSICAL_FIXED | MAPPING_VIRTUAL_GLOBAL
    );
    if (oserr != OS_EOK) {
        (void)MemorySpaceUnmap(memorySpace, userMapping, shm->Size);
        return oserr;
    }

    buffer->KernelMapping = kernelMapping;
    *kernelMappingOut = (void*)kernelMapping;
    *userMappingOut = (void*)userMapping;
    return OS_EOK;
}

static unsigned int
__MapFlagsForTrap(
        _In_  SHM_t* shm)
{
    unsigned int flags = MAPPING_USERSPACE | MAPPING_TRAPPAGE;
    if (shm->Flags & MAPPING_CLEAN) {
        flags |= MAPPING_CLEAN;
    }
    return flags;
}

static oserr_t
__CreateTrapBuffer(
        _In_  SHM_t* shm,
        _Out_ void** userMappingOut)
{
    unsigned int mapFlags = __MapFlagsForTrap(shm);
    oserr_t      oserr;
    vaddr_t      mapping;

    // Trap buffers use only reserved memory and trigger signals when
    // an unmapped page has been accessed.
    oserr = MemorySpaceMapReserved(
            GetCurrentMemorySpace(),
            &mapping,
            shm->Size,
            mapFlags,
            MAPPING_VIRTUAL_PROCESS
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
    unsigned int flags = MAPPING_USERSPACE;
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
            &mapping,
            &buffer->Pages[0],
            shm->Size,
            0,
            mapFlags,
            MAPPING_VIRTUAL_PROCESS
    );
    if (oserr != OS_EOK) {
        return oserr;
    }
    *userMappingOut = (void*)mapping;
    return OS_EOK;
}

static oserr_t
__CreateRegularUnmappedBuffer(
        _In_  SHM_t* shm,
        _Out_ void** userMappingOut)
{
    unsigned int mapFlags = __MapFlagsForRegular(shm);
    oserr_t      oserr;
    vaddr_t      mapping;

    // Trap buffers use only reserved memory and trigger signals when
    // an unmapped page has been accessed.
    oserr = MemorySpaceMapReserved(
            GetCurrentMemorySpace(),
            &mapping,
            shm->Size,
            mapFlags,
            MAPPING_VIRTUAL_PROCESS
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
    return __CreateRegularUnmappedBuffer(shm, userMappingOut);
}

oserr_t
SHMCreate(
        _In_  SHM_t*  shm,
        _Out_ void**  kernelMapping,
        _Out_ void**  userMapping,
        _Out_ uuid_t* handleOut)
{
    struct SHMBuffer* buffer;
    oserr_t           oserr;

    if (shm == NULL || kernelMapping == NULL ||
        userMapping == NULL || handleOut == NULL) {
        return OS_EINVALPARAMS;
    }

    if (shm->Size == 0) {
        return OS_EINVALPARAMS;
    }

    buffer = __SHMBufferNew(shm->Size, shm->Flags);
    if (buffer == NULL) {
        return OS_EOOM;
    }

    // Determine the type of shared memory that should be setup from
    // the flags.
    if (SHM_KIND(shm->Flags) == SHM_DEVICE) {
        oserr = __CreateDeviceBuffer(buffer, shm, userMapping);
    } else if (SHM_KIND(shm->Flags) == SHM_STACK) {
        oserr = __CreateStackBuffer(buffer, shm, userMapping);
    } else if (SHM_KIND(shm->Flags) == SHM_IPC) {
        oserr = __CreateIPCBuffer(buffer, shm, kernelMapping, userMapping);
    } else if (SHM_KIND(shm->Flags) == SHM_TRAP) {
        oserr = __CreateTrapBuffer(shm, userMapping);
    } else {
        oserr = __CreateRegularBuffer(buffer, shm, userMapping);
    }

    if (oserr != OS_EOK) {
        __SHMBufferDelete(buffer);
    } else {
        *handleOut = buffer->ID;
    }
    return oserr;
}

static unsigned int
__FilterFlagsForExport(
        _In_ unsigned int flags)
{
    unsigned int filtered = SHM_PERSISTANT | SHM_COMMIT;
    if (flags & SHM_PRIVATE) {
        filtered |= SHM_PRIVATE;
    }
    return filtered;
}

oserr_t
SHMExport(
        _In_  void*        memory,
        _In_  size_t       size,
        _In_  unsigned int flags,
        _In_  unsigned int accessFlags,
        _Out_ uuid_t*      handleOut)
{
    struct SHMBuffer* buffer;
    oserr_t           oserr;

    if (memory == NULL || size == 0 || handleOut == NULL) {
        return OS_EINVALPARAMS;
    }

    buffer = __SHMBufferNew(size, __FilterFlagsForExport(flags));
    if (buffer == NULL) {
        return OS_EOOM;
    }

    oserr = GetMemorySpaceMapping(
            GetCurrentMemorySpace(),
            (vaddr_t)memory,
            buffer->PageCount,
            &buffer->Pages[0]
    );
    if (oserr != OS_EOK) {
        __SHMBufferDelete(buffer);
    } else {
        *handleOut = buffer->ID;
    }
    return oserr;
}

oserr_t
SHMAttach(
        _In_ uuid_t  shmID,
        _In_ size_t* sizeOut)
{
    struct SHMBuffer* shmBuffer;
    oserr_t           oserr;

    oserr = AcquireHandleOfType(
            shmID,
            HandleTypeSHM,
            (void**)&shmBuffer
    );
    if (oserr != OS_EOK) {
        ERROR("SHMAttach handle %u was invalid", shmID);
        return OS_ENOENT;
    }

    *sizeOut = shmBuffer->Length;
    return OS_EOK;
}

static oserr_t
__UpdateMappingFlags(
        _In_ SHMHandle_t* handle,
        _In_ unsigned int flags)
{
    unsigned int previousFlags;
    return MemorySpaceChangeProtection(
            GetCurrentMemorySpace(),
            (vaddr_t)handle->Buffer,
            handle->Length,
            flags,
            &previousFlags
    );
}

static oserr_t
__ShrinkMapping(
        _In_ SHMHandle_t* handle,
        _In_ size_t       length)
{
    // Calculate the start of the unmapping and the length
    size_t    pageSize        = GetMemorySpacePageSize();
    size_t    correctedLength = DIVUP(length, pageSize) * pageSize;
    uintptr_t startOfUnmap    = (uintptr_t)handle->Buffer + correctedLength;
    size_t    lengthOfUnmap   = handle->Length - correctedLength;
    return MemorySpaceUnmap(GetCurrentMemorySpace(), startOfUnmap, lengthOfUnmap);
}

static oserr_t
__ExpandMapping(
        _In_ struct SHMBuffer* shmBuffer,
        _In_ SHMHandle_t*      handle,
        _In_ size_t            length,
        _In_ unsigned int      flags)
{

}

static oserr_t
__UpdateMapping(
        _In_ struct SHMBuffer* shmBuffer,
        _In_ SHMHandle_t*      handle,
        _In_ size_t            length,
        _In_ unsigned int      flags)
{
    // TODO use MemorySpaceMap
    if (length == handle->Length) {
        return __UpdateMappingFlags(handle, flags);
    } else if (length < handle->Length) {
        return __ShrinkMapping(handle, length);
    } else {
        return __ExpandMapping(shmBuffer, handle, length, flags);
    }
}

oserr_t
__CreateMapping(
        _In_  struct SHMBuffer* shmBuffer,
        _In_  size_t            offset,
        _In_  size_t            length,
        _In_  unsigned int      flags,
        _Out_ void**            mappingOut)
{

}

oserr_t
SHMMap(
        _In_ SHMHandle_t* handle,
        _In_ size_t       offset,
        _In_ size_t       length,
        _In_ unsigned int flags)
{
    struct SHMBuffer* shmBuffer;
    oserr_t           oserr;
    void*             mapping;

    if (handle == NULL || length == 0) {
        return OS_EINVALPARAMS;
    }

    shmBuffer = LookupHandleOfType(handle->ID, HandleTypeSHM);
    if (!shmBuffer) {
        return OS_ENOENT;
    }

    if (handle->Buffer != NULL) {
        // There is a few cases here. If we are trying to modify an already
        // mapped buffer, and the offsets are identical, then we are either
        // expanding or shrinking, or changing protections flags
        if (handle->Offset == offset) {
            return __UpdateMapping(shmBuffer, handle, length, flags);
        }

        // Ok mapping has changed. We do not unmap the previous
        // mapping before having created a new mapping.
    }

    oserr = __CreateMapping(shmBuffer, offset, length, flags, &mapping);
    if (oserr != OS_EOK) {
        return oserr;
    }

    if (handle->Buffer != NULL) {
        oserr = SHMUnmap(handle);
        if (oserr != OS_EOK) {
            (void)MemorySpaceUnmap(
                    GetCurrentMemorySpace(),
                    (vaddr_t)mapping,
                    length
            );
            return oserr;
        }
    }

    handle->Buffer = mapping;
    handle->Offset = offset;
    handle->Length = length;
    return OS_EOK;
}

oserr_t
SHMUnmap(
        _In_ SHMHandle_t* handle)
{
    oserr_t oserr;

    if (handle == NULL || handle->Buffer == NULL) {
        return OS_EINVALPARAMS;
    }

    oserr = MemorySpaceUnmap(
            GetCurrentMemorySpace(),
            (vaddr_t)handle->Buffer,
            handle->Length
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    handle->Buffer = NULL;
    handle->Offset = 0;
    handle->Length = 0;
    return OS_EOK;
}

oserr_t
SHMCommit(
        _In_ uuid_t handle,
        _In_ void*  memoryBase,
        _In_ void*  memory,
        _In_ size_t length)
{
    struct SHMBuffer* shmBuffer;
    oserr_t           oserr;
    size_t            pageSize = GetMemorySpacePageSize();
    uintptr_t         userAddress = (uintptr_t)memory;
    uintptr_t         kernelAddress;
    int               limit;
    int               i;

    shmBuffer = LookupHandleOfType(handle, HandleTypeSHM);
    if (!shmBuffer) {
        return OS_ENOENT;
    }

    i     = DIVUP((uintptr_t)memoryBase - userAddress, pageSize);
    limit = i + (int)(DIVUP(length, pageSize));
    kernelAddress = shmBuffer->KernelMapping;

    MutexLock(&shmBuffer->Mutex);
    for (; i < limit; i++, kernelAddress += pageSize, userAddress += pageSize) {
        if (!shmBuffer->Pages[i]) {
            // handle kernel mapping first
            oserr = MemorySpaceCommit(
                    GetCurrentMemorySpace(),
                    kernelAddress,
                    &shmBuffer->Pages[i],
                    pageSize,
                    shmBuffer->PageMask,
                    0
            );
            if (oserr != OS_EOK) {
                ERROR("SHMCommit failed to commit kernel mapping at 0x%" PRIxIN ", i=%i", kernelAddress, i);
                break;
            }

            // then user mapping
            oserr = MemorySpaceCommit(
                    GetCurrentMemorySpace(),
                    userAddress,
                    &shmBuffer->Pages[i],
                    pageSize,
                    shmBuffer->PageMask,
                    MAPPING_PHYSICAL_FIXED
            );
            if (oserr != OS_EOK) {
                ERROR("SHMCommit failed to commit user mapping at 0x%" PRIxIN ", i=%i", userAddress, i);
                break;
            }
        }
    }
    MutexUnlock(&shmBuffer->Mutex);
    return oserr;
}

oserr_t
SHMRead(
        _In_  uuid_t  handle,
        _In_  size_t  offset,
        _In_  void*   buffer,
        _In_  size_t  length,
        _Out_ size_t* bytesReadOut)
{
    struct SHMBuffer* shmBuffer;
    size_t            clampedLength;

    if (buffer == NULL || length == 0) {
        return OS_EINVALPARAMS;
    }

    shmBuffer = LookupHandleOfType(handle, HandleTypeSHM);
    if (!shmBuffer) {
        return OS_ENOENT;
    }

    if (offset >= shmBuffer->Length) {
        return OS_EINVALPARAMS;
    }

    clampedLength = MIN(shmBuffer->Length - offset, length);
    ReadVolatileMemory((const volatile void*)(shmBuffer->KernelMapping + offset),
                       (volatile void*)buffer, clampedLength);

    *bytesReadOut = clampedLength;
    return OS_EOK;
}

oserr_t
SHMWrite(
        _In_  uuid_t      handle,
        _In_  size_t      offset,
        _In_  const void* buffer,
        _In_  size_t      length,
        _Out_ size_t*     bytesWrittenOut)
{
    struct SHMBuffer* shmBuffer;
    size_t            clampedLength;

    if (buffer == NULL || length == 0) {
        return OS_EINVALPARAMS;
    }

    shmBuffer = LookupHandleOfType(handle, HandleTypeSHM);
    if (!shmBuffer) {
        return OS_ENOENT;
    }

    if (offset >= shmBuffer->Length) {
        return OS_EINVALPARAMS;
    }

    clampedLength = MIN(shmBuffer->Length - offset, length);
    WriteVolatileMemory((volatile void*)(shmBuffer->KernelMapping + offset),
                        (void*)buffer, clampedLength);

    *bytesWrittenOut = clampedLength;
    return OS_EOK;
}

#define SG_IS_SAME_REGION(memory_region, idx, idx2, pageSize) \
    (((memory_region)->Pages[idx] + (pageSize) == (memory_region)->Pages[idx2]) || \
     ((memory_region)->Pages[idx] == 0 && (memory_region)->Pages[idx2] == 0))

oserr_t
SHMBuildSG(
        _In_  uuid_t   handle,
        _Out_ int*     sgCountOut,
        _Out_ SHMSG_t* sgOut)
{
    struct SHMBuffer* shmBuffer;
    size_t            pageSize = GetMemorySpacePageSize();

    if (!sgCountOut) {
        return OS_EINVALPARAMS;
    }

    shmBuffer = LookupHandleOfType(handle, HandleTypeSHM);
    if (!shmBuffer) {
        return OS_ENOENT;
    }

    // Requested count of the scatter-gather units, so count
    // how many entries it would take to fill a list
    // Assume that if both pointers are supplied we are trying to fill
    // the list with the requested amount, and thus skip this step.
    if (!sgOut) {
        int sgCount = 0;
        for (int i = 0; i < shmBuffer->PageCount; i++) {
            if (i == 0 || !SG_IS_SAME_REGION(shmBuffer, i - 1, i, pageSize)) {
                sgCount++;
            }
        }
        *sgCountOut = sgCount;
    }

    // In order to get the list both counters must be filled
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

        // Adjust the initial sg entry for offset
        sgOut[0].Length -= sgOut[0].Address % pageSize;
    }
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

    shmBuffer = LookupHandleOfType(handle, HandleTypeSHM);
    if (!shmBuffer) {
        return OS_ENOENT;
    }

    *bufferOut = (void*)shmBuffer->KernelMapping;
    return OS_EOK;
}
