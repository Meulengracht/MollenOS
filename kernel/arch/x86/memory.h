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
#include <paging.h>

// Shared bitfields
#define PAGE_PRESENT            0x1
#define PAGE_WRITE              0x2
#define PAGE_USER               0x4
#define PAGE_WRITETHROUGH       0x8
#define PAGE_CACHE_DISABLE      0x10
#define PAGE_ACCESSED           0x20

// Page-specific bitfields
#define PAGE_DIRTY              0x40
#define PAGE_PAT                0x80
#define PAGE_GLOBAL             0x100

// PageTable-specific bitfields
#define PAGETABLE_UNUSED        0x40
#define PAGETABLE_LARGE         0x80
#define PAGETABLE_ZERO          0x100 // Must be zero, unused

// OS Bitfields for pages, bits 9-11 are available
#define PAGE_PERSISTENT         0x200
#define PAGE_RESERVED           0x400
#define PAGE_NX                 0x8000000000000000 // amd64 + nx cpuid must be set

// OS Bitfields for page tables, bits 9-11 are available
#define PAGETABLE_INHERITED     0x200

// Function helpers for repeating functions where it pays off
// to have them seperate
#define CREATE_STRUCTURE_HELPER(Type, Name) static Type* MmVirtualCreate##Name(void) { \
                                            Type* Instance = (Type*)AllocateBootMemory(sizeof(Type)); \
                                            assert(Instance != NULL); \
                                            memset((void*)Instance, 0, sizeof(Type)); \
                                            return Instance; }

PACKED_TYPESTRUCT(BIOSMemoryRegion, {
    uint64_t Address;
    uint64_t Size;
    uint32_t Type;        //1 => Available, 2 => ACPI, 3 => Reserved
    uint32_t Nil;
    uint64_t Padding;
});

KERNELAPI OsStatus_t KERNELABI
CreateKernelVirtualMemorySpace(void);

/* ConvertSystemSpaceToPaging
 * Converts system memory-space generic flags to native x86 paging flags */
KERNELAPI Flags_t KERNELABI
ConvertSystemSpaceToPaging(
    _In_ Flags_t Flags);

/* ConvertPagingToSystemSpace
 * Converts native x86 paging flags to system memory-space generic flags */
KERNELAPI Flags_t KERNELABI
ConvertPagingToSystemSpace(
    _In_ Flags_t Flags);

/* ClearKernelMemoryAllocation
 * Clears the kernel memory allocation at the given address and size. */
KERNELAPI OsStatus_t KERNELABI
ClearKernelMemoryAllocation(
    _In_ uintptr_t Address,
    _In_ size_t    Size);

#endif // !_X86_MEMORY_H_
