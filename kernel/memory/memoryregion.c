/**
 * MollenOS
 *
 * Copyright 2018, Philip Meulengracht
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
 *
 *
 * Memory Region Interface
 * - Implementation of shared memory buffers between kernel and processes. This
 *   can be used for conveniently transfering memory.
 */

#define __MODULE "memory_region"
//#define __TRACE

#include <arch/utils.h>
#include <assert.h>
#include <ddk/io.h>
#include <debug.h>
#include <handle.h>
#include <heap.h>
#include <os/types/dma.h>
#include <memoryspace.h>
#include <threading.h>

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

static oserr_t __CreateUserMapping(
        _In_ MemoryRegion_t* region,
        _In_ MemorySpace_t*  memorySpace,
        _In_ uintptr_t*      allocatedMapping,
        _In_ unsigned int    accessFlags)
{
    // This is more tricky, for the calling process we must make a new
    // mapping that spans the entire Capacity, but is uncommitted, and then commit
    // the Length of it.
    unsigned int requiredFlags = MAPPING_USERSPACE | MAPPING_PERSISTENT;
    oserr_t   status = MemorySpaceMapReserved(
            memorySpace,
            (vaddr_t*)allocatedMapping, region->Capacity,
            requiredFlags | region->Flags | accessFlags,
            MAPPING_VIRTUAL_PROCESS);
    if (status != OS_EOK) {
        return status;
    }
    
    // Now commit <Length> in pages, reuse the mappings from the kernel
    if (region->Length) {
        vaddr_t address = *allocatedMapping;
        for (int i = 0; i < region->PageCount; i++, address += GetMemorySpacePageSize()) {
            if (region->Pages[i]) {
                status = MemorySpaceCommit(memorySpace, address,
                                           &region->Pages[i],
                                           GetMemorySpacePageSize(),
                                           region->PageMask,
                                           MAPPING_PHYSICAL_FIXED);
            }
        }
    }
    return status;
}

static oserr_t __CreateKernelMapping(
        _In_ MemoryRegion_t* region,
        _In_ MemorySpace_t*  memorySpace)
{
    oserr_t status = MemorySpaceMapReserved(
            memorySpace,
            (vaddr_t*)&region->KernelMapping, region->Capacity,
            region->Flags, MAPPING_VIRTUAL_GLOBAL);
    if (status != OS_EOK) {
        return status;
    }

    if (region->Length) {
        status = MemorySpaceCommit(
                memorySpace, (vaddr_t)region->KernelMapping,
                &region->Pages[0], region->Length,
                region->PageMask, 0);
    }
    return status;
}

static oserr_t __CreateKernelMappingFromExisting(
        _In_ MemoryRegion_t* region,
        _In_ MemorySpace_t*  memorySpace,
        _In_ uintptr_t       userAddress)
{
    oserr_t status = GetMemorySpaceMapping(
            memorySpace, (uintptr_t)userAddress,
            region->PageCount, &region->Pages[0]);
    if (status != OS_EOK) {
        return status;
    }

    status = MemorySpaceMap(
            memorySpace,
            (uintptr_t*)&region->KernelMapping,
            &region->Pages[0],
            region->Length,
            region->PageMask,
            region->Flags,
            MAPPING_VIRTUAL_GLOBAL
    );
    return status;
}

static void
MemoryRegionDestroy(
    _In_ void* resource)
{
    MemoryRegion_t* region = (MemoryRegion_t*)resource;
    if (region->KernelMapping) {
        MemorySpaceUnmap(GetCurrentMemorySpace(), region->KernelMapping, region->Capacity);
    }
    kfree(region);
}

