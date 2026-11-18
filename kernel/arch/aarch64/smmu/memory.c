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
 *
 * ARM SMMU Control Memory
 * Allocates control pages from a pre-allocated pool and preserves any
 * half-torn-down state if cleanup fails.
 */

#include <debug.h>
#include <heap.h>
#include <machine.h>
#include <memoryspace.h>
#include <string.h>

#include "memory.h"

// A 2 MiB pool holds at most 512 pages, so UINT16_MAX can never be a real
// block page count and safely marks interior pages of multi-page blocks.
#define __INTERIOR_PAGE UINT16_MAX

// Records an internal consistency failure and locks the pool in Failed so
// none of its physical memory can ever be reused.
static oserr_t
__Fail(
    _In_ ArmSMMUMemory_t* memory,
    _In_ const char*      function,
    _In_ const char*      operation,
    _In_ oserr_t          status)
{
    ERROR("%s: %s violated arena bookkeeping: %u", function, operation, status);
    memory->State = ArmSMMUMemoryFailed;
    return OS_EUNKNOWN;
}

// Verifies the pool is ready for page allocations or frees.
static oserr_t
__CheckReady(
    _In_ const ArmSMMUMemory_t* memory)
{
    if (memory->State == ArmSMMUMemoryFailed) {
        return OS_ECANCELLED;
    }
    return memory->State == ArmSMMUMemoryReady ? OS_EOK : OS_EBUSY;
}

oserr_t
ArmSMMUMemoryInitialize(
    _In_ ArmSMMUMemory_t* memory,
    _In_ size_t           length,
    _In_ uint64_t         physicalLimit)
{
    SystemMemory_t* systemMemory;
    size_t          stackBytes;
    size_t          storageBytes;
    unsigned int    flags = MAPPING_COMMIT | MAPPING_PERSISTENT;
    oserr_t         status;

    if (memory == NULL || length == 0 || length > ARM_SMMU_MEMORY_MAX_SIZE) {
        return OS_EINVALPARAMS;
    }

    if (memory->State != ArmSMMUMemoryEmpty) {
        return OS_EBUSY;
    }

    // Both the CPU virtual memory layer and the physical allocator must use
    // 4 KiB granules so page counts and offsets line up 1:1.
    systemMemory = &GetMachine()->PhysicalMemory;
    if (GetMemorySpacePageSize() != ARM_SMMU_MEMORY_PAGE_SIZE ||
        systemMemory->BlockSize != ARM_SMMU_MEMORY_PAGE_SIZE) {
        return OS_ENOTSUPPORTED;
    }

    // Store each resource in memory as soon as it is allocated. If a later
    // step fails, ArmSMMUMemoryDestroy cleans up what succeeded so far.
    length = DIVUP(length, ARM_SMMU_MEMORY_PAGE_SIZE) * ARM_SMMU_MEMORY_PAGE_SIZE;
    memory->State = ArmSMMUMemoryPreparing;
    memory->PageCount = length / ARM_SMMU_MEMORY_PAGE_SIZE;

    stackBytes = MemoryStackCalculateSize(memory->PageCount);
    if (stackBytes == 0) {
        return __Fail(memory, "ArmSMMUMemoryInitialize", "stack sizing", OS_ENOTSUPPORTED);
    }

    // Allocate the free-page stack and the BlockPages validation array in one
    // heap block. Stack storage is pointer-aligned, so the uint16_t array
    // immediately after it is naturally aligned.
    storageBytes = stackBytes + memory->PageCount * sizeof(*memory->BlockPages);
    memory->Storage = kmalloc(storageBytes);
    if (memory->Storage == NULL) {
        ERROR("ArmSMMUMemoryInitialize: metadata allocation of %zu bytes failed", storageBytes);
        return OS_EOOM;
    }

    memset(memory->Storage, 0, storageBytes);
    memory->BlockPages = (uint16_t*)((char*)memory->Storage + stackBytes);
    
    status = MemoryStackConstruct(
        &memory->FreePages,
        ARM_SMMU_MEMORY_PAGE_SIZE,
        (uintptr_t)memory->Storage, 
        stackBytes
    );
    if (status != OS_EOK) {
        return __Fail(memory, "ArmSMMUMemoryInitialize", "stack construction", status);
    }

    status = SystemMemoryAllocateDevice(
        systemMemory,
        length,
        ARM_SMMU_MEMORY_PAGE_SIZE,
        0,
        &memory->Allocation
    );
    if (status != OS_EOK) {
        ERROR("ArmSMMUMemoryInitialize: device allocation of %zu bytes failed: %u", length, status);
        return status;
    }

    // Make sure the whole physical block sits within what this SMMU can address.
    if (memory->Allocation.PhysicalBase > physicalLimit ||
        length - 1 > physicalLimit - memory->Allocation.PhysicalBase) {
        ERROR("ArmSMMUMemoryInitialize: physical range exceeds limit 0x%llx",
              (unsigned long long)physicalLimit);
        return OS_ENOTSUPPORTED;
    }

    status = MemoryStackPush(
        &memory->FreePages,
        memory->Allocation.PhysicalBase,
        (int)memory->PageCount
    );
    if (status != OS_EOK) {
        return __Fail(memory, "ArmSMMUMemoryInitialize", "initial free range", status);
    }

    memory->Space = GetDomainMemorySpace();
    if (!memory->Allocation.Cached) {
        flags |= MAPPING_NOCACHE;
    }

    // MAPPING_PERSISTENT tells MemorySpaceUnmap to remove only the CPU virtual
    // mapping and leave the underlying physical pages with Allocation.
    // This gives the kernel a normal read/write view without execute access.
    status = MemorySpaceMap(
        memory->Space,
        &(struct MemorySpaceMapOptions) {
            .PhysicalStart = memory->Allocation.PhysicalBase,
            .Length = length,
            .Flags = flags,
            .PlacementFlags = MAPPING_PHYSICAL_CONTIGUOUS | MAPPING_VIRTUAL_GLOBAL
        },
        &memory->Mapping
    );
    if (status != OS_EOK) {
        ERROR("ArmSMMUMemoryInitialize: CPU mapping failed: %u", status);
        return status;
    }

    memset((void*)memory->Mapping, 0, length);
    memory->State = ArmSMMUMemoryReady;
    return OS_EOK;
}

