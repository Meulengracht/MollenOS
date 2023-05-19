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
#include <threading.h>
#include <shm.h>
#include <string.h>
#include "private.h"

static void __BuildSG(struct SHMBuffer* shmBuffer, int* sgCountOut, SHMSG_t* sgOut);

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
        _In_ size_t       offset,
        _In_ size_t       size,
        _In_ unsigned int flags,
        _In_ bool         exported)
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
            (HandleDestructorFn)__SHMBufferDelete,
            buffer
    );
    buffer->Owner = GetCurrentMemorySpace();
    buffer->PageCount = (int)pageCount;
    buffer->Offset = offset;
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

    ArchSHMTypeToPageMask(shm->Conformity, &pageMask);
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
    if (shm->Flags & SHM_CONFORM) {
        ArchSHMTypeToPageMask(shm->Conformity, &buffer->PageMask);
    }

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
    // set up the initial shm handle.
    handle->ID = shmID;
    handle->SourceID = UUID_INVALID;
    handle->SourceFlags = 0;
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
    void*             mapping;

    if (shm == NULL || handle == NULL || shm->Size == 0) {
        return OS_EINVALPARAMS;
    }

    buffer = __SHMBufferNew(0, shm->Size, shm->Flags, false);
    if (buffer == NULL) {
        return OS_EOOM;
    }

    // Determine the type of shared memory that should be setup from
    // the flags.
    if (SHM_KIND(shm->Flags) == SHM_DEVICE) {
        oserr = __CreateDeviceBuffer(buffer, shm, &mapping);
    } else if (SHM_KIND(shm->Flags) == SHM_IPC) {
        oserr = __CreateIPCBuffer(buffer, shm, &mapping);
    } else if (SHM_KIND(shm->Flags) == SHM_TRAP) {
        oserr = __CreateTrapBuffer(buffer, shm, &mapping);
    } else {
        oserr = __CreateRegularBuffer(buffer, shm, &mapping);
    }

    if (oserr != OS_EOK) {
        __SHMBufferDelete(buffer);
        return oserr;
    }

    // Was a key provided? Then we should set up the key
    if (shm->Key != NULL) {
        oserr = RegisterHandlePath(buffer->ID, shm->Key);
        if (oserr != OS_EOK) {
            __SHMBufferDelete(buffer);
            return oserr;
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
    return OS_EOK;
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
        _In_ SHMHandle_t*  handle)
{
    struct SHMBuffer* buffer;
    size_t            offset;
    oserr_t           oserr;

    if (memory == NULL || size == 0 || handle == NULL) {
        return OS_EINVALPARAMS;
    }

    // Remember that we must take page offset of the buffer we are exporting into
    // consideration. It must also be corrected in the *actual* size we are exporting
    offset = ((uintptr_t)memory & (GetMemorySpacePageSize() - 1));
    buffer = __SHMBufferNew(offset, size, __FilterFlagsForExport(flags), true);
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
        return oserr;
    }

    // While we do want to maintain the offset of the original page, we do
    // not want it in our page-mappings. It only confuses things. So clear any
    // offset retrieved through GetMemorySpaceMapping().
    buffer->Pages[0] &= ~(GetMemorySpacePageSize() - 1);

    __ConstructSHMHandle(
            handle,
            buffer->ID,
            size,
            size,
            0,
            memory
    );
    return OS_EOK;
}

static oserr_t
__GatherSGList(
        _In_  struct SHMBuffer* shmBuffer,
        _Out_ int*              sgCountOut,
        _Out_ SHMSG_t**         sgOut)
{
    int      sgCount;
    SHMSG_t* sg;

    __BuildSG(shmBuffer, &sgCount, NULL);

    sg = kmalloc(sgCount * sizeof(SHMSG_t));
    if (sg == NULL) {
        return OS_EOOM;
    }

    __BuildSG(shmBuffer, &sgCount, sg);

    *sgCountOut = sgCount;
    *sgOut = sg;
    return OS_EOK;
}

