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
#define __MODULE "MEM0"
#define __TRACE

#include <arch/interrupts.h>
#include <arch/utils.h>
#include <ds/blbitmap.h>
#include <multiboot.h>
#include <machine.h>
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
    VirtualAddress_t VirtualAddress, int IsCurrent, int CreateIfMissing, int* Update);

extern void memory_invalidate_addr(uintptr_t pda);
extern void memory_load_cr3(uintptr_t pda);
extern void memory_reload_cr3(void);

// Global static storage for the memory
static size_t BlockmapBytes       = 0;
uintptr_t     LastReservedAddress = 0;

// Disable the atomic wrong alignment, as they are aligned and are sanitized
// in the arch-specific layer
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Watomic-alignment"
#endif

void
PrintPhysicalMemoryUsage(void) {
    TRACE("Bitmap size: %" PRIuIN " Bytes", BlockmapBytes);
    TRACE("Memory in use %" PRIuIN " Bytes", GetMachine()->PhysicalMemory.BlocksAllocated * PAGE_SIZE);
    TRACE("Block status %" PRIuIN "/%" PRIuIN "", GetMachine()->PhysicalMemory.BlocksAllocated, GetMachine()->PhysicalMemory.BlockCount);
    TRACE("Reserved memory: 0x%" PRIxIN " (%" PRIuIN " blocks)", LastReservedAddress, LastReservedAddress / PAGE_SIZE);
}

/* InitializeSystemMemory (@arch)
 * Initializes the entire system memory range, selecting ranges that should
 * be reserved and those that are free for system use. */
OsStatus_t
InitializeSystemMemory(
    _In_ Multiboot_t*       BootInformation,
    _In_ BlockBitmap_t*     Memory,
    _In_ BlockBitmap_t*     GlobalAccessMemory,
    _In_ SystemMemoryMap_t* MemoryMap,
    _In_ size_t*            MemoryGranularity,
    _In_ size_t*            NumberOfMemoryBlocks)
{
    BIOSMemoryRegion_t* RegionPointer = NULL;
    uintptr_t           MemorySize;
    size_t              BytesOccupied = 0;
    int                 i;

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
            ReleaseBlockmapRegion(Memory, (uintptr_t)RegionPointer->Address, (size_t)RegionPointer->Size);
        }
        RegionPointer++;
    }

    // Initialize the kernel memory region
    ConstructBlockmap(GlobalAccessMemory, (void*)(MEMORY_LOCATION_BITMAP + (BlockmapBytes + PAGE_SIZE)), 
        0, MEMORY_LOCATION_RESERVED, MEMORY_LOCATION_KERNEL_END, PAGE_SIZE);
    BytesOccupied += GetBytesNeccessaryForBlockmap(MEMORY_LOCATION_RESERVED, MEMORY_LOCATION_KERNEL_END, PAGE_SIZE) + PAGE_SIZE;

    // Mark default regions in use and special regions
    //  0x0000 - 0x1000     || Used for catching null-pointers
    //  0x1000 - 0x7FFFF    || Free RAM (mBoot)
    //  0x80000 - 0xFFFFF   || BIOS Area (Mostly)
    //  0x4000 + 0x8000     || Used for memory region & Trampoline-code
    //  0x90000 - 0x9F000   || Kernel Stack
    //  0x100000 - KernelSize
    //  0x200000 - RamDiskSize
    //  0x300000 - ??       || Bitmap Space
    ReserveBlockmapRegion(Memory, 0,                       MEMORY_LOCATION_KERNEL);
    ReserveBlockmapRegion(Memory, MEMORY_LOCATION_KERNEL,  BootInformation->KernelSize + PAGE_SIZE);
    ReserveBlockmapRegion(Memory, MEMORY_LOCATION_RAMDISK, BootInformation->RamdiskSize + PAGE_SIZE);
#if defined(amd64) || defined(__amd64__)
    ReserveBlockmapRegion(Memory, MEMORY_LOCATION_BOOTPAGING, 0x5000);
