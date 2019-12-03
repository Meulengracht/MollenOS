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
 * Memory Utility Functions
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

OsStatus_t
InitializeSystemMemory(
    _In_ Multiboot_t*       BootInformation,
    _In_ BlockBitmap_t*     Memory,
    _In_ BlockBitmap_t*     GlobalAccessMemory,
    _In_ SystemMemoryMap_t* MemoryMap,
    _In_ size_t*            MemoryGranularity,
    _In_ size_t*            NumberOfMemoryBlocks)
{
    BIOSMemoryRegion_t* RegionPointer;
    uintptr_t           MemorySize;
    size_t              BytesOccupied = 0;
    int                 i;

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

static Flags_t
ConvertGenericAttributesToX86(
    _In_ Flags_t Flags)
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

static Flags_t
ConvertX86AttributesToGeneric(
    _In_ Flags_t Flags)
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

void
ArchMmuSwitchMemorySpace(
    SystemMemorySpace_t* MemorySpace)
{
    assert(MemorySpace != NULL);
    assert(MemorySpace->Data[MEMORY_SPACE_CR3] != 0);
    assert(MemorySpace->Data[MEMORY_SPACE_DIRECTORY] != 0);
    memory_load_cr3(MemorySpace->Data[MEMORY_SPACE_CR3]);
}

OsStatus_t
ArchMmuGetPageAttributes(
    _In_  SystemMemorySpace_t* MemorySpace,
    _In_  VirtualAddress_t     StartAddress,
    _In_  int                  PageCount,
    _In_  Flags_t*             AttributeValues,
    _Out_ int*                 PagesCleared)
{
    PAGE_MASTER_LEVEL* ParentDirectory;
    PAGE_MASTER_LEVEL* Directory;
    PageTable_t*       Table;
    int                IsCurrent, Update;
    Flags_t            X86Attributes;
    int                Index;
    int                i      = 0;
    OsStatus_t         Status = OsSuccess;
    
    assert(AttributeValues != NULL);

    Directory = MmVirtualGetMasterTable(MemorySpace, StartAddress, &ParentDirectory, &IsCurrent);
    while (PageCount) {
        Table = MmVirtualGetTable(ParentDirectory, Directory, StartAddress, IsCurrent, 0, &Update);
        if (Table == NULL) {
            Status = (i == 0) ? OsDoesNotExist : OsIncomplete;
            break;
        }
        
        Index = PAGE_TABLE_INDEX(StartAddress);
        for (; Index < ENTRIES_PER_PAGE && PageCount; Index++, PageCount--, i++, StartAddress += PAGE_SIZE) {
            X86Attributes = atomic_load(&Table->Pages[Index]);
            AttributeValues[i] = ConvertX86AttributesToGeneric(X86Attributes & ATTRIBUTE_MASK);
        }
    }
    *PagesCleared = i;
    return OsSuccess;
}

OsStatus_t
ArchMmuUpdatePageAttributes(
    _In_  SystemMemorySpace_t* MemorySpace,
    _In_  VirtualAddress_t     StartAddress,
    _In_  int                  PageCount,
    _In_  Flags_t*             Attributes,
    _Out_ int*                 PagesUpdated)
{
    PAGE_MASTER_LEVEL* ParentDirectory;
    PAGE_MASTER_LEVEL* Directory;
    PageTable_t*       Table;
    Flags_t            X86Attributes;
    int                IsCurrent, Update;
    int                Index;
    int                i      = 0;
    OsStatus_t         Status = OsSuccess;

    X86Attributes = ConvertGenericAttributesToX86(*Attributes);
    
    // For kernel mappings we would like to mark the mappings global
    if (StartAddress < MEMORY_LOCATION_KERNEL_END) {
        if (CpuHasFeatures(0, CPUID_FEAT_EDX_PGE) == OsSuccess) {
            X86Attributes |= PAGE_GLOBAL;
        }
    }
    
    Directory = MmVirtualGetMasterTable(MemorySpace, StartAddress, &ParentDirectory, &IsCurrent);
    while (PageCount) {
        Table = MmVirtualGetTable(ParentDirectory, Directory, StartAddress, IsCurrent, 0, &Update);
        if (Table == NULL) {
            Status = (i == 0) ? OsDoesNotExist : OsIncomplete;
            break;
        }
        
        Index = PAGE_TABLE_INDEX(StartAddress);
        for (; Index < ENTRIES_PER_PAGE && PageCount; Index++, PageCount--, i++, StartAddress += PAGE_SIZE) {
            uintptr_t Mapping        = atomic_load(&Table->Pages[Index]);
            uintptr_t UpdatedMapping = (Mapping & PAGE_MASK) | X86Attributes;
            if (!i) {
                *Attributes = ConvertX86AttributesToGeneric(Mapping & ATTRIBUTE_MASK);
            }
            
            if (!atomic_compare_exchange_strong(&Table->Pages[Index], &Mapping, UpdatedMapping)) {
                if (IsCurrent) {
                    memory_invalidate_addr(StartAddress);
                }
                Status = (i == 0) ? OsBusy : OsIncomplete;
                break;
            }
            
            if (IsCurrent) {
                memory_invalidate_addr(StartAddress);
            }
        }
    }
    *PagesUpdated = i;
    return Status;
}

OsStatus_t
ArchMmuCommitVirtualPage(
    _In_ SystemMemorySpace_t* MemorySpace,
    _In_ VirtualAddress_t     Address,
    _In_ PhysicalAddress_t    PhysicalAddress)
{
    PAGE_MASTER_LEVEL* ParentDirectory;
    PAGE_MASTER_LEVEL* Directory;
    PageTable_t*       Table;
    uintptr_t          Mapping;
    int                Update;
    int                IsCurrent;
    int                Index  = PAGE_TABLE_INDEX(Address);
    OsStatus_t         Status = OsSuccess;

    Directory = MmVirtualGetMasterTable(MemorySpace, Address, &ParentDirectory, &IsCurrent);
    Table     = MmVirtualGetTable(ParentDirectory, Directory, Address, IsCurrent, 0, &Update);
    if (Table == NULL) {
        return OsDoesNotExist;
    }

    // Make sure value is not mapped already, NEVER overwrite a mapping
    Mapping = atomic_load(&Table->Pages[Index]);
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
        PhysicalAddress = Mapping | PAGE_PRESENT;
    }
    else {
        PhysicalAddress = (PhysicalAddress & PAGE_MASK) | (Mapping & ATTRIBUTE_MASK) | PAGE_PRESENT;
    }
    PhysicalAddress &= ~(PAGE_RESERVED);

    // Perform the mapping in a weak context, fast operation.
    if (!atomic_compare_exchange_strong(&Table->Pages[Index], &Mapping, PhysicalAddress)) {
        Status = OsExists;
    }
    
LeaveFunction:
    if (IsCurrent) {
        memory_invalidate_addr(Address);
    }
    return Status;
}