oserr_t
MemoryRegionCreate(
        _In_  size_t       length,
        _In_  size_t       capacity,
        _In_  unsigned int flags,
        _In_  size_t       pageMask,
        _Out_ void**       kernelMapping,
        _Out_ void**       userMapping,
        _Out_ uuid_t*      handleOut)
{
    MemoryRegion_t* memoryRegion;
    oserr_t         osStatus;
    int             pageCount;

    // Capacity is the expected maximum size of the region. Regions
    // are resizable, but to ensure that enough continious space is
    // allocated we must do it like this. Otherwise, one must create a new.
    pageCount    = DIVUP(capacity, GetMemorySpacePageSize());
    memoryRegion = (MemoryRegion_t*)kmalloc(
        sizeof(MemoryRegion_t) + (sizeof(uintptr_t) * pageCount));
    if (!memoryRegion) {
        return OS_EOOM;
    }
    
    memset(memoryRegion, 0, sizeof(MemoryRegion_t) + (sizeof(uintptr_t) * pageCount));
    MutexConstruct(&memoryRegion->SyncObject, MUTEX_FLAG_PLAIN);
    memoryRegion->Flags     = flags;
    memoryRegion->Length    = length;
    memoryRegion->Capacity  = capacity;
    memoryRegion->PageCount = pageCount;
    memoryRegion->PageMask  = pageMask;

    osStatus = __CreateKernelMapping(memoryRegion, GetCurrentMemorySpace());
    if (osStatus != OS_EOK) {
        ERROR("[shared_region] [create] __CreateKernelMapping failed with %u", osStatus);
        goto ErrorHandler;
    }

    osStatus = __CreateUserMapping(memoryRegion, GetCurrentMemorySpace(), (uintptr_t*) userMapping, 0);
    if (osStatus != OS_EOK) {
        ERROR("[shared_region] [create] __CreateUserMapping failed with %u", osStatus);
        goto ErrorHandler;
    }
    
    *kernelMapping = (void*)memoryRegion->KernelMapping;
    *handleOut     = CreateHandle(
            HandleTypeMemoryRegion,
            MemoryRegionDestroy,
            memoryRegion
    );
    return osStatus;
    
ErrorHandler:
    MemoryRegionDestroy(memoryRegion);
    return osStatus;
}

oserr_t
MemoryRegionCreateExisting(
        _In_  void*        memory,
        _In_  size_t       size,
        _In_  unsigned int flags,
        _Out_ uuid_t*      handleOut)
{
    MemoryRegion_t* region;
    oserr_t         osStatus;
    int             pageCount;
    size_t          capacityWithOffset;

    if (!memory || !size || !handleOut) {
        return OS_EINVALPARAMS;
    }

    // Capacity is the expected maximum size of the region. Regions
    // are resizable, but to ensure that enough continious space is
    // allocated we must do it like this. Otherwise, one must create a new.
    capacityWithOffset = size + ((uintptr_t)memory % GetMemorySpacePageSize());
    pageCount          = DIVUP(capacityWithOffset, GetMemorySpacePageSize());

    region = (MemoryRegion_t*)kmalloc(sizeof(MemoryRegion_t) + (sizeof(uintptr_t) * pageCount));
    if (!region) {
        return OS_EOOM;
    }
    
    memset(region, 0, sizeof(MemoryRegion_t) + (sizeof(uintptr_t) * pageCount));
    MutexConstruct(&region->SyncObject, MUTEX_FLAG_PLAIN);
    region->Flags     = flags;
    region->Length    = capacityWithOffset;
    region->Capacity  = capacityWithOffset;
    region->PageCount = pageCount;
    region->PageMask  = __MASK;    // not relevant on exported regions

    osStatus = __CreateKernelMappingFromExisting(region, GetCurrentMemorySpace(), (uintptr_t) memory);
    if (osStatus != OS_EOK) {
        ERROR("[shared_region] [create_existing] __CreateKernelMappingFromExisting failed with %u", osStatus);
        goto ErrorHandler;
    }
    
    *handleOut = CreateHandle(HandleTypeMemoryRegion, MemoryRegionDestroy, region);
    return osStatus;
    
ErrorHandler:
    MemoryRegionDestroy(region);
    return osStatus;
}

oserr_t
MemoryRegionAttach(
        _In_  uuid_t  Handle,
        _Out_ size_t* Length)
{
    MemoryRegion_t* region;
    oserr_t         oserr;

    oserr = AcquireHandleOfType(
            Handle,
            HandleTypeMemoryRegion,
            (void**)&region
    );
    if (oserr != OS_EOK) {
        ERROR("MemoryRegionAttach handle %u was invalid", Handle);
        return OS_ENOENT;
    }
    
    *Length = region->Length;
    return OS_EOK;
}

