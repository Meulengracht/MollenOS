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

#include <system/interrupts.h>
#include <system/utils.h>
#include <ds/blbitmap.h>
#include <multiboot.h>
#include <assert.h>
#include <memory.h>
#include <debug.h>
#include <arch.h>
#include <apic.h>
#include <cpu.h>
#include <gdt.h>

// Interface to the arch-specific
extern PAGE_MASTER_LEVEL* MmVirtualGetMasterTable(SystemMemorySpace_t* MemorySpace, VirtualAddress_t Address,
    PAGE_MASTER_LEVEL** ParentDirectory, int* IsCurrent);
extern PageTable_t* MmVirtualGetTable(PAGE_MASTER_LEVEL* ParentPageMasterTable, PAGE_MASTER_LEVEL* PageMasterTable,
    VirtualAddress_t VirtualAddress, int IsCurrent, int CreateIfMissing, Flags_t CreateFlags, int* Update);

extern void memory_invalidate_addr(uintptr_t pda);
extern void memory_load_cr3(uintptr_t pda);
extern void memory_reload_cr3(void);

// Global static storage for the memory
static BlockBitmap_t                    KernelMemory    = { 0 };
static MemorySynchronizationObject_t    SyncData        = { SPINLOCK_INIT, 0 };
static size_t                           BlockmapBytes   = 0;

/* PrintPhysicalMemoryUsage
 * This is a debug function for inspecting
 * the memory status, it spits out how many blocks are in use */
