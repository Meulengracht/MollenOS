/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS x86 Memory Definitions, Structures, Explanations
 */

#ifndef _X86_MEMORY_H_
#define _X86_MEMORY_H_

#include <os/osdefs.h>
#include <atomicsection.h>
#include <multiboot.h>
#include <machine.h>
#include <paging.h>

/* Shared PT/Page Definitions */
#define PAGE_PRESENT          	0x1
#define PAGE_WRITE              0x2
#define PAGE_USER               0x4
#define PAGE_WRITETHROUGH       0x8
#define PAGE_CACHE_DISABLE      0x10
#define PAGE_ACCESSED           0x20

/* Page Table Definitions */
#define PAGETABLE_UNUSED        0x40
#define PAGETABLE_LARGE         0x80
#define PAGETABLE_IGNORED       0x100

/* Page Definitions */
#define PAGE_DIRTY              0x40
#define PAGE_UNUSED             0x80
#define PAGE_GLOBAL             0x100

/* MollenOS PT/Page Definitions */
#define PAGE_SYSTEM_MAP         0x200
#define PAGE_INHERITED          0x400
#define PAGE_VIRTUAL            0x800

/* Memory Map Structure 
 * This is the structure passed to us by the mBoot bootloader */
PACKED_TYPESTRUCT(BIOSMemoryRegion, {
    uint64_t        Address;
    uint64_t        Size;
    uint32_t        Type;        //1 => Available, 2 => ACPI, 3 => Reserved
    uint32_t        Nil;
    uint64_t        Padding;
});

/* MemorySynchronizationObject
 * Used to synchronize paging structures across all cpu cores. */
typedef struct _MemorySynchronizationObject {
    AtomicSection_t SyncObject;
    volatile int    CallsCompleted;
    void*           ParentPagingData;
    uintptr_t       Address;
} MemorySynchronizationObject_t;

/* MmPhyiscalInit
 * This is the physical memory manager initializor
 * It reads the multiboot memory descriptor(s), initialies
 * the bitmap and makes sure reserved regions are allocated */
KERNELAPI OsStatus_t KERNELABI
MmPhyiscalInit(
    _In_ Multiboot_t*       BootInformation);

/* MmPhysicalQuery
 * Queries information about current block status */
KERNELAPI OsStatus_t KERNELABI
MmPhysicalQuery(
    _Out_Opt_ size_t*       BlocksTotal,
    _Out_Opt_ size_t*       BlocksAllocated);

/* MmPhysicalAllocateBlock
 * This is the primary function for allocating
 * physical memory pages, this takes an argument
 * <Mask> which determines where in memory the allocation is OK */
KERNELAPI PhysicalAddress_t KERNELABI
MmPhysicalAllocateBlock(
    _In_ uintptr_t          Mask, 
    _In_ int                Count);

/* MmPhysicalFreeBlock
 * This is the primary function for
 * freeing physical pages, but NEVER free physical
 * pages if they exist in someones mapping */
KERNELAPI OsStatus_t KERNELABI
MmPhysicalFreeBlock(
    _In_ PhysicalAddress_t  Address);

/* MmVirtualInit
 * Initializes the virtual memory system and
 * installs default kernel mappings */
KERNELAPI OsStatus_t KERNELABI
MmVirtualInit(void);

/* MmVirtualClone
 * Clones a new virtual memory space for an application to use. */
KERNELAPI OsStatus_t KERNELABI
MmVirtualClone(
    _In_  int               Inherit,
    _Out_ void**            PageDirectory,
    _Out_ uintptr_t*        Pdb);

/* MmVirtualDestroy
 * Destroys and cleans up any resources used by the virtual address space. */
KERNELAPI OsStatus_t KERNELABI
MmVirtualDestroy(
    _In_ void*              PageDirectory);

/* MmVirtualSetFlags
 * Changes memory protection flags for the given virtual address */
KERNELAPI OsStatus_t KERNELABI
MmVirtualSetFlags(
    _In_ void*              ParentPageDirectory,
    _In_ void*              PageDirectory, 
    _In_ VirtualAddress_t   vAddress, 
    _In_ Flags_t            Flags);

/* MmVirtualGetFlags
 * Retrieves memory protection flags for the given virtual address */
KERNELAPI OsStatus_t KERNELABI
MmVirtualGetFlags(
    _In_ void*              ParentPageDirectory,
    _In_ void*              PageDirectory, 
    _In_ VirtualAddress_t   vAddress, 
    _In_ Flags_t*           Flags);

/* MmVirtualMap
 * Installs a new page-mapping in the given
 * page-directory. The type of mapping is controlled by
 * the Flags parameter. */
KERNELAPI OsStatus_t KERNELABI
MmVirtualMap(
    _In_ void*              ParentPageDirectory,
    _In_ void*              PageDirectory, 
    _In_ PhysicalAddress_t  pAddress, 
    _In_ VirtualAddress_t   vAddress, 
    _In_ Flags_t            Flags);

/* MmVirtualUnmap
 * Unmaps a previous mapping from the given page-directory
 * the mapping must be present */
KERNELAPI OsStatus_t KERNELABI
MmVirtualUnmap(
    _In_ void*              ParentPageDirectory,
    _In_ void*              PageDirectory, 
    _In_ VirtualAddress_t   Address);

/* MmVirtualGetMapping
 * Retrieves the physical address mapping of the
 * virtual memory address given - from the page directory 
 * that is given */
KERNELAPI PhysicalAddress_t KERNELABI
MmVirtualGetMapping(
    _In_ void*              ParentPageDirectory,
    _In_ void*              PageDirectory, 
    _In_ VirtualAddress_t   Address);

/* MmReserveMemory
 * Reserves memory for system use - should be allocated
 * from a fixed memory region that won't interfere with
 * general usage */
KERNELAPI VirtualAddress_t* KERNELABI
MmReserveMemory(
    _In_ int                Pages);

/* UpdateVirtualAddressingSpace
 * Switches page-directory for the current cpu instance */
KERNELAPI OsStatus_t KERNELABI
UpdateVirtualAddressingSpace(
    _In_ void*              PageDirectory, 
    _In_ PhysicalAddress_t  Pdb);

/* InitializeMemoryForApplicationCore
 * Initializes the missing memory setup for the calling cpu */
KERNELAPI void KERNELABI
InitializeMemoryForApplicationCore(void);

/* PageSynchronizationHandler
 * Synchronizes the page address specified in the MemorySynchronization Object. */
KERNELAPI InterruptStatus_t KERNELABI
PageSynchronizationHandler(
    _In_ void*              Context);

#endif // !_X86_MEMORY_H_
