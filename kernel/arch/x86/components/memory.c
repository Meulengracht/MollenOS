/* MollenOS
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
 * MollenOS Memory Utility Functions
 *   - Implements helpers and utility functions with the MemoryInitialize.
 */
#define __MODULE "PMEM"
#define __TRACE

#include <ds/blbitmap.h>
#include <memory.h>
#include <multiboot.h>
#include <debug.h>
#include <assert.h>
#include <arch.h>
#include <apic.h>

// Global static storage for the memory
static _Atomic(uintptr_t) ReservedMemoryPointer = ATOMIC_VAR_INIT(MEMORY_LOCATION_RESERVED);
static MemorySynchronizationObject_t SyncData   = { SPINLOCK_INIT, 0 };
static size_t BlockmapBytes                     = 0;
extern void memory_invalidate_addr(uintptr_t pda);

/* MmMemoryDebugPrint
 * This is a debug function for inspecting
 * the memory status, it spits out how many blocks are in use */
void
MmMemoryDebugPrint(void) {
    TRACE("Bitmap size: %u Bytes", BlockmapBytes);
    TRACE("Memory in use %u Bytes", GetMachine()->PhysicalMemory.BlocksAllocated * PAGE_SIZE);
    TRACE("Block status %u/%u", GetMachine()->PhysicalMemory.BlocksAllocated, GetMachine()->PhysicalMemory.BlockCount);
}

/* InitializeSystemMemory (@arch)
 * Initializes the entire system memory range, selecting ranges that should
 * be reserved and those that are free for system use. */
OsStatus_t
InitializeSystemMemory(
    _In_ Multiboot_t*       BootInformation,
    _In_ BlockBitmap_t*     Memory,
    _In_ SystemMemoryMap_t* MemoryMap,
    _In_ size_t*            MemoryGranularity,
    _In_ size_t*            NumberOfMemoryBlocks)
{
    // Variables
    BIOSMemoryRegion_t *RegionPointer = NULL;
    uintptr_t MemorySize;
    int i;

    // Initialize
    RegionPointer = (BIOSMemoryRegion_t*)(uintptr_t)BootInformation->MemoryMapAddress;

    // The memory-high part is 64kb blocks 
    // whereas the memory-low part is bytes of memory
    MemorySize  = (BootInformation->MemoryHigh * 64 * 1024);
    MemorySize  += BootInformation->MemoryLow; // This is in kilobytes 
    assert((MemorySize / 1024 / 1024) >= 32);
    ConstructBlockmap(Memory, (void*)MEMORY_LOCATION_BITMAP, 
        BLOCKMAP_ALLRESERVED, 0, MemorySize, PAGE_SIZE);
    BlockmapBytes = GetBytesNeccessaryForBlockmap(0, MemorySize, PAGE_SIZE);

    // Free regions given to us by memory map
    for (i = 0; i < (int)BootInformation->MemoryMapLength; i++) {
        if (RegionPointer->Type == 1) {
            ReleaseBlockmapRegion(Memory, 
                (uintptr_t)RegionPointer->Address, (size_t)RegionPointer->Size);
        }
        RegionPointer++;
    }

    // Mark default regions in use and special regions
    //  0x0000              || Used for catching null-pointers
    //  0x4000 + 0x8000     || Used for memory region & Trampoline-code
    //  0x90000 - 0x9F000   || Kernel Stack
    //  0x100000 - KernelSize
    //  0x200000 - RamDiskSize
    //  0x300000 - ??       || Bitmap Space
    ReserveBlockmapRegion(Memory, 0,                        0x10000);
    ReserveBlockmapRegion(Memory, 0x90000,                  0xF000);
    ReserveBlockmapRegion(Memory, MEMORY_LOCATION_KERNEL,   BootInformation->KernelSize + PAGE_SIZE);
    ReserveBlockmapRegion(Memory, MEMORY_LOCATION_RAMDISK,  BootInformation->RamdiskSize + PAGE_SIZE);
    ReserveBlockmapRegion(Memory, MEMORY_LOCATION_BITMAP,   (BlockmapBytes + PAGE_SIZE));

    // Fill in rest of data
    *MemoryGranularity      = PAGE_SIZE;
    *NumberOfMemoryBlocks   = DIVUP(MemorySize, PAGE_SIZE);
    
    MemoryMap->SystemHeap.Start     = MEMORY_LOCATION_HEAP;
    MemoryMap->SystemHeap.Length    = MEMORY_LOCATION_HEAP_END - MEMORY_LOCATION_HEAP;

    MemoryMap->UserCode.Start       = MEMORY_LOCATION_RING3_CODE;
    MemoryMap->UserCode.Length      = MEMORY_LOCATION_RING3_CODE_END - MEMORY_LOCATION_RING3_CODE;

    MemoryMap->UserHeap.Start       = MEMORY_LOCATION_RING3_HEAP;
    MemoryMap->UserHeap.Length      = MEMORY_LOCATION_RING3_HEAP_END - MEMORY_LOCATION_RING3_HEAP;
    
    MemoryMap->UserIoMemory.Start   = MEMORY_LOCATION_RING3_IOSPACE;
    MemoryMap->UserIoMemory.Length  = MEMORY_LOCATION_RING3_IOSPACE_END - MEMORY_LOCATION_RING3_IOSPACE;

    MemoryMap->ThreadArea.Start     = MEMORY_LOCATION_RING3_THREAD_START;
    MemoryMap->ThreadArea.Length    = MEMORY_LOCATION_RING3_THREAD_END - MEMORY_LOCATION_RING3_THREAD_START;

    // Debug initial stats
    MmMemoryDebugPrint();
    return OsSuccess;
}

