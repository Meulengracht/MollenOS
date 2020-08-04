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

#include <arch.h>
#include <assert.h>
#include <arch/utils.h>
#include <cpu.h>
#include <ddk/io.h>
#include <debug.h>
#include <gdt.h>
#include <machine.h>
#include <multiboot.h>
#include <memory.h>

// Interface to the arch-specific
extern PAGE_MASTER_LEVEL* MmVirtualGetMasterTable(SystemMemorySpace_t* MemorySpace, VirtualAddress_t Address,
    PAGE_MASTER_LEVEL** ParentDirectory, int* IsCurrent);
extern PageTable_t* MmVirtualGetTable(PAGE_MASTER_LEVEL* ParentPageMasterTable, PAGE_MASTER_LEVEL* PageMasterTable,
    VirtualAddress_t VirtualAddress, int IsCurrent, int CreateIfMissing, int* Update);

extern void memory_invalidate_addr(uintptr_t pda);
extern void memory_load_cr3(uintptr_t pda);
extern void memory_reload_cr3(void);

uintptr_t LastReservedAddress = 0;

// Disable the atomic wrong alignment, as they are aligned and are sanitized
// in the arch-specific layer
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Watomic-alignment"
#endif

void
PrintPhysicalMemoryUsage(void) {
    int    MaxBlocks       = READ_VOLATILE(GetMachine()->PhysicalMemory.capacity);
    int    FreeBlocks      = READ_VOLATILE(GetMachine()->PhysicalMemory.index);
    int    AllocatedBlocks = MaxBlocks - FreeBlocks;
    size_t ReservedMemory  = READ_VOLATILE(LastReservedAddress);
    size_t MemoryInUse     = ReservedMemory + ((size_t)AllocatedBlocks * (size_t)PAGE_SIZE);
    
    TRACE("Memory in use %" PRIuIN " Bytes", MemoryInUse);
    TRACE("Block status %" PRIuIN "/%" PRIuIN, AllocatedBlocks, MaxBlocks);
    TRACE("Reserved memory: 0x%" PRIxIN " (%" PRIuIN " blocks)", LastReservedAddress, LastReservedAddress / PAGE_SIZE);
}

uintptr_t
AllocateBootMemory(
    _In_ size_t Length)
{
    uintptr_t Memory = READ_VOLATILE(LastReservedAddress);
    uintptr_t NextMemory;
    if (!Memory) {
        return 0;
    }
    
    NextMemory = Memory + Length;
    if (NextMemory % PAGE_SIZE) {
        NextMemory += PAGE_SIZE - (NextMemory % PAGE_SIZE);
    }
    WRITE_VOLATILE(LastReservedAddress, NextMemory);
    return Memory;
}

