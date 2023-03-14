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
#include <debug.h>
#include <handle.h>
#include <heap.h>
#include <machine.h>
#include <memoryspace.h>
#include <threading.h>
#include <shm.h>
#include <string.h>

struct SHMBuffer {
    uuid_t          ID;
    MemorySpace_t* Owner;
    Mutex_t        Mutex;
    vaddr_t        KernelMapping;
    size_t         Length;
    size_t         PageMask;
    unsigned int   Flags;
    bool           Exported;
    int            PageCount;
    paddr_t        Pages[];
};

static void
__SHMBufferDelete(
        _In_ struct SHMBuffer* shmBuffer)
{
    if (shmBuffer == NULL) {
        return;
    }

    if (!(shmBuffer->Exported)) {
        FreePhysicalMemory(shmBuffer->PageCount, &shmBuffer->Pages[0]);
    }

    MutexDestruct(&shmBuffer->Mutex);
    kfree(shmBuffer);
}

static struct SHMBuffer*
__SHMBufferNew(
        _In_ size_t       size,
        _In_ unsigned int flags,
        _In_ bool         exported)
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
    buffer->Owner = GetCurrentMemorySpace();
    buffer->PageCount = (int)pageCount;
    buffer->Length = size;
    buffer->Flags = flags;
    buffer->Exported = exported;
    buffer->PageMask = __MASK;
    return buffer;
}