/* ConvertSystemSpaceToPaging
 * Converts system memory-space generic flags to native x86 paging flags */
Flags_t
ConvertSystemSpaceToPaging(Flags_t Flags)
{
    // Variables
    Flags_t NativeFlags = PAGE_PRESENT;

    if (Flags & MAPPING_USERSPACE) {
		NativeFlags |= PAGE_USER;
	}
	if (Flags & MAPPING_NOCACHE) {
		NativeFlags |= PAGE_CACHE_DISABLE;
	}
	if (Flags & MAPPING_VIRTUAL) {
		NativeFlags |= PAGE_VIRTUAL;
	}
    if (!(Flags & MAPPING_READONLY)) {
        NativeFlags |= PAGE_WRITE;
    }
    if (Flags & MAPPING_ISDIRTY) {
        NativeFlags |= PAGE_DIRTY;
    }
    return NativeFlags;
}

/* ConvertPagingToSystemSpace
 * Converts native x86 paging flags to system memory-space generic flags */
Flags_t
ConvertPagingToSystemSpace(Flags_t Flags)
{
    // Variables
    Flags_t GenericFlags = 0;

    if (Flags & PAGE_PRESENT) {
        GenericFlags |= MAPPING_EXECUTABLE;
    }
    if (!(Flags & PAGE_WRITE)) {
        GenericFlags |= MAPPING_READONLY;
    }
    if (Flags & PAGE_USER) {
        GenericFlags |= MAPPING_USERSPACE;
    }
    if (Flags & PAGE_CACHE_DISABLE) {
        GenericFlags |= MAPPING_NOCACHE;
    }
    if (Flags & PAGE_VIRTUAL) {
        GenericFlags |= MAPPING_VIRTUAL;
    }
    if (Flags & PAGE_DIRTY) {
        GenericFlags |= MAPPING_ISDIRTY;
    }
    return GenericFlags;
}

/* MmReserveMemory
 * Reserves memory for system use - should be allocated
 * from a fixed memory region that won't interfere with
 * general usage */
VirtualAddress_t*
MmReserveMemory(
	_In_ int Pages)
{
	return (VirtualAddress_t*)atomic_fetch_add(&ReservedMemoryPointer, (PAGE_SIZE * Pages));
}

/* PageSynchronizationHandler
 * Synchronizes the page address specified in the MemorySynchronization Object. */
InterruptStatus_t
PageSynchronizationHandler(
    _In_ void*              Context)
{
    // Variables
    SystemMemorySpace_t *Current = GetCurrentSystemMemorySpace();

    // Make sure the current address space is matching
    // If NULL => everyone must update
    // If it matches our parent, we must update
    // If it matches us, we must update
    if (SyncData.ParentPagingData == NULL ||
        Current->Parent == (SystemMemorySpace_t*)SyncData.ParentPagingData || 
        Current         == (SystemMemorySpace_t*)SyncData.ParentPagingData) {
        memory_invalidate_addr(SyncData.Address);
    }
    SyncData.CallsCompleted++;
    return InterruptHandled;
}

/* SynchronizeVirtualPage
 * Synchronizes the page address across cores to make sure they have the
 * latest revision of the page-table cached. */
void
SynchronizeVirtualPage(
    _In_ SystemMemorySpace_t*   SystemMemorySpace,
    _In_ uintptr_t              Address)
{
    // Multiple cores?
    if (GetMachine()->NumberOfActiveCores <= 1) {
        return;
    }
    assert(InterruptGetActiveStatus() == 0);
    AtomicSectionEnter(&SyncData.SyncObject);
    
    // Setup arguments
    if (Address < MEMORY_LOCATION_KERNEL_END) {
        SyncData.ParentPagingData = NULL; // Everyone must update
    }
    else {
        if (SystemMemorySpace->Parent == NULL) {
            SyncData.ParentPagingData = SystemMemorySpace; // Children of us must update
        }
        else {
            SyncData.ParentPagingData = SystemMemorySpace->Parent; // Parent and siblings!
        }
    }
    SyncData.Address            = Address;
    SyncData.CallsCompleted     = 0;

    // Synchronize the page-tables
    ApicSendInterrupt(InterruptAllButSelf, UUID_INVALID, INTERRUPT_SYNCHRONIZE_PAGE);
    
    // Wait for all cpu's to have handled this.
    while(SyncData.CallsCompleted != (GetMachine()->NumberOfActiveCores - 1));
    AtomicSectionLeave(&SyncData.SyncObject);
}

/* ResolveVirtualSpaceAddress
 * Resolves the virtual address from the given memory flags. There are special areas
 * reserved for special types of allocation. */
OsStatus_t
ResolveVirtualSpaceAddress(
    _In_  SystemMemorySpace_t*  SystemMemorySpace,
    _In_  size_t                Size,
    _In_  Flags_t               Flags,
    _Out_ VirtualAddress_t*     VirtualBase,
    _Out_ PhysicalAddress_t*    PhysicalBase)
{
    // unused
    _CRT_UNUSED(SystemMemorySpace);
    _CRT_UNUSED(PhysicalBase);

    if (Flags & MAPPING_DMA) {
        *VirtualBase = (VirtualAddress_t)MmReserveMemory(DIVUP(Size, PAGE_SIZE));
        return OsSuccess;
    }
    else if (Flags & MAPPING_LEGACY) {
        // @todo
        FATAL(FATAL_SCOPE_KERNEL, "Tried to allocate ISA DMA memory");
    }
    return OsError;
}
