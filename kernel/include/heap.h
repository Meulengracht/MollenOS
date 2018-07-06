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

#include <os/osdefs.h>
#include <criticalsection.h>

/* Heap Definitions 
 * These are defined from trial and error 
 * and should probably never be any LESS than
 * these values, of course they could be higher */
#define HEAP_HEADER_MEMORYSIZE      1 // Percentage reserved for header usage. Can be increased, never lower
#define HEAP_HEADER_MEMORYDIVISOR   100 // The division factor for header-memory area
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
    CriticalSection_t    SyncObject;
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
    CriticalSection_t    SyncObject;
    uintptr_t            BaseHeaderAddress;
    uintptr_t            BaseDataAddress;
    uintptr_t            EndAddress;
    _Atomic(uintptr_t)   DataCurrent;
    _Atomic(uintptr_t)   HeaderCurrent;
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
KERNELAPI OsStatus_t KERNELABI
HeapConstruct(
    _In_ Heap_t*    Heap,
    _In_ uintptr_t  BaseAddress,
    _In_ uintptr_t  EndAddress,
    _In_ int        UserHeap);

/* HeapDestroy
 * Destroys a heap, frees all memory allocated and pages. */
KERNELAPI OsStatus_t KERNELABI
HeapDestroy(
    _In_ Heap_t*    Heap);

/* HeapMaintain
 * Maintaining procedure cleaning up un-used pages to the system.
 * Should be called once in a while. Returns number of bytes freed. */
KERNELAPI size_t KERNELABI
HeapMaintain(
    _In_ Heap_t*    Heap);

/* HeapStatisticsPrint
 * Helper function that enumerates the given heap 
 * and prints out different allocation stats of heap */
KERNELAPI void KERNELABI
HeapStatisticsPrint(
    _In_ Heap_t*    Heap);

/* HeapValidateAddress
 * Used for validation that an address is allocated within the given heap, 
 * this can be used for security or validation purposes. */
KERNELAPI OsStatus_t KERNELABI
HeapValidateAddress(
    _In_ Heap_t*    Heap,
    _In_ uintptr_t  Address);

/* HeapQueryMemoryInformation
 * Queries memory information about the given heap. */
KERNELAPI OsStatus_t KERNELABI
HeapQueryMemoryInformation(
    _In_  Heap_t*   Heap,
    _Out_ size_t*   BytesInUse, 
    _Out_ size_t*   BlocksAllocated);

/* HeapGetKernel
 * Retrieves a pointer to the static system heap. */
KERNELAPI Heap_t* KERNELABI
HeapGetKernel(void);

/* k(ernel) memory allocator
 * There are several variants of the allocator, the keywords are:
 * <a> - (align) page-aligned allocation
 * <p> - (physical) returns the physical address of the allocation
 * <m> - (mask) a memory mask for allowing physical addresses that fit the mask. */
KERNELAPI void* KERNELABI
kmalloc_apm(
    _In_ size_t      Length,
    _In_ uintptr_t   Mask,
    _Out_ uintptr_t *PhysicalAddress);
KERNELAPI void* KERNELABI
kmalloc_ap(
    _In_ size_t      Length,
    _Out_ uintptr_t *PhysicalAddress);
KERNELAPI void* KERNELABI
kmalloc_p(
    _In_ size_t      Length,
    _Out_ uintptr_t *PhysicalAddress);
KERNELAPI void* KERNELABI
kmalloc_a(
    _In_ size_t      Length);
KERNELAPI void* KERNELABI
kmalloc(
    _In_ size_t      Length);

/* kfree 
 * Wrapper for the HeapFree that essentially 
 * just calls it with the kernel heap as argument */
KERNELAPI void KERNELABI
kfree(
    _In_ void *Pointer);

#endif
