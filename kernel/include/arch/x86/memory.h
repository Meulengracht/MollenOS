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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS x86 Memory Definitions, Structures, Explanations
 */

#ifndef _X86_MEMORY_H_
#define _X86_MEMORY_H_

#include <os/osdefs.h>

#if defined(__i386__)
#include <arch/x86/x32/paging.h>
#else
#include <arch/x86/x64/paging.h>
#endif

DECL_STRUCT(MemorySpace);

// Shared bitfields
#define PAGE_PRESENT            0x1U
#define PAGE_WRITE              0x2U
#define PAGE_USER               0x4U
#define PAGE_WRITETHROUGH       0x8U
#define PAGE_CACHE_DISABLE      0x10U
#define PAGE_ACCESSED           0x20U

// Page-specific bitfields
#define PAGE_DIRTY              0x40U
#define PAGE_PAT                0x80U
#define PAGE_GLOBAL             0x100U

// PageTable-specific bitfields
#define PAGETABLE_UNUSED        0x40u
#define PAGETABLE_LARGE         0x80U  // 4MB (32 bit), 2MB (64 bit, if used on PD), 1GB (64 bit, if used on PDP)
#define PAGETABLE_ZERO          0x100U // Must be zero, unused

// OS Bitfields for pages, bits 9-11 are available
#define PAGE_PERSISTENT         0x200U
#define PAGE_RESERVED           0x400U
#define PAGE_NX                 0x8000000000000000U // amd64 + nx cpuid must be set

// OS Bitfields for page tables, bits 9-11 are available
#define PAGETABLE_INHERITED     0x200U

// Function helpers for repeating functions where it pays off
// to have them seperate
#define CREATE_STRUCTURE_HELPER(Type, Name) static Type* MmVirtualCreate##Name(vaddr_t* virtualBase) { \
                                            paddr_t physicalBase; \
                                            MachineAllocateBootMemory(sizeof(Type), virtualBase, &physicalBase); \
                                            assert(physicalBase != 0); \
                                            memset((void*)physicalBase, 0, sizeof(Type)); \
                                            return (Type*)physicalBase; }

/**
 * @brief Prepares the kernel addressing space. This will be called while it is possible
 * to allocate boot memory for the virtual addressing space. It is expected that the addressing
 * space will accomodate all boot memory mappings are available once the switch happens. This means
 * identity mapping the allocated addresses up until this point.
 */
KERNELAPI void KERNELABI
MmBootPrepareKernel(void);

/**
 * @brief
 *
 * @param masterTable
 * @param address
 * @return
 */
KERNELAPI PageTable_t* KERNELABI
MmBootGetPageTable(
        _In_ PAGE_MASTER_LEVEL* masterTable,
        _In_ vaddr_t            address);

/**
 * @brief
 *
 * @param memorySpace
 * @param address
 * @param parentDirectory
 * @param isCurrentOut
 * @return
 */
KERNELAPI PAGE_MASTER_LEVEL* KERNELABI
MmVirtualGetMasterTable(
        _In_ MemorySpace_t*      memorySpace,
        _In_ vaddr_t             address,
        _In_ PAGE_MASTER_LEVEL** parentDirectory,
        _In_ int*                isCurrentOut);

/**
 * @brief
 *
 * @param parentPageDirectory
 * @param pageDirectory
 * @param address
 * @param isCurrent
 * @param createIfMissing
 * @param update
 * @return
 */
KERNELAPI PageTable_t* KERNELABI
MmVirtualGetTable(
        _In_  PAGE_MASTER_LEVEL* parentPageDirectory,
        _In_  PAGE_MASTER_LEVEL* pageDirectory,
        _In_  vaddr_t            address,
        _In_  int                isCurrent,
        _In_  int                createIfMissing,
        _Out_ int*               update);

/**
 * @brief
 *
 * @param source
 * @param inherit
 * @param cr3Out
 * @param pdirOut
 * @return
 */
KERNELAPI oscode_t KERNELABI
MmVirtualClone(
        _In_ MemorySpace_t* source,
        _In_ int            inherit,
        _Out_ paddr_t*      cr3Out,
        _Out_ vaddr_t*      pdirOut);

#endif // !_X86_MEMORY_H_