#endif
    ReserveBlockmapRegion(Memory, MEMORY_LOCATION_BITMAP, BytesOccupied);
    BlockmapBytes       = BytesOccupied;
    LastReservedAddress = MEMORY_LOCATION_BITMAP + BytesOccupied;

    // Fill in rest of data
    *MemoryGranularity    = PAGE_SIZE;
    *NumberOfMemoryBlocks = DIVUP(MemorySize, PAGE_SIZE);
    
    MemoryMap->KernelRegion.Start  = 0;
    MemoryMap->KernelRegion.Length = MEMORY_LOCATION_KERNEL_END;

    MemoryMap->UserCode.Start  = MEMORY_LOCATION_RING3_CODE;
    MemoryMap->UserCode.Length = MEMORY_LOCATION_RING3_CODE_END - MEMORY_LOCATION_RING3_CODE;

    MemoryMap->UserHeap.Start  = MEMORY_LOCATION_RING3_HEAP;
    MemoryMap->UserHeap.Length = MEMORY_LOCATION_RING3_HEAP_END - MEMORY_LOCATION_RING3_HEAP;
    
    MemoryMap->ThreadRegion.Start  = MEMORY_LOCATION_RING3_THREAD_START;
    MemoryMap->ThreadRegion.Length = MEMORY_LOCATION_RING3_THREAD_END - MEMORY_LOCATION_RING3_THREAD_START;

    // Debug initial stats
    PrintPhysicalMemoryUsage();
    return OsSuccess;
}

/* ConvertSystemSpaceToPaging
 * Converts system memory-space generic flags to native x86 paging flags */
Flags_t
ConvertSystemSpaceToPaging(Flags_t Flags)
{
    Flags_t NativeFlags = 0;

    if (Flags & MAPPING_COMMIT) {
        NativeFlags |= PAGE_PRESENT;
    }
    else {
        NativeFlags |= PAGE_RESERVED;
    }
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
    Flags_t GenericFlags = 0;

    if (Flags & (PAGE_PRESENT | PAGE_RESERVED)) {
        GenericFlags = MAPPING_EXECUTABLE; // For now 
        if (Flags & PAGE_PRESENT) {
            GenericFlags |= MAPPING_COMMIT;
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
    }
    return GenericFlags;
}

/* SwitchVirtualSpace
 * Updates the currently active memory space for the calling core. */
OsStatus_t
SwitchVirtualSpace(
    SystemMemorySpace_t* SystemMemorySpace)
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
    PAGE_MASTER_LEVEL* ParentDirectory;
    PAGE_MASTER_LEVEL* Directory;
    PageTable_t*       Table;
    uint32_t           Mapping;
    Flags_t            ConvertedFlags;
    int                IsCurrent, Update;
    int                Index = PAGE_TABLE_INDEX(Address);

    ConvertedFlags = ConvertSystemSpaceToPaging(Flags);
    Directory      = MmVirtualGetMasterTable(MemorySpace, Address, &ParentDirectory, &IsCurrent);
    Table          = MmVirtualGetTable(ParentDirectory, Directory, Address, IsCurrent, 0, &Update);
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
    Mapping = (atomic_load(&Table->Pages[Index])) & PAGE_MASK;
    atomic_store(&Table->Pages[Index], Mapping | ConvertedFlags);
    if (IsCurrent) {
        memory_invalidate_addr(Address);
    }
    return OsSuccess;
}

/* GetVirtualPageAttributes
 * Retrieves memory protection flags for the given virtual address */
OsStatus_t
GetVirtualPageAttributes(
    _In_  SystemMemorySpace_t* MemorySpace,
    _In_  VirtualAddress_t     Address,
    _Out_ Flags_t*             Flags)
{
    PAGE_MASTER_LEVEL* ParentDirectory;
    PAGE_MASTER_LEVEL* Directory;
    PageTable_t*       Table;
    int                IsCurrent, Update;
    Flags_t            OriginalFlags;
    int                Index = PAGE_TABLE_INDEX(Address);

    Directory = MmVirtualGetMasterTable(MemorySpace, Address, &ParentDirectory, &IsCurrent);
    Table     = MmVirtualGetTable(ParentDirectory, Directory, Address, IsCurrent, 0, &Update);
    if (Table == NULL) {
        return OsError;
    }

    // Map it, make sure we mask the page address so we don't accidently set any flags
    if (Flags != NULL) {
        OriginalFlags = atomic_load(&Table->Pages[Index]) & ATTRIBUTE_MASK;
        *Flags        = ConvertPagingToSystemSpace(OriginalFlags);
    }
    return OsSuccess;
}