static unsigned int
__MapFlagsForDevice(
        _In_ SHM_t* shm)
{
    unsigned int flags = MAPPING_USERSPACE | MAPPING_PERSISTENT | MAPPING_NOCACHE | MAPPING_COMMIT;
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
            &(struct MemorySpaceMapOptions) {
                .SHMTag = buffer->ID,
                .Pages = &buffer->Pages[0],
                .Length = shm->Size,
                .Mask = pageMask,
                .Flags = mapFlags,
                .PlacementFlags = MAPPING_VIRTUAL_PROCESS
            },
            &mapping
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    buffer->PageMask = pageMask;
    *userMapping = (void*)mapping;
    return OS_EOK;
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
        _Out_ void**            kernelMappingOut,
        _Out_ void**            userMappingOut)
{
    MemorySpace_t* memorySpace    = GetCurrentMemorySpace();
    unsigned int   userMapFlags   = __MapFlagsForIPC(shm);
    unsigned int   kernelMapFlags = MAPPING_COMMIT | MAPPING_PERSISTENT;
    oserr_t        oserr;
    vaddr_t        userMapping;
    vaddr_t        kernelMapping;

    // Needs both an userspace mapping and a kernel mapping. IPC buffers
    // are special in this sense, since we want to anonomously be able to
    // send messages, without having direct access to the buffer itself from
    // the view of the process.
    oserr = MemorySpaceMap(
            memorySpace,
            &(struct MemorySpaceMapOptions) {
                .SHMTag = buffer->ID,
                .Pages = &buffer->Pages[0],
                .Length = shm->Size,
                .Mask = buffer->PageMask,
                .Flags = userMapFlags,
                .PlacementFlags = MAPPING_VIRTUAL_PROCESS
            },
            &userMapping
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = MemorySpaceMap(
            memorySpace,
            &(struct MemorySpaceMapOptions) {
                .SHMTag = buffer->ID,
                .Pages = &buffer->Pages[0],
                .Length = shm->Size,
                .Mask = buffer->PageMask,
                .Flags = kernelMapFlags,
                .PlacementFlags = MAPPING_PHYSICAL_FIXED | MAPPING_VIRTUAL_GLOBAL
            },
            &kernelMapping
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

    // Trap buffers use only reserved memory and trigger signals when
    // an unmapped page has been accessed.
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
                .Mask = buffer->PageMask,
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

    // Trap buffers use only reserved memory and trigger signals when
    // an unmapped page has been accessed.
    oserr = MemorySpaceMap(
            GetCurrentMemorySpace(),
            &(struct MemorySpaceMapOptions) {
                .SHMTag = buffer->ID,
                .Length = shm->Size,
                .Mask = buffer->PageMask,
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

    buffer = __SHMBufferNew(shm->Size, shm->Flags, false);
    if (buffer == NULL) {
        return OS_EOOM;
    }

    // Determine the type of shared memory that should be setup from
    // the flags.
    if (SHM_KIND(shm->Flags) == SHM_DEVICE) {
        oserr = __CreateDeviceBuffer(buffer, shm, userMapping);
    } else if (SHM_KIND(shm->Flags) == SHM_IPC) {
        oserr = __CreateIPCBuffer(buffer, shm, kernelMapping, userMapping);
    } else if (SHM_KIND(shm->Flags) == SHM_TRAP) {
        oserr = __CreateTrapBuffer(buffer, shm, userMapping);
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
    unsigned int filtered = SHM_COMMIT;
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

    buffer = __SHMBufferNew(size, __FilterFlagsForExport(flags), true);
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
        return OS_ENOENT;
    }

    // If the buffer is marked PRIVATE, then we cannot let anyone else try
    // to map this buffer, except for the buffer owner.
    if (shmBuffer->Flags & SHM_PRIVATE) {
        oserr = AreMemorySpacesRelated(shmBuffer->Owner, GetCurrentMemorySpace());
        if (oserr != OS_EOK) {
            return OS_EPERMISSIONS;
        }
    }

    *sizeOut = shmBuffer->Length;
    return OS_EOK;
}

static oserr_t
__EnsurePhysicalPages(
        _In_ struct SHMBuffer* shmBuffer,
        _In_ int               pageStart,
        _In_ int               pageCount)
{
    MutexLock(&shmBuffer->Mutex);
    for (int i = 0; i < pageCount; i++) {
        oserr_t oserr;

        if (shmBuffer->Pages[pageStart + i]) {
            continue;
        }

        oserr = AllocatePhysicalMemory(shmBuffer->PageMask, 1, &shmBuffer->Pages[pageStart + i]);
        if (oserr != OS_EOK) {
            MutexUnlock(&shmBuffer->Mutex);
            return oserr;
        }
    }
    MutexUnlock(&shmBuffer->Mutex);
    return OS_EOK;
}

static unsigned int
__RecalculateFlags(
        _In_ unsigned int shmFlags,
        _In_ unsigned int accessFlags)
{
    // All mappings must be marked USERSPACE and PERSISTENT. We do not allow
    // the underlying virtual memory system to automatically free any physical
    // pages as we want the full control over them until the SHM buffer is freed.
    unsigned int flags = MAPPING_USERSPACE | MAPPING_PERSISTENT;

    // SHM_CLEAN we include when doing maps
    if (shmFlags & SHM_CLEAN) {
        flags |= MAPPING_CLEAN;
    }

    // SHM_COMMIT we ignore as we already have this handled by
    // SHM_ACCESS_COMMIT.

    // Ignore MAPPING_TRAPPAGE and MAPPING_GUARDPAGE attributes for
    // stack and trap SHM buffers. When they are originally mapped in,
    // by the owners of the buffers, they are correctly mapped with these
    // attributes, and consumers of these SHM buffers don't actually need
    // those mappings.
    // TODO: do we actually encounter issues with this?

    // Handle access modifiers last
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
    size_t  pageIndex = handle->Offset / pageSize;
    oserr_t oserr;

    // Ensure that physical pages are allocated for this mapping, so we can
    // map with PHYSICAL_FIXED, but *only* if SHM_ACCESS_COMMIT is provided
    if (flags & SHM_ACCESS_COMMIT) {
        oserr = __EnsurePhysicalPages(shmBuffer, (int)pageIndex, (int)pageCount);
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
                .Mask = shmBuffer->PageMask,
                .Flags = __RecalculateFlags(shmBuffer->Flags, flags),
                .PlacementFlags = MAPPING_PHYSICAL_FIXED | MAPPING_VIRTUAL_FIXED
            },
            (vaddr_t*)&handle->Buffer
    );
}

oserr_t
__CreateMapping(
        _In_  struct SHMBuffer* shmBuffer,
        _In_  size_t            offset,
        _In_  size_t            length,
        _In_  unsigned int      flags,
        _Out_ void**            mappingOut)
{
    size_t  pageSize        = GetMemorySpacePageSize();
    size_t  pageIndex       = offset / pageSize;
    size_t  pageCount       = DIVUP(length, pageSize);
    size_t  correctedLength = pageCount * pageSize;
    oserr_t oserr;

    // Ensure that the page index is not oob, if it is we cannot trust
    // the above calculations
    if (pageIndex >= shmBuffer->PageCount) {
        return OS_EINVALPARAMS;
    }

    // Ensure that physical pages are allocated for this mapping, so we can
    // map with PHYSICAL_FIXED, but *only* if SHM_ACCESS_COMMIT is provided
    if (flags & SHM_ACCESS_COMMIT) {
        oserr = __EnsurePhysicalPages(shmBuffer, (int)pageIndex, (int)pageCount);
        if (oserr != OS_EOK) {
            return oserr;
        }
    }

    return MemorySpaceMap(
            GetCurrentMemorySpace(),
            &(struct MemorySpaceMapOptions) {
                .SHMTag = shmBuffer->ID,
                .Pages = &shmBuffer->Pages[pageIndex],
                .Length = correctedLength,
                .Mask = shmBuffer->PageMask,
                .Flags = __RecalculateFlags(shmBuffer->Flags, flags),
                .PlacementFlags = MAPPING_PHYSICAL_FIXED | MAPPING_VIRTUAL_PROCESS
            },
            (vaddr_t*)mappingOut
    );
}

static size_t
__ClampLength(
        _In_ struct SHMBuffer* shmBuffer,
        _In_ size_t            offset,
        _In_ size_t            length)
{
    size_t pageSize        = GetMemorySpacePageSize();
    size_t correctedOffset = offset % pageSize;
    size_t pageIndex       = offset / pageSize;
    return MIN(length + correctedOffset, (shmBuffer->PageCount - pageIndex) * pageSize);
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
    size_t            clampedLength;

    if (handle == NULL || length == 0) {
        return OS_EINVALPARAMS;
    }

    shmBuffer = LookupHandleOfType(handle->ID, HandleTypeSHM);
    if (!shmBuffer) {
        return OS_ENOENT;
    }

    // If the buffer is marked PRIVATE, then we cannot let anyone else try
    // to map this buffer, except for the buffer owner.
    if (shmBuffer->Flags & SHM_PRIVATE) {
        oserr = AreMemorySpacesRelated(shmBuffer->Owner, GetCurrentMemorySpace());
        if (oserr != OS_EOK) {
            return OS_EPERMISSIONS;
        }
    }

    // Calculate the actual length of the mapping
    clampedLength = __ClampLength(shmBuffer, offset, length);

    if (handle->Buffer != NULL) {
        // There is a few cases here. If we are trying to modify an already
        // mapped buffer, and the offsets are identical, then we are either
        // expanding or shrinking, or changing protections flags
        if (handle->Offset == offset) {
            return __UpdateMapping(shmBuffer, handle, clampedLength, flags);
        }

        // Ok mapping has changed. We do not unmap the previous
        // mapping before having created a new mapping.
    }

    oserr = __CreateMapping(shmBuffer, offset, clampedLength, flags, &mapping);
    if (oserr != OS_EOK) {
        return oserr;
    }

    if (handle->Buffer != NULL) {
        oserr = SHMUnmap(handle, (vaddr_t)handle->Buffer, handle->Length);
        if (oserr != OS_EOK) {
            (void)MemorySpaceUnmap(
                    GetCurrentMemorySpace(),
                    (vaddr_t)mapping,
                    clampedLength
            );
            return oserr;
        }
    }

    handle->Buffer = mapping;
    handle->Offset = offset;
    handle->Length = clampedLength;
    return OS_EOK;
}

oserr_t
SHMUnmap(
        _In_ SHMHandle_t* handle,
        _In_ vaddr_t      address,
        _In_ size_t       length)
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
