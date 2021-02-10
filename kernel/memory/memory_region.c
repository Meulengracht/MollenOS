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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
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
#include <os/dmabuf.h>
#include <memoryspace.h>
#include <string.h>
#include <threading.h>

typedef struct MemoryRegion {
    Mutex_t      SyncObject;
    uintptr_t    KernelMapping;
    size_t       Length;
    size_t       Capacity;
    unsigned int Flags;
    int          PageCount;
    uintptr_t    Pages[];
} MemoryRegion_t;

static OsStatus_t
CreateUserMapping(
        _In_ MemoryRegion_t* region,
        _In_ MemorySpace_t*  memorySpace,
        _In_ uintptr_t*      allocatedMapping,
        _In_ unsigned int    accessFlags)
{
    // This is more tricky, for the calling process we must make a new
    // mapping that spans the entire Capacity, but is uncommitted, and then commit
    // the Length of it.
    unsigned int requiredFlags = MAPPING_USERSPACE | MAPPING_PERSISTENT;
    OsStatus_t   status = MemorySpaceMapReserved(
            memorySpace,
            (vaddr_t*)allocatedMapping, region->Capacity,
            requiredFlags | region->Flags | accessFlags,
            MAPPING_VIRTUAL_PROCESS);
    if (status != OsSuccess) {
        return status;
    }
    
    // Now commit <Length> in pages, reuse the mappings from the kernel
    status = MemorySpaceCommit(
            memorySpace,
            (vaddr_t)*allocatedMapping,
            &region->Pages[0],
            region->Length,
            MAPPING_PHYSICAL_FIXED);
    return status;
}

static OsStatus_t
CreateKernelMapping(
        _In_ MemoryRegion_t* region,
        _In_ MemorySpace_t*  memorySpace)
{
    OsStatus_t status = MemorySpaceMapReserved(
            memorySpace,
            (vaddr_t*)&region->KernelMapping, region->Capacity,
            region->Flags, MAPPING_VIRTUAL_GLOBAL);
    if (status != OsSuccess) {
        return status;
    }
    
    // Now commit <Length> in pages, reuse the mappings from the kernel
    status = MemorySpaceCommit(
            memorySpace, (vaddr_t)region->KernelMapping,
            &region->Pages[0], region->Length, 0);
    return status;
}