static bool
__VerifySGAlignment(
        _In_ SHMSG_t* sg,
        _In_ int      sgCount,
        _In_ size_t   offset,
        _In_ uint32_t alignment)
{
    size_t bytesLeft = offset;

    if (alignment == 0) {
        return true;
    }

    // Alignment must be a power of two.
    if (!IsPowerOfTwo(alignment)) {
        ERROR("__VerifySGAlignment: alignment requirement 0x%x is not a power of two!", alignment);
        return false;
    }

    for (int i = 0; i < sgCount; i++) {
        uintptr_t address = sg[i].Address;
        if (sg[i].Length > bytesLeft) {
            // found correct SG entry
            address += bytesLeft;
            if (address & (alignment - 1)) {
                return false;
            }
            return true;
        }
        bytesLeft -= sg[i].Length;
    }

    // offset was wild, just check what's left.
    if (bytesLeft & (alignment - 1)) {
        return false;
    }
    return true;
}

static bool
__VerifyMemoryConformity(
        _In_ SHMSG_t*                sg,
        _In_ int                     sgCount,
        _In_ enum OSMemoryConformity conformity)
{
    size_t pageMask = __MASK;

    if (conformity == OSMEMORYCONFORMITY_NONE) {
        return true;
    }

    // Lookup the page-mask for the specific conformityOpts
    ArchSHMTypeToPageMask(conformity, &pageMask);

    // Verify all SG entries against the page-mask
    for (int i = 0; i < sgCount; i++) {
        if ((sg[i].Address + sg[i].Length) >= pageMask) {
            return false;
        }
    }
    return true;
}

static bool
__VerifySGConformity(
        _In_ SHMSG_t*                sg,
        _In_ int                     sgCount,
        _In_ size_t                  offset,
        _In_ SHMConformityOptions_t* conformityOpts)
{
    if (!__VerifySGAlignment(sg, sgCount, offset, conformityOpts->BufferAlignment)) {
        return false;
    }
    if (!__VerifyMemoryConformity(sg, sgCount, conformityOpts->Conformity)) {
        return false;
    }
    return true;
}

static oserr_t
__MapOriginalBuffer(
        _In_ uuid_t       shmID,
        _In_ unsigned int access,
        _In_ size_t       offset,
        _In_ SHMHandle_t* handle)
{
    oserr_t oserr;

    oserr = SHMAttach(shmID, handle);
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = SHMMap(
            handle,
            offset,
            handle->Capacity,
            access
    );
    if (oserr != OS_EOK) {
        DestroyHandle(shmID);
    }
    return oserr;
}

static oserr_t
__CopySGToBuffer(
        _In_ SHMHandle_t* handle,
        _In_ SHMSG_t*     sg,
        _In_ int          sgCount,
        _In_ size_t       sgOffset)
{
    MemorySpace_t* memorySpace = GetCurrentMemorySpace();
    size_t         currentOffset = 0;
    size_t         bufferOffset = 0;
    size_t         pageSize = GetMemorySpacePageSize();
    int            i = 0;
    oserr_t        oserr;

    while (bufferOffset < handle->Length && i < sgCount) {
        vaddr_t sgMapping;
        size_t  sgMappingOffset = sg[i].Address & (pageSize - 1);

        // make sure we are at the right sg entry, spool ahead until
        // we reach the SG entry that contains our start
        if (currentOffset + sg[i].Length <= sgOffset) {
            // move offset and increase the sg index
            currentOffset += sg[i++].Length;
            continue;
        }

        // Did we encounter a NULL scatter-gather entry in the spot
        // we are copying data to? Then abort the operation
        if (sg[i].Address == 0) {
            return OS_EBUFFER;
        }

        // adjust for a partial copy
        if (currentOffset < sgOffset) {
            sgMappingOffset += sgOffset - currentOffset;
        }

        // map the SG in to kernel
        oserr = MemorySpaceMap(
                memorySpace,
                &(struct MemorySpaceMapOptions) {
                    .PhysicalStart = sg[i].Address,
                    .Length = sg[i].Length,
                    .Flags = MAPPING_COMMIT | MAPPING_PERSISTENT | MAPPING_READONLY,
                    .PlacementFlags = MAPPING_PHYSICAL_CONTIGUOUS | MAPPING_VIRTUAL_GLOBAL
                },
                &sgMapping
        );
        if (oserr != OS_EOK) {
            return oserr;
        }

        memcpy(
                (void*)((uint8_t*)handle->Buffer + bufferOffset),
                (const void*)(sgMapping + sgMappingOffset),
                MIN(sg[i].Length, handle->Length - bufferOffset)
        );

        oserr = MemorySpaceUnmap(memorySpace, sgMapping, sg[i].Length);
        if (oserr != OS_EOK) {
            return oserr;
        }

        currentOffset += sg[i].Length;
        bufferOffset += sg[i].Length;
        i++;
    }
    return OS_EOK;
}