OsStatus_t
InitializeSystemMemory(
    _In_ Multiboot_t*        BootInformation,
    _In_ bounded_stack_t*    PhysicalMemory,
    _In_ StaticMemoryPool_t* GlobalAccessMemory,
    _In_ SystemMemoryMap_t*  MemoryMap,
    _In_ size_t*             MemoryGranularity,
    _In_ size_t*             NumberOfMemoryBlocks)
{
    BIOSMemoryRegion_t* RegionPointer;
    uintptr_t           MemorySize;
    uintptr_t           GAMemory;
    size_t              Count;
    size_t              GAMemorySize;
    OsStatus_t          Status;
    int                 i;

    RegionPointer = (BIOSMemoryRegion_t*)(uintptr_t)BootInformation->MemoryMapAddress;

    // The memory-high part is 64kb blocks 
    // whereas the memory-low part is bytes of memory
    MemorySize  = (BootInformation->MemoryHigh * 64 * 1024);
    MemorySize  += BootInformation->MemoryLow; // This is in kilobytes
    assert((MemorySize / 1024 / 1024) >= 64);
    
    // Initialize the reserved memory address
    WRITE_VOLATILE(LastReservedAddress,  MEMORY_LOCATION_RESERVED);
    
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
    
    // Create the physical memory map
    Count = MemorySize / PAGE_SIZE;
    bounded_stack_construct(PhysicalMemory, (void*)AllocateBootMemory(
        Count * sizeof(void*)), (int)Count);
    
    // Create the global access memory, it needs to start after the last reserved
    // memory address, because the reserved memory is not freeable or allocatable.
    GAMemorySize = MEMORY_LOCATION_VIDEO - READ_VOLATILE(LastReservedAddress);
    GAMemory     = AllocateBootMemory(StaticMemoryPoolCalculateSize(GAMemorySize, PAGE_SIZE));
    
    // Create the system kernel virtual memory space, this call identity maps all
    // memory allocated by AllocateBootMemory, and also allocates some itself
    Status = CreateKernelVirtualMemorySpace();
    if (Status != OsSuccess) {
        return Status;
    }
    
    // After the AllocateBootMemory+CreateKernelVirtualMemorySpace call, the reserved address 
    // has moved again, which means we actually have allocated too much memory right 
    // out the box, however we accept this memory waste, as it's max a few 10's of kB.
    GAMemorySize = MEMORY_LOCATION_VIDEO - READ_VOLATILE(LastReservedAddress);
    TRACE("[pmem] [mem_init] initial size of ga memory to 0x%" PRIxIN, GAMemorySize);    
    if (!IsPowerOfTwo(GAMemorySize)) {
        GAMemorySize = NextPowerOfTwo(GAMemorySize) >> 1U;
        TRACE("[pmem] [mem_init] adjusting size of ga memory to 0x%" PRIxIN, GAMemorySize);    
    }
    StaticMemoryPoolConstruct(GlobalAccessMemory, (void*)GAMemory, 
        READ_VOLATILE(LastReservedAddress), GAMemorySize, PAGE_SIZE);
    
    // So now we go through the memory regions provided by the system and add the physical pages
    // we can use, that are not already pre-allocated by the system.
    // ISSUE: it seems that the highest address (total number of blocks) actually
    // exceeds the number of initial blocks available
    TRACE("[pmem] [mem_init] region count %i, block count %u", BootInformation->MemoryMapLength, Count);
    for (i = 0; i < (int)BootInformation->MemoryMapLength; i++) {
        if (RegionPointer->Type == 1) {
            uintptr_t Address = (uintptr_t)RegionPointer->Address;
            uintptr_t Limit   = (uintptr_t)RegionPointer->Address + (uintptr_t)RegionPointer->Size;
            TRACE("[pmem] [mem_init] region %i: 0x%" PRIxIN " => 0x%" PRIxIN, i, Address, Limit);
            if (Address < LastReservedAddress) {
                Address = LastReservedAddress;
            }
            
            while (Address < Limit) {
                bounded_stack_push(PhysicalMemory, (void*)Address);
                Address += PAGE_SIZE;
            }
        }
        RegionPointer++;
    }

    // Debug initial stats
    PrintPhysicalMemoryUsage();
    return OsSuccess;
}