oserr_t
ArmSMMUMemoryAllocate(
    _In_  ArmSMMUMemory_t*      memory,
    _In_  size_t                length,
    _In_  size_t                alignment,
    _Out_ ArmSMMUMemoryBlock_t* blockOut)
{
    ArmSMMUMemoryBlock_t  block;
    uintptr_t            physical;
    size_t               offset;
    size_t               first;
    size_t               count;
    oserr_t              status;

    if (memory == NULL || blockOut == NULL || length == 0 ||
        length > ARM_SMMU_MEMORY_MAX_SIZE || (alignment != 0 && !IsPowerOfTwo(alignment))) {
        return OS_EINVALPARAMS;
    }

    status = __CheckReady(memory);
    if (status != OS_EOK) {
        return status;
    }

    length = DIVUP(length, ARM_SMMU_MEMORY_PAGE_SIZE) * ARM_SMMU_MEMORY_PAGE_SIZE;
    status = MemoryStackPopContiguous(
        &memory->FreePages,
        length,
        alignment,
        0,
        &physical
    );
    if (status != OS_EOK) {
        if (status == OS_EOOM) {
            return status;
        }
        return __Fail(memory, "ArmSMMUMemoryAllocate", "allocation", status);
    }

    // Sanity-check the address from FreePages so a corrupt stack can never
    // hand out memory outside this pool or overlap an active block.
    if (physical < memory->Allocation.PhysicalBase) {
        return __Fail(memory, "ArmSMMUMemoryAllocate", "allocation base", OS_EUNKNOWN);
    }

    offset = physical - memory->Allocation.PhysicalBase;
    if (offset % ARM_SMMU_MEMORY_PAGE_SIZE != 0 || offset > memory->Allocation.Length ||
        length > memory->Allocation.Length - offset) {
        return __Fail(memory, "ArmSMMUMemoryAllocate", "allocation range", OS_EUNKNOWN);
    }

    first = offset / ARM_SMMU_MEMORY_PAGE_SIZE;
    count = length / ARM_SMMU_MEMORY_PAGE_SIZE;
    for (size_t i = 0; i < count; i++) {
        if (memory->BlockPages[first + i] != 0) {
            return __Fail(memory, "ArmSMMUMemoryAllocate", "allocation overlap", OS_EUNKNOWN);
        }
    }

    block.PhysicalAddress = physical;
    block.VirtualAddress = (void*)(memory->Mapping + offset);
    block.Length = length;
    
    // Always zero reused pages before handing them out so stale page-table
    // entries from an earlier user are never read by hardware.
    memset(block.VirtualAddress, 0, length);
    memory->BlockPages[first] = (uint16_t)count;

    for (size_t i = 1; i < count; i++) {
        memory->BlockPages[first + i] = __INTERIOR_PAGE;
    }
    memory->BlockCount++;

    *blockOut = block;
    return OS_EOK;
}