KERNELAPI OsStatus_t KERNELABI
ArchMmuSetContiguousVirtualPages(
    _In_  SystemMemorySpace_t* MemorySpace,
    _In_  VirtualAddress_t     StartAddress,
    _In_  PhysicalAddress_t    PhysicalStartAddress,
    _In_  int                  PageCount,
    _In_  Flags_t              Attributes,
    _Out_ int*                 PagesUpdated)
{
    PAGE_MASTER_LEVEL* ParentDirectory;
    PAGE_MASTER_LEVEL* Directory;
    PageTable_t*       Table;
    Flags_t            X86Attributes;
    int                Update;
    int                IsCurrent;
    int                Index;
    int                i      = 0;
    OsStatus_t         Status = OsSuccess;
    uintptr_t          Zero   = 0;

    X86Attributes = ConvertGenericAttributesToX86(Attributes);
    
    // For kernel mappings we would like to mark the mappings global
    if (StartAddress < MEMORY_LOCATION_KERNEL_END) {
        if (CpuHasFeatures(0, CPUID_FEAT_EDX_PGE) == OsSuccess) {
            X86Attributes |= PAGE_GLOBAL;
        }
    }

    Directory = MmVirtualGetMasterTable(MemorySpace, StartAddress, &ParentDirectory, &IsCurrent);
    while (PageCount) {
        Table = MmVirtualGetTable(ParentDirectory, Directory, StartAddress, IsCurrent, 1, &Update);
        assert(Table != NULL);
        
        Index = PAGE_TABLE_INDEX(StartAddress);
        for (; Index < ENTRIES_PER_PAGE && PageCount; 
                Index++, PageCount--, i++, StartAddress += PAGE_SIZE, PhysicalStartAddress += PAGE_SIZE) {
            uintptr_t Mapping = (PhysicalStartAddress & PAGE_MASK) | X86Attributes;
            if (!atomic_compare_exchange_strong(&Table->Pages[Index], &Zero, Mapping)) {
                // Tried to replace a value that was not 0
                ERROR("[arch_update_virtual] failed to update address 0x%" PRIxIN ", existing mapping was in place 0x%" PRIxIN,
                    StartAddress, Zero);
                Status = OsIncomplete;
                break;
            }
            
            if (IsCurrent) {
                memory_invalidate_addr(StartAddress);
            }
        }
    }
    *PagesUpdated = i;
    return Status;
}

