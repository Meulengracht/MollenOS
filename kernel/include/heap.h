/* MollenOS
 *
 * Copyright 2011 - 2018, Philip Meulengracht
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
 * MollenOS Heap Manager (Dynamic Buddy Block System)
 * DBBS Heap Manager
 * Version History:
 * - Version 2.0: Code refactoring, bug-fix with identifiers and
 *                lock improvements, a deadlock was present before.
 * - Version 1.1: The heap allocator has been upgraded 
 *                to involve better error checking, boundary checking
 *                and involve a buddy-allocator system
 *
 * - Version 1.0: Upgraded the linked list allocator to
 *                be a dynamic block system allocator and to support
 *                memory masks, aligned allocations etc
 *
 * - Version 0.1: Linked list memory allocator, simple but it worked
 */

#ifndef _MCORE_HEAP_H_
#define _MCORE_HEAP_H_

/* Includes
 * - System */
#include <os/osdefs.h>
#include <mutex.h>

/* Heap Definitions 
 * These are defined from trial and error 
 * and should probably never be any LESS than
 * these values, of course they could be higher */
#define HEAP_HEADER_MEMORYSIZE      1 // Percentage reserved for header usage. Can be increased, never lower
#define HEAP_NORMAL_BLOCK           0x2000
#define HEAP_LARGE_BLOCK            0x20000
#define HEAP_IDENTIFICATION_SIZE    16
#define HEAP_STANDARD_ALIGN         4
//#define HEAP_USE_IDENTIFICATION

/* HeapNode 
 * Describes an allocation header. */
typedef struct _HeapNode {
#ifdef HEAP_USE_IDENTIFICATION
    char                 Identifier[HEAP_IDENTIFICATION_SIZE];
#endif
    uintptr_t            Address;
    size_t               Length;
    Flags_t              Flags;
    struct _HeapNode    *Link;
} HeapNode_t;

/* HeapNode::Flags
 * Contains definitions and bitfield definitions for HeapNode::Flags */
#define NODE_ALLOCATED              0x1

/* HeapBlock
 * Contains a number of heap-nodes to describe a region of memory. */
typedef struct _HeapBlock {
    Mutex_t              Lock;
    uintptr_t            BaseAddress;
    uintptr_t            EndAddress;
    Flags_t              Flags;
    uintptr_t            Mask;
    size_t               BytesFree;

    struct _HeapBlock   *Link;
    struct _HeapNode    *Nodes;
} HeapBlock_t;

/* HeapBlock::Flags
 * Contains definitions and bitfield definitions for HeapBlock::Flags */
#define BLOCK_NORMAL                0x0
#define BLOCK_ALIGNED               0x1
#define BLOCK_VERY_LARGE            0x2

/* HeapRegion
 * Describes a larger region of memory from where allocations
 * are possible. Reuses header blocks and reserves area for headers. */
typedef struct _HeapRegion {
    Mutex_t              Lock;
    uintptr_t            BaseHeaderAddress;
    uintptr_t            BaseDataAddress;
    uintptr_t            EndAddress;
    uintptr_t            DataCurrent;
    uintptr_t            HeaderCurrent;
    int                  IsUser;

    size_t               BytesAllocated;
    size_t               NumAllocs;
    size_t               NumFrees;
    size_t               NumPages;
    HeapBlock_t         *BlockRecycler;
    HeapNode_t          *NodeRecycler;

    struct _HeapBlock   *Blocks;
    struct _HeapBlock   *PageBlocks;
    struct _HeapBlock   *CustomBlocks;
} Heap_t;

// Options for allocations
#define ALLOCATION_COMMIT           0x00000001
#define ALLOCATION_ZERO             0x00000002

/* HeapConstruct
 * Constructs a new heap, the structure must be pre-allocated
 * or static. Use this for creating the system heap. */
KERNELAPI
OsStatus_t
KERNELABI
HeapConstruct(
    _In_ Heap_t     *Heap,
    _In_ uintptr_t   BaseAddress,
    _In_ uintptr_t   EndAddress,
    _In_ int         UserHeap);

/* HeapDestroy
 * Destroys a heap, frees all memory allocated and pages. */

/* HeapMaintain
 * Maintaining procedure cleaning up un-used pages to the system.
 * Should be called once in a while. Returns number of bytes freed. */
KERNELAPI
size_t
KERNELABI
HeapMaintain(
    _In_ Heap_t *Heap);

/* HeapStatisticsPrint
 * Helper function that enumerates the given heap 
 * and prints out different allocation stats of heap */
KERNELAPI
void
KERNELABI
HeapStatisticsPrint(
    _In_ Heap_t *Heap);

/* Used for validation that an address is allocated
 * within the given heap, this can be used for security
 * or validation purposes, use NULL for kernel heap */
KERNELAPI
OsStatus_t
KERNELABI
HeapValidateAddress(
    Heap_t *Heap,
    uintptr_t Address);

/* HeapQueryMemoryInformation
 * Queries memory information about the given heap. */
KERNELAPI
OsStatus_t
KERNELABI
HeapQueryMemoryInformation(
    _In_ Heap_t *Heap,
    _Out_ size_t *BytesInUse, 
    _Out_ size_t *BlocksAllocated);

/* HeapGetKernel
 * Retrieves a pointer to the static system heap. */
KERNELAPI
Heap_t*
KERNELABI
HeapGetKernel(void);

/* Simply just a wrapper for HeapAllocate
 * with the kernel heap as argument 
 * but this does some basic validation and
 * makes sure pages are mapped in memory
 * this function also returns the physical address 
 * of the allocation and aligned to PAGE_ALIGN with memory <Mask> */
__EXTERN void *kmalloc_apm(size_t Size, uintptr_t *Ptr, uintptr_t Mask);

/* Simply just a wrapper for HeapAllocate
 * with the kernel heap as argument 
 * but this does some basic validation and
 * makes sure pages are mapped in memory
 * this function also returns the physical address 
 * of the allocation and aligned to PAGE_ALIGN */
__EXTERN void *kmalloc_ap(size_t Size, uintptr_t *Ptr);

/* Simply just a wrapper for HeapAllocate
 * with the kernel heap as argument 
 * but this does some basic validation and
 * makes sure pages are mapped in memory
 * this function also returns the physical address 
 * of the allocation */
__EXTERN void *kmalloc_p(size_t Size, uintptr_t *Ptr);

/* Simply just a wrapper for HeapAllocate
 * with the kernel heap as argument 
 * but this does some basic validation and
 * makes sure pages are mapped in memory 
 * the memory returned is PAGE_ALIGNED */
__EXTERN void *kmalloc_a(size_t Size);

/* Simply just a wrapper for HeapAllocate
 * but this does some basic validation and
 * makes sure pages are mapped in memory */
__EXTERN void *kmalloc(size_t Size);

/* kfree 
 * Wrapper for the HeapFree that essentially 
 * just calls it with the kernel heap as argument */
__EXTERN void kfree(void *p);

#endif