OsStatus_t
CommitVirtualPageMapping(
    _In_ SystemMemorySpace_t* MemorySpace,
    _In_ PhysicalAddress_t    pAddress,
    _In_ VirtualAddress_t     vAddress)
{
    PAGE_MASTER_LEVEL* ParentDirectory;
    PAGE_MASTER_LEVEL* Directory;
    PageTable_t*       Table;
    uintptr_t          Mapping;
    int                Update;
    int                IsCurrent;
    int                Index  = PAGE_TABLE_INDEX(vAddress);
    OsStatus_t         Status = OsSuccess;

    vAddress &= PAGE_MASK;
    Directory = MmVirtualGetMasterTable(MemorySpace, vAddress, &ParentDirectory, &IsCurrent);
    Table     = MmVirtualGetTable(ParentDirectory, Directory, vAddress, IsCurrent, 0, &Update);
    if (Table == NULL) {
        return OsDoesNotExist;
    }

    // Make sure value is not mapped already, NEVER overwrite a mapping
    Mapping = atomic_load(&Table->Pages[Index]);
SyncTable:
    if (Mapping & PAGE_PRESENT) {
        Status = OsExists;
        goto LeaveFunction;
    }
    if (!(Mapping & PAGE_RESERVED)) {
        Status = OsDoesNotExist;
        goto LeaveFunction;
    }
    
    // Build the mapping, reuse existing attached physical if it exists
    if ((Mapping & PAGE_MASK) != 0) {
        pAddress = Mapping | PAGE_PRESENT;
    }
    else {
        pAddress = (pAddress & PAGE_MASK) | (Mapping & ATTRIBUTE_MASK) | PAGE_PRESENT;
    }
    pAddress &= ~(PAGE_RESERVED);

    // Perform the mapping in a weak context, fast operation
    if (!atomic_compare_exchange_weak(&Table->Pages[Index], &Mapping, pAddress)) {
        goto SyncTable;
    }

    // Last step is to invalidate the address in the MMIO
LeaveFunction:
    if (IsCurrent || Update) {
        if (Update) {
            memory_reload_cr3();
        }
        memory_invalidate_addr(vAddress);
    }
    return Status;
}

OsStatus_t
SetVirtualPageMapping(
    _In_ SystemMemorySpace_t* MemorySpace,
    _In_ PhysicalAddress_t    pAddress,
    _In_ VirtualAddress_t     vAddress,
    _In_ Flags_t              Flags)
{
    PAGE_MASTER_LEVEL* ParentDirectory;
    PAGE_MASTER_LEVEL* Directory;
    PageTable_t*       Table;
    uintptr_t          Mapping;
    Flags_t            ConvertedFlags;
    int                Update;
    int                IsCurrent;
    int                Index  = PAGE_TABLE_INDEX(vAddress);
    OsStatus_t         Status = OsSuccess;

    vAddress      &= PAGE_MASK;
    ConvertedFlags = ConvertSystemSpaceToPaging(Flags);
    Directory      = MmVirtualGetMasterTable(MemorySpace, vAddress, &ParentDirectory, &IsCurrent);
    Table          = MmVirtualGetTable(ParentDirectory, Directory, vAddress, IsCurrent, 1, &Update);

    // For kernel mappings we would like to mark the mappings global
    if (vAddress < MEMORY_LOCATION_KERNEL_END) {
        if (CpuHasFeatures(0, CPUID_FEAT_EDX_PGE) == OsSuccess) {
            ConvertedFlags |= PAGE_GLOBAL;
        }
    }

    // If table is null creation failed
    assert(Table != NULL);

    // Make sure value is not mapped already, NEVER overwrite a mapping
    pAddress = (pAddress & PAGE_MASK) | ConvertedFlags;
    Mapping  = atomic_load(&Table->Pages[Index]);
SyncTable:
    if (Mapping != 0) {
        Status = OsExists;
        goto LeaveFunction;
    }

    // Perform the mapping in a weak context, fast operation
    if (!atomic_compare_exchange_weak(&Table->Pages[Index], &Mapping, pAddress)) {
        goto SyncTable;
    }

    // Last step is to invalidate the address in the MMIO
LeaveFunction:
    if (IsCurrent || Update) {
        if (Update) {
            memory_reload_cr3();
        }
        memory_invalidate_addr(vAddress);
    }
    return Status;
}