OsStatus_t
ArchMmuReserveVirtualPages(
    _In_  SystemMemorySpace_t* MemorySpace,
    _In_  VirtualAddress_t     StartAddress,
    _In_  int                  PageCount,
    _In_  Flags_t              Attributes,
    _Out_ int*                 PagesReserved)
{
    PAGE_MASTER_LEVEL* ParentDirectory;
    PAGE_MASTER_LEVEL* Directory;
    PageTable_t*       Table;
    Flags_t            X86Attributes;
    int                Update;
    int                IsCurrent;
    int                Index;
    int                i      = 0;
    OsStatus_t         Status = OsSuccess;
    uintptr_t          Zero   = 0;

    X86Attributes = ConvertGenericAttributesToX86(Attributes);
    
    // For kernel mappings we would like to mark the mappings global
    if (StartAddress < MEMORY_LOCATION_KERNEL_END) {
        if (CpuHasFeatures(0, CPUID_FEAT_EDX_PGE) == OsSuccess) {
            X86Attributes |= PAGE_GLOBAL;
        }
    }

    Directory = MmVirtualGetMasterTable(MemorySpace, StartAddress, &ParentDirectory, &IsCurrent);
    while (PageCount) {
        Table = MmVirtualGetTable(ParentDirectory, Directory, StartAddress, IsCurrent, 1, &Update);
        assert(Table != NULL);
        
        Index = PAGE_TABLE_INDEX(StartAddress);
        for (; Index < ENTRIES_PER_PAGE && PageCount; Index++, PageCount--, i++, StartAddress += PAGE_SIZE) {
            if (!atomic_compare_exchange_strong(&Table->Pages[Index], &Zero, X86Attributes)) {
                // Tried to replace a value that was not 0
                ERROR("[arch_update_virtual] failed to reserve address 0x%" PRIxIN ", existing mapping was in place 0x%" PRIxIN,
                    StartAddress, Zero);
                Status = OsIncomplete;
                break;
            }
        }
    }
    *PagesReserved = i;
    return Status;
}

OsStatus_t
ArchMmuSetVirtualPages(
    _In_  SystemMemorySpace_t* MemorySpace,
    _In_  VirtualAddress_t     StartAddress,
    _In_  PhysicalAddress_t*   PhysicalAddressValues,
    _In_  int                  PageCount,
    _In_  Flags_t              Attributes,
    _Out_ int*                 PagesUpdated)
{
    PAGE_MASTER_LEVEL* ParentDirectory;
    PAGE_MASTER_LEVEL* Directory;
    PageTable_t*       Table;
    Flags_t            X86Attributes;
    int                Update;
    int                IsCurrent;
    int                Index;
    int                i      = 0;
    OsStatus_t         Status = OsSuccess;
    uintptr_t          Zero   = 0;

    X86Attributes = ConvertGenericAttributesToX86(Attributes);
    
    // For kernel mappings we would like to mark the mappings global
    if (StartAddress < MEMORY_LOCATION_KERNEL_END) {
        if (CpuHasFeatures(0, CPUID_FEAT_EDX_PGE) == OsSuccess) {
            X86Attributes |= PAGE_GLOBAL;
        }
    }

    Directory = MmVirtualGetMasterTable(MemorySpace, StartAddress, &ParentDirectory, &IsCurrent);
    while (PageCount) {
        Table = MmVirtualGetTable(ParentDirectory, Directory, StartAddress, IsCurrent, 1, &Update);
        assert(Table != NULL);
        
        Index = PAGE_TABLE_INDEX(StartAddress);
        for (; Index < ENTRIES_PER_PAGE && PageCount; Index++, PageCount--, i++, StartAddress += PAGE_SIZE) {
            uintptr_t Mapping = (PhysicalAddressValues[i] & PAGE_MASK) | X86Attributes;
            if (!atomic_compare_exchange_strong(&Table->Pages[Index], &Zero, Mapping)) {
                // Tried to replace a value that was not 0
                ERROR("[arch_update_virtual] failed to update address 0x%" PRIxIN ", existing mapping was in place 0x%" PRIxIN,
                    StartAddress, Zero);
                Status = OsIncomplete;
                break;
            }
            
            if (IsCurrent) {
                memory_invalidate_addr(StartAddress);
            }
        }
    }
    *PagesUpdated = i;
    return Status;
}