static OsStatus_t
CreateKernelMappingFromExisting(
        _In_ MemoryRegion_t* region,
        _In_ MemorySpace_t*  memorySpace,
        _In_ uintptr_t       userAddress)
{
    OsStatus_t status = GetMemorySpaceMapping(
            memorySpace, (uintptr_t)userAddress,
            region->PageCount, &region->Pages[0]);
    if (status != OsSuccess) {
        return status;
    }

    status = MemorySpaceMap(
            memorySpace, (uintptr_t*)&region->KernelMapping,
            &region->Pages[0], region->Length, region->Flags, MAPPING_VIRTUAL_GLOBAL);
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

OsStatus_t
MemoryRegionCreate(
    _In_  size_t  Length,
    _In_  size_t  Capacity,
    _In_  unsigned int Flags,
    _Out_ void**  KernelMapping,
    _Out_ void**  UserMapping,
    _Out_ UUId_t* Handle)
{
    MemoryRegion_t* Region;
    OsStatus_t      Status;
    int             PageCount;

    // Capacity is the expected maximum size of the region. Regions
    // are resizable, but to ensure that enough continious space is
    // allocated we must do it like this. Otherwise one must create a new.
    PageCount = DIVUP(Capacity, GetMemorySpacePageSize());
    Region    = (MemoryRegion_t*)kmalloc(
        sizeof(MemoryRegion_t) + (sizeof(uintptr_t) * PageCount));
    if (!Region) {
        return OsOutOfMemory;
    }
    
    memset(Region, 0, sizeof(MemoryRegion_t) + (sizeof(uintptr_t) * PageCount));
    MutexConstruct(&Region->SyncObject, MUTEX_FLAG_PLAIN);
    Region->Flags     = Flags;
    Region->Length    = Length;
    Region->Capacity  = Capacity;
    Region->PageCount = PageCount;
    
    Status = CreateKernelMapping(Region, GetCurrentMemorySpace());
    if (Status != OsSuccess) {
        ERROR("[shared_region] [create] CreateKernelMapping failed with %u", Status);
        goto ErrorHandler;
    }
    
    Status = CreateUserMapping(Region, GetCurrentMemorySpace(), (uintptr_t*) UserMapping, 0);
    if (Status != OsSuccess) {
        ERROR("[shared_region] [create] CreateUserMapping failed with %u", Status);
        goto ErrorHandler;
    }
    
    *KernelMapping = (void*)Region->KernelMapping;
    *Handle        = CreateHandle(HandleTypeMemoryRegion, MemoryRegionDestroy, Region);
    return Status;
    
ErrorHandler:
    MemoryRegionDestroy(Region);
    return Status;
}

OsStatus_t
MemoryRegionCreateExisting(
    _In_  void*        memory,
    _In_  size_t       size,
    _In_  unsigned int flags,
    _Out_ UUId_t*      handleOut)
{
    MemoryRegion_t* region;
    OsStatus_t      osStatus;
    int             pageCount;
    size_t          capacityWithOffset;

    // Capacity is the expected maximum size of the region. Regions
    // are resizable, but to ensure that enough continious space is
    // allocated we must do it like this. Otherwise one must create a new.
    capacityWithOffset = size + ((uintptr_t)memory % GetMemorySpacePageSize());
    pageCount          = DIVUP(capacityWithOffset, GetMemorySpacePageSize());

    region = (MemoryRegion_t*)kmalloc(sizeof(MemoryRegion_t) + (sizeof(uintptr_t) * pageCount));
    if (!region) {
        return OsOutOfMemory;
    }
    
    memset(region, 0, sizeof(MemoryRegion_t) + (sizeof(uintptr_t) * pageCount));
    MutexConstruct(&region->SyncObject, MUTEX_FLAG_PLAIN);
    region->Flags     = flags;
    region->Length    = capacityWithOffset;
    region->Capacity  = capacityWithOffset;
    region->PageCount = pageCount;

    osStatus = CreateKernelMappingFromExisting(region, GetCurrentMemorySpace(), (uintptr_t)memory);
    if (osStatus != OsSuccess) {
        ERROR("[shared_region] [create_existing] CreateKernelMappingFromExisting failed with %u", osStatus);
        goto ErrorHandler;
    }
    
    *handleOut = CreateHandle(HandleTypeMemoryRegion, MemoryRegionDestroy, region);
    return osStatus;
    
ErrorHandler:
    MemoryRegionDestroy(region);
    return osStatus;
}


OsStatus_t
MemoryRegionAttach(
    _In_  UUId_t  Handle,
    _Out_ size_t* Length)
{
    MemoryRegion_t* Region;
    
    if (AcquireHandle(Handle, (void**)&Region) != OsSuccess) {
        ERROR("[sc_dma_attach] [acquire_handle] invalid handle %u", Handle);
        return OsDoesNotExist;
    }
    
    *Length = Region->Length;
    return OsSuccess;
}

OsStatus_t
MemoryRegionInherit(
        _In_  UUId_t       regionHandle,
        _Out_ void**       memoryOut,
        _Out_ size_t*      size,
        _In_  unsigned int accessFlags)
{
    MemoryRegion_t* region;
    OsStatus_t      osStatus;
    uintptr_t       offset;
    uintptr_t       address;
    TRACE("MemoryRegionInherit(0x%x)", regionHandle);
    
    if (!memoryOut) {
        return OsInvalidParameters;
    }

    region = (MemoryRegion_t*)LookupHandleOfType(regionHandle, HandleTypeMemoryRegion);
    if (!region) {
        return OsDoesNotExist;
    }
    
    MutexLock(&region->SyncObject);
    // This is more tricky, for the calling process we must make a new
    // mapping that spans the entire Capacity, but is uncommitted, and then commit
    // the Length of it.
    offset   = (region->Pages[0] % GetMemorySpacePageSize());
    osStatus = CreateUserMapping(region, GetCurrentMemorySpace(), &address, accessFlags);
    MutexUnlock(&region->SyncObject);
    
    if (osStatus != OsSuccess) {
        ERROR("[shared_region] [create] CreateUserMapping failed with %u", osStatus);
        return osStatus;
    }
    
    *memoryOut = (void*)(address + offset);
    *size      = region->Length; // OBS: unsafe access to length
    return osStatus;
}

OsStatus_t
MemoryRegionUnherit(
    _In_ UUId_t Handle,
    _In_ void*  Memory)
{
    MemoryRegion_t* Region;
    uintptr_t       Address;
    uintptr_t       Offset;
    TRACE("MemoryRegionUnherit(0x%x)", Handle);
    
    if (!Memory) {
        return OsInvalidParameters;
    }
    
    Region = (MemoryRegion_t*)LookupHandleOfType(Handle, HandleTypeMemoryRegion);
    if (!Region) {
        return OsDoesNotExist;
    }
    
    Address  = (uintptr_t)Memory;
    Offset   = Address % GetMemorySpacePageSize();
    Address -= Offset;
    
    TRACE("... free vmem mappings of length 0x%x", LODWORD(Region->Capacity));
    return MemorySpaceUnmap(GetCurrentMemorySpace(), Address, Region->Capacity);
}

OsStatus_t
MemoryRegionResize(
    _In_ UUId_t Handle,
    _In_ void*  Memory,
    _In_ size_t NewLength)
{
    MemoryRegion_t* Region;
    int             CurrentPages;
    int             NewPages;
    uintptr_t       End;
    OsStatus_t      Status;
    
    // Lookup region
    Region = LookupHandleOfType(Handle, HandleTypeMemoryRegion);
    if (!Region) {
        return OsDoesNotExist;
    }
    
    // Verify that the new length is not exceeding capacity
    if (NewLength > Region->Capacity) {
        return OsInvalidParameters;
    }
    
    MutexLock(&Region->SyncObject);
    CurrentPages = DIVUP(Region->Length, GetMemorySpacePageSize());
    NewPages     = DIVUP(NewLength, GetMemorySpacePageSize());
    
    // If we are shrinking (not supported atm) or equal then simply move on
    // and report success. We won't perform any unmapping
    if (CurrentPages >= NewPages) {
        MutexUnlock(&Region->SyncObject);
        return OsNotSupported;
    }
    
    // Start by resizing kernel mappings
    End    = Region->KernelMapping + (CurrentPages * GetMemorySpacePageSize());
    Status = MemorySpaceCommit(GetCurrentMemorySpace(), End,
        &Region->Pages[CurrentPages], NewLength - Region->Length, 0);
    if (Status != OsSuccess) {
        MutexUnlock(&Region->SyncObject);
        return Status;
    }
    
    // Calculate from where we should start committing new pages
    End    = (uintptr_t)Memory + (CurrentPages * GetMemorySpacePageSize());
    Status = MemorySpaceCommit(GetCurrentMemorySpace(), End,
        &Region->Pages[CurrentPages], NewLength - Region->Length, 
        MAPPING_PHYSICAL_FIXED);
    if (Status == OsSuccess) {
        Region->Length = NewLength;
    }
    MutexUnlock(&Region->SyncObject);
    return Status;
}

OsStatus_t
MemoryRegionRefresh(
    _In_  UUId_t  Handle,
    _In_  void*   Memory,
    _In_  size_t  CurrentLength,
    _Out_ size_t* NewLength)
{
    MemoryRegion_t* Region;
    int             CurrentPages;
    int             NewPages;
    uintptr_t       End;
    OsStatus_t      Status;
    
    // Lookup region
    Region = LookupHandleOfType(Handle, HandleTypeMemoryRegion);
    if (!Region) {
        return OsDoesNotExist;
    }
    
    MutexLock(&Region->SyncObject);
    
    // Update the out first
    *NewLength = Region->Length;
    
    // Calculate the new number of pages that should be mapped,
    // but instead of using the provided argument as new, it must be
    // the previous
    CurrentPages = DIVUP(CurrentLength, GetMemorySpacePageSize());
    NewPages     = DIVUP(Region->Length, GetMemorySpacePageSize());
    
    // If we are shrinking (not supported atm) or equal then simply move on
    // and report success. We won't perform any unmapping
    if (CurrentPages >= NewPages) {
        MutexUnlock(&Region->SyncObject);
        return OsSuccess;
    }
    
    // Otherwise commit mappings, but instead of doing like the Resize
    // operation we will tell that we provide them ourself
    End = (uintptr_t)Memory + (CurrentPages * GetMemorySpacePageSize());
    Status = MemorySpaceCommit(GetCurrentMemorySpace(), End, 
        &Region->Pages[CurrentPages], Region->Length - CurrentLength,
        MAPPING_PHYSICAL_FIXED);
    MutexUnlock(&Region->SyncObject);
    return Status;
}

OsStatus_t
MemoryRegionRead(
    _In_  UUId_t  Handle,
    _In_  size_t  Offset,
    _In_  void*   Buffer,
    _In_  size_t  Length,
    _Out_ size_t* BytesRead)
{
    MemoryRegion_t* Region;
    size_t          ClampedLength;
    
    if (!Buffer || !Length) {
        return OsInvalidParameters;
    }
    
    Region = (MemoryRegion_t*)LookupHandleOfType(Handle, HandleTypeMemoryRegion);
    if (!Region) {
        return OsDoesNotExist;
    }
    
    if (Offset >= Region->Length) {
        return OsInvalidParameters;
    }
    
    ClampedLength = MIN(Region->Length - Offset, Length);
    ReadVolatileMemory((const volatile void*)(Region->KernelMapping + Offset),
        (volatile void*)Buffer, ClampedLength);
    
    *BytesRead = ClampedLength;
    return OsSuccess;
}

OsStatus_t
MemoryRegionWrite(
    _In_  UUId_t      Handle,
    _In_  size_t      Offset,
    _In_  const void* Buffer,
    _In_  size_t      Length,
    _Out_ size_t*     BytesWritten)
{
    MemoryRegion_t* Region;
    size_t          ClampedLength;
    
    if (!Buffer || !Length) {
        return OsInvalidParameters;
    }
    
    Region = (MemoryRegion_t*)LookupHandleOfType(Handle, HandleTypeMemoryRegion);
    if (!Region) {
        return OsDoesNotExist;
    }
    
    if (Offset >= Region->Length) {
        return OsInvalidParameters;
    }
    
    ClampedLength = MIN(Region->Length - Offset, Length);
    WriteVolatileMemory((volatile void*)(Region->KernelMapping + Offset),
        (void*)Buffer, ClampedLength);
    
    *BytesWritten = ClampedLength;
    return OsSuccess;
}

OsStatus_t
MemoryRegionGetSg(
    _In_  UUId_t         Handle,
    _Out_ int*           SgCountOut,
    _Out_ struct dma_sg* SgListOut)
{
    MemoryRegion_t* Region;
    size_t          PageSize = GetMemorySpacePageSize();
    
    if (!SgCountOut) {
        return OsInvalidParameters;
    }
    
    Region = (MemoryRegion_t*)LookupHandleOfType(Handle, HandleTypeMemoryRegion);
    if (!Region) {
        return OsDoesNotExist;
    }
    
    // Requested count of the scatter-gather units, so count
    // how many entries it would take to fill a list
    // Assume that if both pointers are supplied we are trying to fill
    // the list with the requested amount, and thus skip this step.
    if (!SgListOut) {
        int SgCount = 0;
        for (int i = 0; i < Region->PageCount; i++) {
            if (i == 0 || (Region->Pages[i - 1] + PageSize) != Region->Pages[i]) {
                SgCount++;
            }
        }
        *SgCountOut = SgCount;
    }
    
    // In order to get the list both counters must be filled
    if (SgListOut) {
        int SgCount = *SgCountOut;
        for (int i = 0, j = 0; (i < SgCount) && (j < Region->PageCount); i++) {
            struct dma_sg* Sg = &SgListOut[i];
            
            Sg->address = Region->Pages[j++];
            Sg->length  = PageSize;
            
            while ((j < Region->PageCount) &&
                   (Region->Pages[j - 1] + PageSize) == Region->Pages[j]) {
                Sg->length += PageSize;
                j++;
            }
        }
        
        // Adjust the initial sg entry for offset
        SgListOut[0].length -= SgListOut[0].address % PageSize;
    }
    return OsSuccess;
}

OsStatus_t
MemoryRegionGetKernelMapping(
        _In_  UUId_t handle,
        _Out_ void** bufferOut)
{
    MemoryRegion_t* region;

    if (!bufferOut) {
        return OsInvalidParameters;
    }

    region = (MemoryRegion_t*)LookupHandleOfType(handle, HandleTypeMemoryRegion);
    if (!region) {
        return OsDoesNotExist;
    }

    *bufferOut = (void*)region->KernelMapping;
    return OsSuccess;
}