oserr_t
MemoryRegionInherit(
        _In_  uuid_t       regionHandle,
        _Out_ void**       memoryOut,
        _Out_ size_t*      sizeOut,
        _In_  unsigned int accessFlags)
{
    MemoryRegion_t* region;
    oserr_t      osStatus;
    uintptr_t       offset;
    uintptr_t       address;
    size_t          size;
    TRACE("MemoryRegionInherit(0x%x)", regionHandle);
    
    if (!memoryOut) {
        return OS_EINVALPARAMS;
    }

    region = (MemoryRegion_t*)LookupHandleOfType(regionHandle, HandleTypeMemoryRegion);
    if (!region) {
        return OS_ENOENT;
    }
    
    MutexLock(&region->SyncObject);
    // This is more tricky, for the calling process we must make a new
    // mapping that spans the entire Capacity, but is uncommitted, and then commit
    // the Length of it.
    offset   = (region->Pages[0] % GetMemorySpacePageSize());
    size     = region->Length;
    osStatus = __CreateUserMapping(region, GetCurrentMemorySpace(), &address, accessFlags);
    MutexUnlock(&region->SyncObject);
    
    if (osStatus != OS_EOK) {
        ERROR("[shared_region] [create] __CreateUserMapping failed with %u", osStatus);
        return osStatus;
    }
    
    *memoryOut = (void*)(address + offset);
    *sizeOut   = size;
    return osStatus;
}

oserr_t
MemoryRegionUnherit(
        _In_ uuid_t handle,
        _In_ void*  memory)
{
    MemoryRegion_t* memoryRegion;
    uintptr_t       address;
    uintptr_t       offset;
    TRACE("MemoryRegionUnherit(0x%x)", handle);
    
    if (!memory) {
        return OS_EINVALPARAMS;
    }

    memoryRegion = (MemoryRegion_t*)LookupHandleOfType(handle, HandleTypeMemoryRegion);
    if (!memoryRegion) {
        return OS_ENOENT;
    }

    address = (uintptr_t)memory;
    offset  = address % GetMemorySpacePageSize();
    address -= offset;
    
    TRACE("... free vmem mappings of length 0x%x", LODWORD(memoryRegion->Capacity));
    return MemorySpaceUnmap(GetCurrentMemorySpace(), address, memoryRegion->Capacity);
}

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
    memoryRegion = LookupHandleOfType(handle, HandleTypeMemoryRegion);
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
    memoryRegion = LookupHandleOfType(handle, HandleTypeMemoryRegion);
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

oserr_t
MemoryRegionCommit(
        _In_ uuid_t handle,
        _In_ void*  memoryBase,
        _In_ void*  memory,
        _In_ size_t length)
{
    MemoryRegion_t* memoryRegion;
    oserr_t      osStatus;
    uintptr_t       userAddress = (uintptr_t)memory;
    uintptr_t       kernelAddress;
    int             limit;
    int             i;

    // Lookup region
    memoryRegion = LookupHandleOfType(handle, HandleTypeMemoryRegion);
    if (!memoryRegion) {
        return OS_ENOENT;
    }

    i     = DIVUP((uintptr_t)memoryBase - userAddress, GetMemorySpacePageSize());
    limit = i + (int)(DIVUP(length, GetMemorySpacePageSize()));
    kernelAddress = memoryRegion->KernelMapping;

    MutexLock(&memoryRegion->SyncObject);
    for (; i < limit; i++, kernelAddress += GetMemorySpacePageSize(), userAddress += GetMemorySpacePageSize()) {
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
                ERROR("MemoryRegionCommit failed to commit kernel mapping at 0x%" PRIxIN ", i=%i", kernelAddress, i);
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
                ERROR("MemoryRegionCommit failed to commit user mapping at 0x%" PRIxIN ", i=%i", userAddress, i);
                break;
            }
        }
    }

    MutexUnlock(&memoryRegion->SyncObject);
    return osStatus;
}