OsStatus_t
ArchMmuClearVirtualPages(
    _In_  SystemMemorySpace_t* MemorySpace,
    _In_  VirtualAddress_t     StartAddress,
    _In_  int                  PageCount,
    _Out_ int*                 PagesCleared)
{
    PAGE_MASTER_LEVEL* ParentDirectory;
    PAGE_MASTER_LEVEL* Directory;
    PageTable_t*       Table;
    uintptr_t          Mapping;
    int                Update;
    int                IsCurrent;
    int                Index;
    int                i      = 0;
    OsStatus_t         Status = OsSuccess;

    Directory = MmVirtualGetMasterTable(MemorySpace, StartAddress, &ParentDirectory, &IsCurrent);
    while (PageCount) {
        Table = MmVirtualGetTable(ParentDirectory, Directory, StartAddress, IsCurrent, 0, &Update);
        if (Table == NULL) {
            Status = (i == 0) ? OsDoesNotExist : OsIncomplete;
            break;
        }
        
        Index = PAGE_TABLE_INDEX(StartAddress);
        for (; Index < ENTRIES_PER_PAGE && PageCount; Index++, PageCount--, i++, StartAddress += PAGE_SIZE) {
            Mapping = atomic_exchange(&Table->Pages[Index], 0);
            if (IsCurrent) {
                memory_invalidate_addr(StartAddress);
            }
            
            // Release memory, but don't if it is a virtual mapping, that means we 
            // should not free the physical page. We only do this if the memory
            // is marked as present, otherwise we don't
            if ((Mapping & PAGE_PRESENT) && !(Mapping & PAGE_PERSISTENT)) {
                FreeSystemMemory(Mapping & PAGE_MASK, PAGE_SIZE);
            }
        }
    }
    *PagesCleared = i;
    return Status;
}

OsStatus_t
ArchMmuVirtualToPhysical(
    _In_  SystemMemorySpace_t* MemorySpace,
    _In_  VirtualAddress_t     StartAddress,
    _In_  int                  PageCount,
    _In_  PhysicalAddress_t*   PhysicalAddressValues,
    _Out_ int*                 PagesRetrieved)
{
    PAGE_MASTER_LEVEL* ParentDirectory;
    PAGE_MASTER_LEVEL* Directory;
    PageTable_t*       Table;
    uint32_t           Mapping;
    int                IsCurrent, Update;
    int                Index;
    int                i      = 0;
    OsStatus_t         Status = OsSuccess;
    
    Directory = MmVirtualGetMasterTable(MemorySpace, StartAddress, &ParentDirectory, &IsCurrent);
    while (PageCount) {
        Table = MmVirtualGetTable(ParentDirectory, Directory, StartAddress, IsCurrent, 0, &Update);
        if (Table == NULL) {
            Status = (i == 0) ? OsDoesNotExist : OsIncomplete;
            break;
        }
        
        Index = PAGE_TABLE_INDEX(StartAddress);
        for (; Index < ENTRIES_PER_PAGE && PageCount; Index++, PageCount--, i++, StartAddress += PAGE_SIZE) {
            Mapping = atomic_load(&Table->Pages[Index]);
            Mapping &= PAGE_MASK;
            if (!i) {
                Mapping |= StartAddress & ATTRIBUTE_MASK;
            }
            PhysicalAddressValues[i] = Mapping;
        }
    }
    *PagesRetrieved = i;
    return Status;
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