OsStatus_t
ClearVirtualPageMapping(
    _In_ SystemMemorySpace_t* MemorySpace,
    _In_ VirtualAddress_t     Address)
{
    PAGE_MASTER_LEVEL* ParentDirectory;
    PAGE_MASTER_LEVEL* Directory;
    PageTable_t*       Table;
    uintptr_t          Mapping;
    int                Update;
    int                IsCurrent;
    int                Index = PAGE_TABLE_INDEX(Address);

    Directory = MmVirtualGetMasterTable(MemorySpace, Address, &ParentDirectory, &IsCurrent);
    Table     = MmVirtualGetTable(ParentDirectory, Directory, Address, IsCurrent, 0, &Update);
    if (Table == NULL) {
        return OsError;
    }

    // Load the mapping
    Mapping = atomic_load(&Table->Pages[Index]);
SyncTable:
    if (Mapping != 0) {
        // Maybe present, not system map
        // Perform the clearing in a weak context, fast operation
        if (!atomic_compare_exchange_weak(&Table->Pages[Index], &Mapping, 0)) {
            goto SyncTable;
        }

        // Invalidate page if it was present
        if ((Mapping & PAGE_PRESENT) && IsCurrent) {
            memory_invalidate_addr(Address);
        }

        // Release memory, but don't if it is a virtual mapping, that means we 
        // should not free the physical page. We only do this if the memory
        // is marked as present, otherwise we don't
        if ((Mapping & PAGE_PRESENT) && !(Mapping & PAGE_PERSISTENT)) {
            FreeSystemMemory(Mapping & PAGE_MASK, PAGE_SIZE);
        }
        return OsSuccess;
    }
    return OsDoesNotExist;
}

uintptr_t
GetVirtualPageMapping(
    _In_ SystemMemorySpace_t* MemorySpace,
    _In_ VirtualAddress_t     Address)
{
    PAGE_MASTER_LEVEL* ParentDirectory;
    PAGE_MASTER_LEVEL* Directory;
    PageTable_t*       Table;
    uint32_t           Mapping;
    int                IsCurrent, Update;
    int                Index = PAGE_TABLE_INDEX(Address);

    Directory = MmVirtualGetMasterTable(MemorySpace, Address, &ParentDirectory, &IsCurrent);
    Table     = MmVirtualGetTable(ParentDirectory, Directory, Address, IsCurrent, 0, &Update);
    if (Table == NULL) {
        return 0;
    }

    // Get the address and return with proper offset
    Mapping = atomic_load(&Table->Pages[Index]);

    // Make sure we still return 0 if the mapping is indeed 0
    if ((Mapping & PAGE_MASK) == 0 || !(Mapping & PAGE_PRESENT)) {
        return 0;
    }
    return ((Mapping & PAGE_MASK) + (Address & ATTRIBUTE_MASK));
}

OsStatus_t
SetDirectIoAccess(
    _In_ UUId_t               CoreId,
    _In_ SystemMemorySpace_t* MemorySpace,
    _In_ uint16_t             Port,
    _In_ int                  Enable)
{
    uint8_t *IoMap = (uint8_t*)MemorySpace->Data[MEMORY_SPACE_IOMAP];

    // Update thread's io-map and the active access
    if (Enable) {
        IoMap[Port / 8] &= ~(1 << (Port % 8));
        if (CoreId == ArchGetProcessorCoreId()) {
            TssEnableIo(CoreId, Port);
        }
    }
    else {
        IoMap[Port / 8] |= (1 << (Port % 8));
        if (CoreId == ArchGetProcessorCoreId()) {
            TssDisableIo(CoreId, Port);
        }
    }
    return OsSuccess;
}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