oserr_t
ArmSMMUMemoryFree(
    _In_    ArmSMMUMemory_t*      memory,
    _InOut_ ArmSMMUMemoryBlock_t* block)
{
    size_t  offset;
    size_t  first;
    size_t  count;
    oserr_t status;

    if (memory == NULL || block == NULL) {
        return OS_EINVALPARAMS;
    }

    status = __CheckReady(memory);
    if (status != OS_EOK) {
        return status;
    }

    if (block->Length == 0 || block->Length % ARM_SMMU_MEMORY_PAGE_SIZE != 0 ||
        block->PhysicalAddress < memory->Allocation.PhysicalBase) {
        return OS_EINVALPARAMS;
    }

    offset = block->PhysicalAddress - memory->Allocation.PhysicalBase;
    if (offset % ARM_SMMU_MEMORY_PAGE_SIZE != 0 || offset >= memory->Allocation.Length ||
        block->Length > memory->Allocation.Length - offset ||
        block->VirtualAddress != (void*)(memory->Mapping + offset)) {
        return OS_EINVALPARAMS;
    }

    first = offset / ARM_SMMU_MEMORY_PAGE_SIZE;
    count = block->Length / ARM_SMMU_MEMORY_PAGE_SIZE;
    if (memory->BlockPages[first] != count) {
        return OS_EINVALPARAMS;
    }

    if (memory->BlockCount == 0) {
        return __Fail(memory, "ArmSMMUMemoryFree", "block count", OS_EUNKNOWN);
    }

    for (size_t i = 1; i < count; i++) {
        if (memory->BlockPages[first + i] != __INTERIOR_PAGE) {
            return __Fail(memory, "ArmSMMUMemoryFree", "block interior", OS_EUNKNOWN);
        }
    }

    // All validation happens before pushing back onto FreePages. Because the
    // stack was sized for the worst case up front, returning a valid block
    // should never run out of tracking space.
    status = MemoryStackPush(
        &memory->FreePages,
        block->PhysicalAddress,
        (int)count
    );
    if (status != OS_EOK) {
        return __Fail(memory, "ArmSMMUMemoryFree", "block return", status);
    }

    memset(&memory->BlockPages[first], 0, count * sizeof(*memory->BlockPages));
    memory->BlockCount--;
    memset(block, 0, sizeof(*block));
    return OS_EOK;
}

oserr_t
ArmSMMUMemoryDestroy(
    _In_ ArmSMMUMemory_t* memory)
{
    oserr_t status;

    if (memory == NULL) {
        return OS_EINVALPARAMS;
    }

    if (memory->State == ArmSMMUMemoryEmpty) {
        return OS_EOK;
    }

    if (memory->State == ArmSMMUMemoryFailed) {
        return OS_ECANCELLED;
    }

    if (memory->BlockCount != 0) {
        return OS_EBUSY;
    }

    // If the pool reached Ready, verify every page made it back to FreePages
    // before tearing down the memory.
    if (memory->State == ArmSMMUMemoryReady) {
        if (MemoryStackFreeBlockCount(&memory->FreePages) != memory->PageCount) {
            return __Fail(memory, "ArmSMMUMemoryDestroy", "final free count", OS_EUNKNOWN);
        }
        for (size_t i = 0; i < memory->PageCount; i++) {
            if (memory->BlockPages[i] != 0) {
                return __Fail(memory, "ArmSMMUMemoryDestroy", "final block table", OS_EUNKNOWN);
            }
        }
    }

    // Hardware is already disconnected. Move to Closing so no allocations can
    // slip in between retries, unmap the CPU view first, and clear Mapping so a
    // retry after a physical-release error will not unmap the same range twice.
    memory->State = ArmSMMUMemoryClosing;
    if (memory->Mapping != 0) {
        status = MemorySpaceUnmap(memory->Space, memory->Mapping, memory->Allocation.Length);
        if (status != OS_EOK) {
            ERROR("ArmSMMUMemoryDestroy: CPU unmap failed: %u", status);
            return status;
        }
        memory->Mapping = 0;
    }

    status = SystemMemoryReleaseAllocation(&memory->Allocation);
    if (status != OS_EOK) {
        ERROR("ArmSMMUMemoryDestroy: device allocation release failed: %u", status);
        return status;
    }

    kfree(memory->Storage);
    memset(memory, 0, sizeof(*memory));
    return OS_EOK;
}