static oserr_t
__CopyBufferToSG(
        _In_ void*    buffer,
        _In_ size_t   length,
        _In_ SHMSG_t* sg,
        _In_ int      sgCount,
        _In_ size_t   sgOffset)
{
    MemorySpace_t* memorySpace = GetCurrentMemorySpace();
    size_t         currentOffset = 0;
    size_t         bufferOffset = 0;
    size_t         pageSize = GetMemorySpacePageSize();
    int            i = 0;
    oserr_t        oserr;

    while (bufferOffset < length && i < sgCount) {
        vaddr_t sgMapping;
        size_t  sgMappingOffset = sg[i].Address & (pageSize - 1);

        // make sure we are at the right sg entry, spool ahead until
        // we reach the SG entry that contains our start
        if (currentOffset + sg[i].Length <= sgOffset) {
            // move offset and increase the sg index
            currentOffset += sg[i++].Length;
            continue;
        }

        // Did we encounter a NULL scatter-gather entry in the spot
        // we are copying data to? Then abort the operation
        if (sg[i].Address == 0) {
            return OS_EBUFFER;
        }

        // adjust for a partial copy
        if (currentOffset < sgOffset) {
            sgMappingOffset += sgOffset - currentOffset;
        }

        // map the SG in to kernel
        oserr = MemorySpaceMap(
                memorySpace,
                &(struct MemorySpaceMapOptions) {
                        .PhysicalStart = sg[i].Address,
                        .Length = sg[i].Length,
                        .Flags = MAPPING_COMMIT | MAPPING_PERSISTENT,
                        .PlacementFlags = MAPPING_PHYSICAL_CONTIGUOUS | MAPPING_VIRTUAL_GLOBAL
                },
                &sgMapping
        );
        if (oserr != OS_EOK) {
            return oserr;
        }

        memcpy(
                (void*)(sgMapping + sgMappingOffset),
                (const void*)((uint8_t*)buffer + bufferOffset),
                MIN(sg[i].Length, length - bufferOffset)
        );

        oserr = MemorySpaceUnmap(memorySpace, sgMapping, sg[i].Length);
        if (oserr != OS_EOK) {
            return oserr;
        }

        currentOffset += sg[i].Length;
        bufferOffset += sg[i].Length;
        i++;
    }
    return OS_EOK;
}

static oserr_t
__CopyBufferToSource(
        _In_ void*  buffer,
        _In_ size_t length,
        _In_ uuid_t sourceID,
        _In_ size_t sourceOffset)
{
    struct SHMBuffer* source;
    int               sgCount;
    SHMSG_t*          sg;
    oserr_t           oserr;

    source = LookupHandleOfType(sourceID, HandleTypeSHM);
    if (source == NULL) {
        return OS_ENOENT;
    }

    oserr = __GatherSGList(source, &sgCount, &sg);
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = __CopyBufferToSG(
            buffer,
            length,
            sg,
            sgCount,
            sourceOffset
    );
    kfree(sg);
    return oserr;
}