static unsigned int
ConvertGenericAttributesToX86(
    _In_ unsigned int Flags)
{
    unsigned int NativeFlags = 0;

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

static unsigned int
ConvertX86AttributesToGeneric(
    _In_ unsigned int Flags)
{
    unsigned int GenericFlags = 0;

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
    _In_  unsigned int*             AttributeValues,
    _Out_ int*                 PagesCleared)
{
    PAGE_MASTER_LEVEL* ParentDirectory;
    PAGE_MASTER_LEVEL* Directory;
    PageTable_t*       Table;
    int                IsCurrent, Update;
    unsigned int       X86Attributes;
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
    return Status;
}

OsStatus_t
ArchMmuUpdatePageAttributes(
    _In_  SystemMemorySpace_t* MemorySpace,
    _In_  VirtualAddress_t     StartAddress,
    _In_  int                  PageCount,
    _In_  unsigned int*             Attributes,
    _Out_ int*                 PagesUpdated)
{
    PAGE_MASTER_LEVEL* ParentDirectory;
    PAGE_MASTER_LEVEL* Directory;
    PageTable_t*       Table;
    unsigned int            X86Attributes;
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
    _In_ SystemMemorySpace_t*     MemorySpace,
    _In_ VirtualAddress_t         StartAddress,
    _In_ const PhysicalAddress_t* PhysicalAddressValues,
    _In_  int                     PageCount,
    _Out_ int*                    PagesComitted)
{
    
    PAGE_MASTER_LEVEL* ParentDirectory;
    PAGE_MASTER_LEVEL* Directory;
    PageTable_t*       Table;
    int                Update;
    int                IsCurrent;
    int                Index;
    int                i      = 0;
    OsStatus_t         Status = OsSuccess;

    Directory = MmVirtualGetMasterTable(MemorySpace, StartAddress, &ParentDirectory, &IsCurrent);
    while (PageCount && Status == OsSuccess) {
        Table = MmVirtualGetTable(ParentDirectory, Directory, StartAddress, IsCurrent, 0, &Update);
        if (!Table) {
            Status = (i == 0) ? OsDoesNotExist : OsIncomplete;
            break;
        }
        
        Index = PAGE_TABLE_INDEX(StartAddress);
        for (; Index < ENTRIES_PER_PAGE && PageCount; Index++, PageCount--, i++, StartAddress += PAGE_SIZE) {
            uintptr_t Mapping = atomic_load(&Table->Pages[Index]);
            uintptr_t NewMapping = ((Mapping & PAGE_MASK) != 0) ? 
                (Mapping & ~(PAGE_RESERVED)) | PAGE_PRESENT : 
                ((PhysicalAddressValues[i] & PAGE_MASK) | (Mapping & ~(PAGE_RESERVED)) | PAGE_PRESENT);
            
            if (Mapping & PAGE_PRESENT) { // Mapping was already comitted
                Status = (i == 0) ? OsExists : OsIncomplete;
                break;
            }
            
            if (!(Mapping & PAGE_RESERVED)) { // Mapping was not reserved
                Status = (i == 0) ? OsDoesNotExist : OsIncomplete;
                break;
            }
            
            if (!atomic_compare_exchange_strong(&Table->Pages[Index], &Mapping, NewMapping)) {
                ERROR("[arch_commit_virtual] failed to update address 0x%" PRIxIN ", existing mapping was in place 0x%" PRIxIN,
                    StartAddress, Mapping);
                Status = OsIncomplete;
                break;
            }
            
            if (IsCurrent) {
                memory_invalidate_addr(StartAddress);
            }
        }
    }
    *PagesComitted = i;
    return Status;
}

KERNELAPI OsStatus_t KERNELABI
ArchMmuSetContiguousVirtualPages(
    _In_  SystemMemorySpace_t* MemorySpace,
    _In_  VirtualAddress_t     StartAddress,
    _In_  PhysicalAddress_t    PhysicalStartAddress,
    _In_  int                  PageCount,
    _In_  unsigned int              Attributes,
    _Out_ int*                 PagesUpdated)
{
    PAGE_MASTER_LEVEL* ParentDirectory;
    PAGE_MASTER_LEVEL* Directory;
    PageTable_t*       Table;
    unsigned int            X86Attributes;
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
    while (PageCount && Status == OsSuccess) {
        Table = MmVirtualGetTable(ParentDirectory, Directory, StartAddress, IsCurrent, 1, &Update);
        assert(Table != NULL);
        
        Index = PAGE_TABLE_INDEX(StartAddress);
        for (; Index < ENTRIES_PER_PAGE && PageCount; 
                Index++, PageCount--, i++, StartAddress += PAGE_SIZE, PhysicalStartAddress += PAGE_SIZE) {
            uintptr_t Mapping = (PhysicalStartAddress & PAGE_MASK) | X86Attributes;
            if (!atomic_compare_exchange_strong(&Table->Pages[Index], &Zero, Mapping)) {
                // Tried to replace a value that was not 0
                ERROR("[arch_update_virtual_cont] failed to update address 0x%" PRIxIN ", existing mapping was in place 0x%" PRIxIN,
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
    _In_  unsigned int              Attributes,
    _Out_ int*                 PagesReserved)
{
    PAGE_MASTER_LEVEL* ParentDirectory;
    PAGE_MASTER_LEVEL* Directory;
    PageTable_t*       Table;
    unsigned int            X86Attributes;
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
    while (PageCount && Status == OsSuccess) {
        Table = MmVirtualGetTable(ParentDirectory, Directory, StartAddress, IsCurrent, 1, &Update);
        assert(Table != NULL);
        
        Index = PAGE_TABLE_INDEX(StartAddress);
        for (; Index < ENTRIES_PER_PAGE && PageCount; Index++, PageCount--, i++, StartAddress += PAGE_SIZE) {
            if (!atomic_compare_exchange_strong(&Table->Pages[Index], &Zero, X86Attributes)) {
                // Tried to replace a value that was not 0
                ERROR("[arch_reserve_virtual] failed to reserve address 0x%" PRIxIN ", existing mapping was in place 0x%" PRIxIN,
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
    _In_  SystemMemorySpace_t*     MemorySpace,
    _In_  VirtualAddress_t         StartAddress,
    _In_  const PhysicalAddress_t* PhysicalAddressValues,
    _In_  int                      PageCount,
    _In_  unsigned int             Attributes,
    _Out_ int*                     PagesUpdated)
{
    PAGE_MASTER_LEVEL* ParentDirectory;
    PAGE_MASTER_LEVEL* Directory;
    PageTable_t*       Table;
    unsigned int            X86Attributes;
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
    while (PageCount && Status == OsSuccess) {
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
                Mapping &= PAGE_MASK;
                IrqSpinlockAcquire(&GetMachine()->PhysicalMemoryLock);
                bounded_stack_push(&GetMachine()->PhysicalMemory, (void*)Mapping);
                IrqSpinlockRelease(&GetMachine()->PhysicalMemoryLock);
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