void
PrintPhysicalMemoryUsage(void) {
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
    size_t BytesOccupied = 0;
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
    BytesOccupied += BlockmapBytes + PAGE_SIZE;

    // Free regions given to us by memory map
    for (i = 0; i < (int)BootInformation->MemoryMapLength; i++) {
        if (RegionPointer->Type == 1) {
            ReleaseBlockmapRegion(Memory, 
                (uintptr_t)RegionPointer->Address, (size_t)RegionPointer->Size);
        }
        RegionPointer++;
    }

    // Initialize the kernel memory region
    ConstructBlockmap(&KernelMemory, (void*)(MEMORY_LOCATION_BITMAP + (BlockmapBytes + PAGE_SIZE)), 
        0, MEMORY_LOCATION_RESERVED, MEMORY_LOCATION_KERNEL_END, PAGE_SIZE);
    BytesOccupied += GetBytesNeccessaryForBlockmap(MEMORY_LOCATION_RESERVED, MEMORY_LOCATION_KERNEL_END, PAGE_SIZE) + PAGE_SIZE;

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
    ReserveBlockmapRegion(Memory, MEMORY_LOCATION_BITMAP,   BytesOccupied);
    BlockmapBytes = BytesOccupied;

    // Fill in rest of data
    *MemoryGranularity      = PAGE_SIZE;
    *NumberOfMemoryBlocks   = DIVUP(MemorySize, PAGE_SIZE);
    
    MemoryMap->SystemHeap.Start     = MEMORY_LOCATION_HEAP;
    MemoryMap->SystemHeap.Length    = MEMORY_LOCATION_HEAP_END - MEMORY_LOCATION_HEAP;

    MemoryMap->UserCode.Start       = MEMORY_LOCATION_RING3_CODE;
    MemoryMap->UserCode.Length      = MEMORY_LOCATION_RING3_CODE_END - MEMORY_LOCATION_RING3_CODE;

    MemoryMap->UserHeap.Start       = MEMORY_LOCATION_RING3_HEAP;
    MemoryMap->UserHeap.Length      = MEMORY_LOCATION_RING3_HEAP_END - MEMORY_LOCATION_RING3_HEAP;
    
    MemoryMap->ThreadArea.Start     = MEMORY_LOCATION_RING3_THREAD_START;
    MemoryMap->ThreadArea.Length    = MEMORY_LOCATION_RING3_THREAD_END - MEMORY_LOCATION_RING3_THREAD_START;

    // Debug initial stats
    PrintPhysicalMemoryUsage();
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
    if (!(Flags & MAPPING_READONLY)) {
        NativeFlags |= PAGE_WRITE;
    }
    if (Flags & MAPPING_ISDIRTY) {
        NativeFlags |= PAGE_DIRTY;
    }
    if (Flags & MAPPING_PERSISTENT) {
        NativeFlags |= PAGE_PERSISTENT;
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
    if (Flags & PAGE_PERSISTENT) {
        GenericFlags |= MAPPING_PERSISTENT;
    }
    if (Flags & PAGE_DIRTY) {
        GenericFlags |= MAPPING_ISDIRTY;
    }
    return GenericFlags;
}

/* PageSynchronizationHandler
 * Synchronizes the page address specified in the MemorySynchronization Object. */
InterruptStatus_t
PageSynchronizationHandler(
    _In_ FastInterruptResources_t*  NotUsed,
    _In_ void*                      Context)
{
    SystemMemorySpace_t* Current = GetCurrentMemorySpace();
    UUId_t CurrentHandle         = GetCurrentMemorySpaceHandle();
    _CRT_UNUSED(NotUsed);
    _CRT_UNUSED(Context);

    // Make sure the current address space is matching
    // If NULL => everyone must update
    // If it matches our parent, we must update
    // If it matches us, we must update
    if (SyncData.MemorySpaceHandle == UUID_INVALID ||
        Current->ParentHandle      == SyncData.MemorySpaceHandle || 
        CurrentHandle              == SyncData.MemorySpaceHandle) {
        for (uintptr_t i = 0; i < SyncData.Length; i += PAGE_SIZE) {
            memory_invalidate_addr(SyncData.Address + i);
        }
    }
    SyncData.CallsCompleted++;
    return InterruptHandled;
}

/* SynchronizePageRegion
 * Synchronizes the page address across cores to make sure they have the
 * latest revision of the page-table cached. */
void
SynchronizePageRegion(
    _In_ SystemMemorySpace_t*   SystemMemorySpace,
    _In_ uintptr_t              Address,
    _In_ size_t                 Length)
{
    IntStatus_t Status;

    // Skip this entire step if there is no multiple cores active
    if (GetMachine()->NumberOfActiveCores <= 1) {
        return;
    }
    assert(InterruptGetActiveStatus() == 0);

    // Get the lock before disabling interrupts
    SpinlockAcquire(&SyncData.SyncObject);
    Status = InterruptDisable();
    
    // Setup arguments
    if (Address < MEMORY_LOCATION_KERNEL_END) {
        SyncData.MemorySpaceHandle = UUID_INVALID; // Everyone must update
    }
    else {
        if (SystemMemorySpace->ParentHandle == UUID_INVALID) {
            SyncData.MemorySpaceHandle = GetCurrentMemorySpaceHandle(); // Children of us must update
        }
        else {
            SyncData.MemorySpaceHandle = SystemMemorySpace->ParentHandle; // Parent and siblings!
        }
    }
    SyncData.Address        = Address;
    SyncData.CallsCompleted = 0;

    // Synchronize the page-tables
    ApicSendInterrupt(InterruptAllButSelf, UUID_INVALID, INTERRUPT_SYNCHRONIZE_PAGE);
    
    // Wait for all cpu's to have handled this.
    while(SyncData.CallsCompleted != (GetMachine()->NumberOfActiveCores - 1));

    // Release lock before enabling interrupts to avoid a schedule before we've released.
    SpinlockRelease(&SyncData.SyncObject);
    InterruptRestoreState(Status);
}

/* ResolveVirtualSpaceAddress
 * Resolves the virtual address from the given memory flags. There are special areas
 * reserved for special types of allocation. */
OsStatus_t
ResolveVirtualSpaceAddress(
    _In_  SystemMemorySpace_t*  SystemMemorySpace,
    _In_  size_t                Size,
    _In_  Flags_t               Flags,
    _Out_ VirtualAddress_t*     VirtualBase)
{
    // unused
    _CRT_UNUSED(SystemMemorySpace);

    if (Flags & MAPPING_KERNEL) {
        *VirtualBase = AllocateBlocksInBlockmap(&KernelMemory, __MASK, Size);
        return OsSuccess;
    }
    else if (Flags & MAPPING_LEGACY) {
        // @todo
        FATAL(FATAL_SCOPE_KERNEL, "Tried to allocate ISA DMA memory");
    }
    return OsError;
}

/* ClearKernelMemoryAllocation
 * Clears the kernel memory allocation at the given address and size. */
OsStatus_t
ClearKernelMemoryAllocation(
    _In_ uintptr_t              Address,
    _In_ size_t                 Size)
{
    return ReleaseBlockmapRegion(&KernelMemory, Address, Size);
}

/* SwitchVirtualSpace
 * Updates the currently active memory space for the calling core. */
OsStatus_t
SwitchVirtualSpace(
    SystemMemorySpace_t*        SystemMemorySpace)
{
    // Variables
    assert(SystemMemorySpace != NULL);
    assert(SystemMemorySpace->Data[MEMORY_SPACE_CR3] != 0);
    assert(SystemMemorySpace->Data[MEMORY_SPACE_DIRECTORY] != 0);

    // Update current page-directory
    memory_load_cr3(SystemMemorySpace->Data[MEMORY_SPACE_CR3]);
    return OsSuccess;
}

/* SetVirtualPageAttributes
 * Changes memory protection flags for the given virtual address */
OsStatus_t
SetVirtualPageAttributes(
    _In_ SystemMemorySpace_t*   MemorySpace,
    _In_ VirtualAddress_t       Address,
    _In_ Flags_t                Flags)
{
    // Variabes
    PAGE_MASTER_LEVEL *ParentDirectory;
    PAGE_MASTER_LEVEL *Directory;
    PageTable_t *Table;
    uint32_t Mapping;
    Flags_t ConvertedFlags;
    int IsCurrent, Update;

    ConvertedFlags  = ConvertSystemSpaceToPaging(Flags);
    Directory       = MmVirtualGetMasterTable(MemorySpace, Address, &ParentDirectory, &IsCurrent);
    Table           = MmVirtualGetTable(ParentDirectory, Directory, Address, IsCurrent, 0, ConvertedFlags, &Update);

    // Does page table exist?
    if (Table == NULL) {
        return OsError;
    }
 
    // For kernel mappings we would like to mark the mappings global
    if (Address < MEMORY_LOCATION_KERNEL_END) {
        if (CpuHasFeatures(0, CPUID_FEAT_EDX_PGE) == OsSuccess) {
            ConvertedFlags |= PAGE_GLOBAL;
        }  
    }

    // Map it, make sure we mask the page address so we don't accidently set any flags
    Mapping = atomic_load(&Table->Pages[PAGE_TABLE_INDEX(Address)]);
    if (!(Mapping & PAGE_SYSTEM_MAP)) {
        atomic_store(&Table->Pages[PAGE_TABLE_INDEX(Address)], (Mapping & PAGE_MASK) | ConvertedFlags);
        if (IsCurrent) {
            memory_invalidate_addr(Address);
        }
        return OsSuccess;
    }
    return OsError;
}

/* GetVirtualPageAttributes
 * Retrieves memory protection flags for the given virtual address */
OsStatus_t
GetVirtualPageAttributes(
    _In_  SystemMemorySpace_t*  MemorySpace,
    _In_  VirtualAddress_t      Address,
    _Out_ Flags_t*              Flags)
{
    // Variabes
    PAGE_MASTER_LEVEL *ParentDirectory;
    PAGE_MASTER_LEVEL *Directory;
    PageTable_t *Table;
    int IsCurrent, Update;
    Flags_t OriginalFlags;

    Directory   = MmVirtualGetMasterTable(MemorySpace, Address, &ParentDirectory, &IsCurrent);
    Table       = MmVirtualGetTable(ParentDirectory, Directory, Address, IsCurrent, 0, 0, &Update);

    // Does page table exist?
    if (Table == NULL) {
        return OsError;
    }

    // Map it, make sure we mask the page address so we don't accidently set any flags
    if (Flags != NULL) {
        OriginalFlags   = atomic_load(&Table->Pages[PAGE_TABLE_INDEX(Address)]) & ATTRIBUTE_MASK;
        *Flags          = ConvertPagingToSystemSpace(OriginalFlags);
    }
    return OsSuccess;
}

/* SetVirtualPageMapping
 * Installs a new page-mapping in the given page-directory. The type of mapping 
 * is controlled by the Flags parameter. */
OsStatus_t
SetVirtualPageMapping(
    _In_ SystemMemorySpace_t*   MemorySpace,
    _In_ PhysicalAddress_t      pAddress,
    _In_ VirtualAddress_t       vAddress,
    _In_ Flags_t                Flags)
{
    PAGE_MASTER_LEVEL*  ParentDirectory;
    PAGE_MASTER_LEVEL*  Directory;
    PageTable_t*        Table;
    uintptr_t           Mapping;
    Flags_t             ConvertedFlags;
    int                 Update;
    int                 IsCurrent;
    OsStatus_t          Status = OsSuccess;

    ConvertedFlags  = ConvertSystemSpaceToPaging(Flags);
    Directory       = MmVirtualGetMasterTable(MemorySpace, (vAddress & PAGE_MASK), &ParentDirectory, &IsCurrent);
    Table           = MmVirtualGetTable(ParentDirectory, Directory, (vAddress & PAGE_MASK), IsCurrent, 1, ConvertedFlags, &Update);

    // For kernel mappings we would like to mark the mappings global
    if (vAddress < MEMORY_LOCATION_KERNEL_END) {
        if (CpuHasFeatures(0, CPUID_FEAT_EDX_PGE) == OsSuccess) {
            ConvertedFlags |= PAGE_GLOBAL;
        }
    }

    // If table is null creation failed
    assert(Table != NULL);

    // Make sure value is not mapped already, NEVER overwrite a mapping
    Mapping = atomic_load(&Table->Pages[PAGE_TABLE_INDEX((vAddress & PAGE_MASK))]);
SyncTable:
    if (Mapping != 0) {
        if (ConvertedFlags & PAGE_PERSISTENT) {
            if (Mapping != (pAddress & PAGE_MASK)) {
                FATAL(FATAL_SCOPE_KERNEL, 
                    "Tried to remap fixed virtual address 0x%x => 0x%x (Existing 0x%x), debug-address 0x%x", 
                    vAddress, pAddress, Mapping, &Table->Pages[PAGE_TABLE_INDEX((vAddress & PAGE_MASK))]);
            }
        }
        Status = OsExists;
        goto LeaveFunction;
    }

    // Perform the mapping in a weak context, fast operation
    if (!atomic_compare_exchange_weak(&Table->Pages[PAGE_TABLE_INDEX((vAddress & PAGE_MASK))], 
        &Mapping, (pAddress & PAGE_MASK) | ConvertedFlags)) {
        goto SyncTable;
    }

    // Last step is to invalidate the address in the MMIO
LeaveFunction:
    if (IsCurrent || Update) {
        if (Update) {
            memory_reload_cr3();
        }
        memory_invalidate_addr((vAddress & PAGE_MASK));
    }
    return Status;
}

/* ClearVirtualPageMapping
 * Unmaps a previous mapping from the given page-directory
 * the mapping must be present */
OsStatus_t
ClearVirtualPageMapping(
    _In_ SystemMemorySpace_t*   MemorySpace,
    _In_ VirtualAddress_t       Address)
{
    PAGE_MASTER_LEVEL*  ParentDirectory;
    PAGE_MASTER_LEVEL*  Directory;
    PageTable_t*        Table;
    uintptr_t           Mapping;
    int                 Update;
    int                 IsCurrent;

    Directory   = MmVirtualGetMasterTable(MemorySpace, Address, &ParentDirectory, &IsCurrent);
    Table       = MmVirtualGetTable(ParentDirectory, Directory, Address, IsCurrent, 0, 0, &Update);
 
    if (Table == NULL) {
        return OsError;
    }
    
    // For kernel mappings we would like to mark the page free if it's
    // in the kernel reserved region
    if (Address >= MEMORY_LOCATION_RESERVED && Address < MEMORY_LOCATION_KERNEL_END) {
        ClearKernelMemoryAllocation(Address, PAGE_SIZE);
    }

    // Load the mapping
    Mapping = atomic_load(&Table->Pages[PAGE_TABLE_INDEX(Address)]);
SyncTable:
    if (Mapping & PAGE_PRESENT) {
        if (!(Mapping & PAGE_SYSTEM_MAP)) {
            // Present, not system map
            // Perform the un-mapping in a weak context, fast operation
            if (!atomic_compare_exchange_weak(&Table->Pages[PAGE_TABLE_INDEX(Address)], &Mapping, 0)) {
                goto SyncTable;
            }

            // Release memory, but don't if it is a virtual mapping, that means we 
            // should not free the physical page
            if (!(Mapping & PAGE_PERSISTENT)) {
                FreeSystemMemory(Mapping & PAGE_MASK, PAGE_SIZE);
            }
            
            if (IsCurrent) {
                memory_invalidate_addr(Address);
            }
            return OsSuccess;
        }
    }
    return OsError;
}

/* GetVirtualPageMapping
 * Retrieves the physical address mapping of the
 * virtual memory address given - from the page directory that is given */
uintptr_t
GetVirtualPageMapping(
    _In_ SystemMemorySpace_t*   MemorySpace,
    _In_ VirtualAddress_t       Address)
{
    // Variabes
    PAGE_MASTER_LEVEL *ParentDirectory;
    PAGE_MASTER_LEVEL *Directory;
    PageTable_t *Table;
    uint32_t Mapping;
    int IsCurrent, Update;

    Directory   = MmVirtualGetMasterTable(MemorySpace, Address, &ParentDirectory, &IsCurrent);
    Table       = MmVirtualGetTable(ParentDirectory, Directory, Address, IsCurrent, 0, 0, &Update);
 
    // Does page table exist?
    if (Table == NULL) {
        return 0;
    }

    // Get the address and return with proper offset
    Mapping = atomic_load(&Table->Pages[PAGE_TABLE_INDEX(Address)]);

    // Make sure we still return 0 if the mapping is indeed 0
    if ((Mapping & PAGE_MASK) == 0 || !(Mapping & PAGE_PRESENT)) {
        return 0;
    }
    return ((Mapping & PAGE_MASK) + (Address & ATTRIBUTE_MASK));
}

/* SetDirectIoAccess
 * Set's the io status of the given memory space. */
OsStatus_t
SetDirectIoAccess(
    _In_ UUId_t                     CoreId,
    _In_ SystemMemorySpace_t*       MemorySpace,
    _In_ uint16_t                   Port,
    _In_ int                        Enable)
{
    uint8_t *IoMap = (uint8_t*)MemorySpace->Data[MEMORY_SPACE_IOMAP];

    // Update thread's io-map and the active access
    if (Enable) {
        IoMap[Port / 8] &= ~(1 << (Port % 8));
        if (CoreId == CpuGetCurrentId()) {
            TssEnableIo(CoreId, Port);
        }
    }
    else {
        IoMap[Port / 8] |= (1 << (Port % 8));
        if (CoreId == CpuGetCurrentId()) {
            TssDisableIo(CoreId, Port);
        }
    }
    return OsSuccess;
}