static oserr_t
__CloneConformBuffer(
        _In_ struct SHMBuffer*       source,
        _In_ enum OSMemoryConformity conformity,
        _In_ unsigned int            flags,
        _In_ size_t                  offset,
        _In_ size_t                  length,
        _In_ SHMSG_t*                sg,
        _In_ int                     sgCount,
        _In_ SHMHandle_t*            handle)
{
    oserr_t oserr;

    oserr = SHMCreate(
            &(SHM_t) {
                    .Flags = SHM_CONFORM | SHM_COMMIT,
                    .Conformity = conformity,
                    .Access = SHM_ACCESS_READ | SHM_ACCESS_WRITE,
                    .Size = length,
            },
            handle
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    // Store information related to the conformity
    handle->SourceID = source->ID;
    handle->SourceFlags = flags;
    handle->Offset = offset;

    // Handle the initial copy flags
    if (flags & SHM_CONFORM_FILL_ON_CREATION) {
        oserr = __CopySGToBuffer(handle, sg, sgCount, offset);
        if (oserr != OS_EOK) {
            DestroyHandle(handle->ID);
            return oserr;
        }
    }
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
SHMConform(
        _In_ uuid_t                  shmID,
        _In_ SHMConformityOptions_t* conformity,
        _In_ unsigned int            flags,
        _In_ unsigned int            access,
        _In_ size_t                  offset,
        _In_ size_t                  length,
        _In_ SHMHandle_t*            handle)
{
    struct SHMBuffer* source;
    int               sgCount;
    SHMSG_t*          sg;
    oserr_t           oserr;
    size_t            correctedLength;

    oserr = AcquireHandleOfType(shmID, HandleTypeSHM, (void**)&source);
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = __GatherSGList(source, &sgCount, &sg);
    if (oserr != OS_EOK) {
        DestroyHandle(shmID);
        return oserr;
    }

    // Verify the buffer conformity
    if (__VerifySGConformity(sg, sgCount, offset, conformity)) {
        DestroyHandle(shmID);
        kfree(sg);
        return __MapOriginalBuffer(
                shmID,
                access,
                offset,
                handle
        );
    }

    correctedLength = __ClampLength(source, offset, length);
    oserr = __CloneConformBuffer(
            source,
            conformity->Conformity,
            flags,
            offset,
            correctedLength,
            sg,
            sgCount,
            handle
    );
    if (oserr != OS_EOK) {
        DestroyHandle(shmID);
    }
    kfree(sg);
    return oserr;
}

oserr_t
SHMAttach(
        _In_ uuid_t       shmID,
        _In_ SHMHandle_t* handle)
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

    __ConstructSHMHandle(
            handle,
            shmID,
            shmBuffer->Length,
            0,
            0,
            NULL
    );
    return OS_EOK;
}

oserr_t
SHMDetach(
        _In_ SHMHandle_t* handle)
{
    if (handle == NULL) {
        return OS_EINVALPARAMS;
    }

    if (handle->Buffer != NULL) {
        oserr_t oserr = SHMUnmap(
                handle,
                (vaddr_t)handle->Buffer,
                handle->Length
        );
        if (oserr != OS_EOK) {
            return oserr;
        }
    }

    // Did the handle refer to a source buffer? Then release our reference
    // on it.
    if (handle->SourceID != UUID_INVALID) {
        DestroyHandle(handle->SourceID);
    }
    return DestroyHandle(handle->ID);
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

        oserr = AllocatePhysicalMemory(
                shmBuffer->PageMask,
                1,
                &shmBuffer->Pages[pageStart + i]
        );
        if (oserr != OS_EOK) {
            MutexUnlock(&shmBuffer->Mutex);
            return oserr;
        }
        TRACE("__EnsurePhysicalPages: allocated page %i=0x%llx", i, shmBuffer->Pages[pageStart + i]);
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
    size_t  pageIndex = (shmBuffer->Offset + handle->Offset) / pageSize;
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
    TRACE("__CreateMapping(length=0x%llx, index=0x%llx, count=0x%llx)", length, pageIndex, pageCount);

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
                // Ensure that the length we are requesting pages for actually cover
                // the asked mapping range. If we are requesting a mapping from 0xFFF0
                // and 32 bytes, we will do a page-crossing, and actually we are requesting
                // the entire first page.
                .Length = (length + (actualOffset % pageSize)),
                .Mask = shmBuffer->PageMask,
                .Flags = __RecalculateFlags(shmBuffer->Flags, flags),
                .PlacementFlags = MAPPING_PHYSICAL_FIXED | MAPPING_VIRTUAL_PROCESS
            },
            (vaddr_t*)mappingOut
    );
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
    TRACE("SHMMap(offset=0x%llx, length=0x%llx, flags=0x%x)", offset, length, flags);

    if (handle == NULL || length == 0) {
        return OS_EINVALPARAMS;
    }

    // Is the handle the primary handle of a cloned conformity buffer? Then do not
    // allow to remap it.
    if (handle->SourceID != UUID_INVALID) {
        return OS_ENOTSUPPORTED;
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
    TRACE("SHMMap: mapping: 0x%p\n", mapping);

    // Increase the mapping with the built-in offset
    // if this was an exported buffer
    if (shmBuffer->Exported) {
        mapping = (void*)((uintptr_t)mapping + shmBuffer->Offset);
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

    // Handle backfilling to source buffer if set.
    if (handle->SourceFlags & SHM_CONFORM_BACKFILL_ON_UNMAP) {
        size_t bufferOffset = address - (vaddr_t)handle->Buffer;

        // This will work as long as no overlapping areas will be freed. Should
        // an overlapping area be freed this will generate #PF exceptions.
        oserr = __CopyBufferToSource(
                (void*)address,
                length,
                handle->SourceID,
                handle->Offset + bufferOffset
        );
        if (oserr != OS_EOK) {
            return oserr;
        }
    }

    oserr = MemorySpaceUnmap(
            GetCurrentMemorySpace(),
            address,
            length
    );
    if (oserr != OS_EOK) {
        // Unmap might return OS_EINCOMPLETE, which means that unmapping
        // has actually taken place, but a full unmap of the original mapping
        // was not done. However, they share a code-path with an error as we want
        // to proxy this error code up.
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
    uintptr_t         baseAddress = (uintptr_t)memoryBase;
    uintptr_t         address = (uintptr_t)memory;
    size_t            clampedLength;
    int               i;
    TRACE("SHMCommit(memoryBase=0x%llx, memory=0x%llx, length=0x%llx)",
          memoryBase, memory, length);

    shmBuffer = LookupHandleOfType(handle, HandleTypeSHM);
    if (!shmBuffer) {
        return OS_ENOENT;
    }

    // Align the address to a page-boundary
    address &= ~(pageSize - 1);

    // Clamp the length
    i = (int)((address - baseAddress) / pageSize);
    clampedLength = __ClampLength(shmBuffer, i * pageSize, length);

    // Calculate the start index/offset for the memory mappings
    oserr = __EnsurePhysicalPages(shmBuffer, i, (int)(clampedLength / pageSize));
    if (oserr != OS_EOK) {
        ERROR("SHMCommit failed to allocate pages: %u", oserr);
        return oserr;
    }

    // Commit the actual mappings, it's important to note here that we will never
    // be taking care of the IPC kernel mapping here. IPC mappings are *always* comitted
    // immediately, and never imported. It's actually a bit backwards that we use SHM mappings
    // as a part of the IPC implementation.
    oserr = MemorySpaceCommit(
            GetCurrentMemorySpace(),
            address,
            &shmBuffer->Pages[i],
            clampedLength,
            shmBuffer->PageMask,
            MAPPING_PHYSICAL_FIXED
    );
    if (oserr != OS_EOK) {
        ERROR("SHMCommit failed to commit mapping: %u", oserr);
    }

    // TODO: support cleaning of pages allocated.

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
        sgOut[0].Address += shmBuffer->Offset;
        sgOut[0].Length  -= shmBuffer->Offset;
    }
}

oserr_t
SHMBuildSG(
        _In_  uuid_t   handle,
        _Out_ int*     sgCountOut,
        _Out_ SHMSG_t* sgOut)
{
    struct SHMBuffer* shmBuffer;

    if (!sgCountOut) {
        return OS_EINVALPARAMS;
    }

    shmBuffer = LookupHandleOfType(handle, HandleTypeSHM);
    if (!shmBuffer) {
        return OS_ENOENT;
    }

    __BuildSG(shmBuffer, sgCountOut, sgOut);
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