oserr_t
MemoryRegionRead(
        _In_  uuid_t  Handle,
        _In_  size_t  Offset,
        _In_  void*   Buffer,
        _In_  size_t  Length,
        _Out_ size_t* BytesRead)
{
    MemoryRegion_t* Region;
    size_t          ClampedLength;
    
    if (!Buffer || !Length) {
        return OS_EINVALPARAMS;
    }
    
    Region = (MemoryRegion_t*)LookupHandleOfType(Handle, HandleTypeMemoryRegion);
    if (!Region) {
        return OS_ENOENT;
    }
    
    if (Offset >= Region->Length) {
        return OS_EINVALPARAMS;
    }
    
    ClampedLength = MIN(Region->Length - Offset, Length);
    ReadVolatileMemory((const volatile void*)(Region->KernelMapping + Offset),
        (volatile void*)Buffer, ClampedLength);
    
    *BytesRead = ClampedLength;
    return OS_EOK;
}

oserr_t
MemoryRegionWrite(
        _In_  uuid_t      Handle,
        _In_  size_t      Offset,
        _In_  const void* Buffer,
        _In_  size_t      Length,
        _Out_ size_t*     BytesWritten)
{
    MemoryRegion_t* Region;
    size_t          ClampedLength;
    
    if (!Buffer || !Length) {
        return OS_EINVALPARAMS;
    }
    
    Region = (MemoryRegion_t*)LookupHandleOfType(Handle, HandleTypeMemoryRegion);
    if (!Region) {
        return OS_ENOENT;
    }
    
    if (Offset >= Region->Length) {
        return OS_EINVALPARAMS;
    }
    
    ClampedLength = MIN(Region->Length - Offset, Length);
    WriteVolatileMemory((volatile void*)(Region->KernelMapping + Offset),
        (void*)Buffer, ClampedLength);
    
    *BytesWritten = ClampedLength;
    return OS_EOK;
}

#define SG_IS_SAME_REGION(memory_region, idx, idx2, pageSize) \
    (((memory_region)->Pages[idx] + (pageSize) == (memory_region)->Pages[idx2]) || \
     ((memory_region)->Pages[idx] == 0 && (memory_region)->Pages[idx2] == 0))

oserr_t
MemoryRegionGetSg(
        _In_  uuid_t   handle,
        _Out_ int*     sgCountOut,
        _Out_ DMASG_t* sgListOut)
{
    MemoryRegion_t* memoryRegion;
    size_t          pageSize = GetMemorySpacePageSize();
    
    if (!sgCountOut) {
        return OS_EINVALPARAMS;
    }

    memoryRegion = (MemoryRegion_t*)LookupHandleOfType(handle, HandleTypeMemoryRegion);
    if (!memoryRegion) {
        return OS_ENOENT;
    }
    
    // Requested count of the scatter-gather units, so count
    // how many entries it would take to fill a list
    // Assume that if both pointers are supplied we are trying to fill
    // the list with the requested amount, and thus skip this step.
    if (!sgListOut) {
        int sgCount = 0;
        for (int i = 0; i < memoryRegion->PageCount; i++) {
            if (i == 0 || !SG_IS_SAME_REGION(memoryRegion, i - 1, i, pageSize)) {
                sgCount++;
            }
        }
        *sgCountOut = sgCount;
    }
    
    // In order to get the list both counters must be filled
    if (sgListOut) {
        int sgCount = *sgCountOut;
        for (int i = 0, j = 0; (i < sgCount) && (j < memoryRegion->PageCount); i++) {
            DMASG_t* sg = &sgListOut[i];

            sg->address = memoryRegion->Pages[j++];
            sg->length  = pageSize;
            
            while ((j < memoryRegion->PageCount) && SG_IS_SAME_REGION(memoryRegion, j - 1, j, pageSize)) {
                sg->length += pageSize;
                j++;
            }
        }
        
        // Adjust the initial sg entry for offset
        sgListOut[0].length -= sgListOut[0].address % pageSize;
    }
    return OS_EOK;
}

oserr_t
MemoryRegionGetKernelMapping(
        _In_  uuid_t handle,
        _Out_ void** bufferOut)
{
    MemoryRegion_t* region;

    if (!bufferOut) {
        return OS_EINVALPARAMS;
    }

    region = (MemoryRegion_t*)LookupHandleOfType(handle, HandleTypeMemoryRegion);
    if (!region) {
        return OS_ENOENT;
    }

    *bufferOut = (void*)region->KernelMapping;
    return OS_EOK;
}
