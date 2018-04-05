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
#define __MODULE "HEAP"
//#define __TRACE

/* Includes 
 * - System */
#include <system/addressspace.h>
#include <debug.h>
#include <arch.h>
#include <heap.h>

/* Includes 
 * - Library */
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

/* These are internal allocation flags
 * and get applied based on size for optimization */
#define ALLOCATION_PAGEALIGN        0x10000000
#define ALLOCATION_BIG              0x20000000

/* Helper to define whether or not an allocation is normal
 * or modified by allocation flags as alignment/size */
#define ALLOCISNORMAL(x)            ((x & (ALLOCATION_PAGEALIGN | ALLOCATION_BIG)) == 0)
#define ALLOCISNOTBIG(x)            ((x & ALLOCATION_BIG) == 0)
#define ALLOCISPAGE(x)              (((x & ALLOCATION_PAGEALIGN) != 0) && ALLOCISNOTBIG(x))

#ifdef HEAP_USE_IDENTIFICATION
static const char *GlbKernelUnknown = "Unknown";
#define IDENTIFIER                  Identifier,
#else
#define IDENTIFIER
#endif

/* Globals 
 * - Static storage for fixed data */
static Heap_t GlbKernelHeap         = { { 0 }, 0 };

/* HeapAllocateInternal
 * Allocates header-space. This function acquires the heap-lock. */
uintptr_t*
HeapAllocateInternal(
    _In_ Heap_t *Heap,
    _In_ size_t  Length)
{
    // Variables
    uintptr_t ReturnAddressEnd  = 0;
    uintptr_t ReturnAddress     = 0;
    Flags_t MapFlags            = ((Heap->IsUser == 1) ? ASPACE_FLAG_APPLICATION : 0) | ASPACE_FLAG_SUPPLIEDVIRTUAL;

    // Sanitize the size, and memory status
    assert(Length > 0);
    assert((Heap->HeaderCurrent + Length) < Heap->BaseDataAddress);

    // Allocate address, locked operation
    MutexLock(&Heap->Lock);
    ReturnAddress = Heap->HeaderCurrent;
    Heap->HeaderCurrent += Length;
    MutexUnlock(&Heap->Lock);

    // Sanitize that the header address is mapped, take into account
    // a physical memory page-boundary
    ReturnAddressEnd = ReturnAddress + Length;
    if (!AddressSpaceGetMapping(AddressSpaceGetCurrent(), ReturnAddress)) {
        AddressSpaceMap(AddressSpaceGetCurrent(), NULL, &ReturnAddress, AddressSpaceGetPageSize(), MapFlags, __MASK);
    }
    if (!AddressSpaceGetMapping(AddressSpaceGetCurrent(), ReturnAddressEnd)) {
        AddressSpaceMap(AddressSpaceGetCurrent(), NULL, &ReturnAddressEnd, AddressSpaceGetPageSize(), MapFlags, __MASK);
    }
    return (uintptr_t*)ReturnAddress;
}

/* HeapBlockInsert
 * Appends a block onto the correct block-list in the heap. */
OsStatus_t
HeapBlockInsert(
    _In_ Heap_t         *Heap,
    _In_ HeapBlock_t    *Block)
{
    // Variables
    HeapBlock_t *CurrentBlock = NULL;

    // Debug
    TRACE("HeapBlockInsert()");

    // Sanitize input
    if (Block == NULL) {
        ERROR("Invalid block, is null");
        return OsError;
    }

    // Select list to iterate, locked ops
    MutexLock(&Heap->Lock);
    if (Block->Flags & BLOCK_VERY_LARGE) {
        if (Heap->CustomBlocks == NULL) {
            Heap->CustomBlocks = Block;
            goto Unlock;
        }
        CurrentBlock = Heap->CustomBlocks;
    }
    else if (Block->Flags & BLOCK_ALIGNED) {
        if (Heap->PageBlocks == NULL) {
            Heap->PageBlocks = Block;
            goto Unlock;
        }
        CurrentBlock = Heap->PageBlocks;
    }
    else {
        if (Heap->Blocks == NULL) {
            Heap->Blocks = Block;
            goto Unlock;
        }
        CurrentBlock = Heap->Blocks;
    }

    // Loop to end of block chain and append
    while (CurrentBlock->Link) {
        CurrentBlock = CurrentBlock->Link;
    }
    CurrentBlock->Link = Block;
    Block->Link = NULL;

Unlock:
    MutexUnlock(&Heap->Lock);
    return OsSuccess;
}

/* HeapStatisticsCount
 * Helper function that enumerates a given block
 * list, since we need to support multiple lists, reuse this */
void
HeapStatisticsCount(
    _In_ HeapBlock_t    *Block,
    _Out_ size_t        *BlockCounter,
    _Out_ size_t        *NodeCounter,
    _Out_ size_t        *NodesAllocated,
    _Out_ size_t        *BytesAllocated)
{
    while (Block) {
        HeapNode_t *CurrentNode = Block->Nodes;
        while (CurrentNode) {
            (*NodesAllocated) += (CurrentNode->Flags & NODE_ALLOCATED) ? 1 : 0;
            (*BytesAllocated) += (CurrentNode->Flags & NODE_ALLOCATED) ? CurrentNode->Length : 0;
            (*NodeCounter) += 1;
            CurrentNode = CurrentNode->Link;
        }
        (*BlockCounter) += 1;
        Block = Block->Link;
    }
}

/* HeapStatisticsPrint
 * Helper function that enumerates the given heap 
 * and prints out different allocation stats of heap */
void
HeapStatisticsPrint(
    _In_ Heap_t *Heap)
{
    // Variables
    size_t StatNodeCount            = 0;
    size_t StatBlockCount           = 0;
    size_t StatBytesAllocated       = 0;
    size_t StatNodesAllocated       = 0;

    size_t StatBlockPageCount       = 0;
    size_t StatBlockBigCount        = 0;
    size_t StatBlockNormCount       = 0;

    size_t StatNodePageCount        = 0;
    size_t StatNodeBigCount         = 0;
    size_t StatNodeNormCount        = 0;
   
    size_t StatNodePageAllocated    = 0;
    size_t StatNodeBigAllocated     = 0;
    size_t StatNodeNormAllocated    = 0;

    size_t StatNodePageAllocatedBytes   = 0;
    size_t StatNodeBigAllocatedBytes    = 0;
    size_t StatNodeNormAllocatedBytes   = 0;

    // Retrieve block counters
    HeapStatisticsCount(Heap->Blocks, &StatBlockNormCount, &StatNodeNormCount,
        &StatNodeNormAllocated, &StatNodeNormAllocatedBytes);
    HeapStatisticsCount(Heap->PageBlocks, &StatBlockPageCount, &StatNodePageCount,
        &StatNodePageAllocated, &StatNodePageAllocatedBytes);
    HeapStatisticsCount(Heap->CustomBlocks, &StatBlockBigCount, &StatNodeBigCount,
        &StatNodeBigAllocated, &StatNodeBigAllocatedBytes);

    // Calculate the resulting information
    StatBlockCount = (StatBlockNormCount + StatBlockPageCount + StatBlockBigCount);
    StatNodeCount = (StatNodeNormCount + StatNodePageCount + StatNodeBigCount);
    StatNodesAllocated = (StatNodeNormAllocated + StatNodePageAllocated + StatNodeBigAllocated);
    StatBytesAllocated = (StatNodeNormAllocatedBytes + StatNodePageAllocatedBytes + StatNodeBigAllocatedBytes);

    WRITELINE("Heap Stats (Salloc ptr at 0x%x, next data at 0x%x", 
        Heap->HeaderCurrent, Heap->DataCurrent);
    WRITELINE("  -- Blocks Total: %u", StatBlockCount);
    WRITELINE("     -- Normal Blocks: %u", StatBlockNormCount);
    WRITELINE("     -- Page Blocks: %u", StatBlockPageCount);
    WRITELINE("     -- Big Blocks: %u", StatBlockBigCount);
    WRITELINE("  -- Nodes Total: %u (Allocated %u)", StatNodeCount, StatNodesAllocated);
    WRITELINE("     -- Normal Nodes: %u (Bytes - %u)", StatNodeNormCount, StatNodeNormAllocatedBytes);
    WRITELINE("     -- Page Nodes: %u (Bytes - %u)", StatNodePageCount, StatNodePageAllocatedBytes);
    WRITELINE("     -- Big Nodes: %u (Bytes - %u)", StatNodeBigCount, StatNodeBigAllocatedBytes);
    WRITELINE("  -- Bytes Total Allocated: %u", StatBytesAllocated);
}

/* HeapCreateBlock
 * Helper to allocate and create a block for a given heap
 * it will allocate from the recycler if possible */
HeapBlock_t*
HeapCreateBlock(
    _In_ Heap_t         *Heap, 
#ifdef HEAP_USE_IDENTIFICATION
    _In_ __CONST char   *Identifier,
#endif
    _In_ size_t          Length,
    _In_ uintptr_t       Mask,
    _In_ Flags_t         Flags)
{
    // Variables
    HeapBlock_t *hBlock     = NULL;
    HeapNode_t *hNode       = NULL;
    uintptr_t BaseAddress   = 0;

    // OOM Sanitization
    if ((Heap->DataCurrent + Length) > Heap->EndAddress) {
#if __BITS == 64
        FATAL(FATAL_SCOPE_KERNEL, "Out of memory (Current 0x%llx, End 0x%llx)", Heap->DataCurrent, Heap->EndAddress);
#elif __BITS == 32
        FATAL(FATAL_SCOPE_KERNEL, "Out of memory (Current 0x%x, End 0x%x)", Heap->DataCurrent, Heap->EndAddress);
#endif
        return NULL;
    }

    // Can we pop on off from recycler?
    // The below operations must be locked
    MutexLock(&Heap->Lock);
    if (Heap->BlockRecycler != NULL) {
        hBlock = Heap->BlockRecycler;
        Heap->BlockRecycler = Heap->BlockRecycler->Link;
    }
    else {
        hBlock = (HeapBlock_t*)HeapAllocateInternal(Heap, sizeof(HeapBlock_t));
    }
    if (Heap->NodeRecycler != NULL) {
        hNode = Heap->NodeRecycler;
        Heap->NodeRecycler = Heap->NodeRecycler->Link;
    }
    else {
        hNode = (HeapNode_t*)HeapAllocateInternal(Heap, sizeof(HeapNode_t));
    }

    // Sanitize blocks
    assert(hBlock != NULL);
    assert(hNode != NULL);

    BaseAddress = Heap->DataCurrent;
    Heap->DataCurrent += Length;
    MutexUnlock(&Heap->Lock);

    // Initialize members to default values
    memset(hBlock, 0, sizeof(HeapBlock_t));
    MutexConstruct(&hBlock->Lock);
    hBlock->BaseAddress = BaseAddress;
    hBlock->EndAddress = BaseAddress + Length - 1;
    hBlock->BytesFree = Length;
    hBlock->Flags = Flags;
    hBlock->Nodes = hNode;
    hBlock->Mask = Mask;

    // Initialize members of node to default
    memset(hNode, 0, sizeof(HeapNode_t));
#ifdef HEAP_USE_IDENTIFICATION
    memcpy(&hNode->Identifier[0], Identifier == NULL ? GlbKernelUnknown : Identifier,
        Identifier == NULL ? strlen(GlbKernelUnknown) : strnlen(Identifier, HEAP_IDENTIFICATION_SIZE - 1));
    hNode->Identifier[HEAP_IDENTIFICATION_SIZE - 1] = '\0';
#endif
    hNode->Address = BaseAddress;
    hNode->Length = Length;
    return hBlock;
}

/* HeapExpand
 * Helper to expand the heap with a given size, now 
 * it also heavily depends on which kind of allocation is being made */
OsStatus_t
HeapExpand(
    _In_ Heap_t         *Heap,
#ifdef HEAP_USE_IDENTIFICATION
    _In_ __CONST char   *Identifier,
#endif
    _In_ size_t          Length,
    _In_ uintptr_t       Mask,
    _In_ Flags_t         Flags)
{
    // Variables
    HeapBlock_t *hBlock = NULL;

    // Perform allocation based on type
    if (Flags & ALLOCATION_BIG) {
        hBlock = HeapCreateBlock(Heap, IDENTIFIER Length, Mask, BLOCK_VERY_LARGE);
    }
    else if (Flags & ALLOCATION_PAGEALIGN) {
        hBlock = HeapCreateBlock(Heap, IDENTIFIER HEAP_LARGE_BLOCK, Mask, BLOCK_ALIGNED);
    }
    else {
        hBlock = HeapCreateBlock(Heap, IDENTIFIER HEAP_NORMAL_BLOCK, Mask, BLOCK_NORMAL);
    }
    return HeapBlockInsert(Heap, hBlock);
}

/* HeapAllocateSizeInBlock
 * Helper function for the primary allocation function, this 
 * 'sub'-allocates <size> in a given <block> from the heap */
uintptr_t
HeapAllocateSizeInBlock(
    _In_ Heap_t         *Heap,
#ifdef HEAP_USE_IDENTIFICATION
    _In_ __CONST char   *Identifier,
#endif
    _In_ HeapBlock_t    *Block, 
    _In_ size_t          Length,
    _In_ size_t          Alignment)
{
    // Variables
    HeapNode_t *CurrNode    = Block->Nodes, 
               *PrevNode    = NULL;
    uintptr_t ReturnAddress = 0;

    // Basic algorithm for finding a spot, locked operation
    MutexLock(&Block->Lock);
    while (CurrNode) {
        if (CurrNode->Flags & NODE_ALLOCATED
            || CurrNode->Length < Length) {
            goto Skip;
        }

        // Allocate, two cases, either exact
        // match in size or we make a new header
        if (CurrNode->Length == Length
            || Block->Flags & BLOCK_VERY_LARGE) {
#ifdef HEAP_USE_IDENTIFICATION
            memcpy(&CurrNode->Identifier[0], Identifier == NULL ? GlbKernelUnknown : Identifier,
                Identifier == NULL ? strlen(GlbKernelUnknown) : strnlen(Identifier, HEAP_IDENTIFICATION_SIZE - 1));
            CurrNode->Identifier[HEAP_IDENTIFICATION_SIZE - 1] = '\0';
#endif
            // Update node information
            CurrNode->Flags = NODE_ALLOCATED;
            ReturnAddress = CurrNode->Address;
            Block->BytesFree -= Length;
            break;
        }
        else {
            HeapNode_t *hNode = NULL;
            if (Alignment != 0
                && !ISALIGNED(CurrNode->Address, Alignment)) {
                if (CurrNode->Length < ALIGN(Length, Alignment, 1)) {
                    goto Skip;
                }
            }

            // Get a new node, heap locked operation
            MutexLock(&Heap->Lock);
            if (Heap->NodeRecycler != NULL) {
                hNode = Heap->NodeRecycler;
                Heap->NodeRecycler = Heap->NodeRecycler->Link;
            }
            else {
                hNode = (HeapNode_t*)HeapAllocateInternal(Heap, sizeof(HeapNode_t));
            }

            // Make sure it didn't fail
            assert(hNode != NULL);
            MutexUnlock(&Heap->Lock);

            // Initialize the node
            memset(hNode, 0, sizeof(HeapNode_t));
#ifdef HEAP_USE_IDENTIFICATION
            memcpy(&hNode->Identifier[0], Identifier == NULL ? GlbKernelUnknown : Identifier,
                Identifier == NULL ? strlen(GlbKernelUnknown) : strnlen(Identifier, HEAP_IDENTIFICATION_SIZE - 1));
            hNode->Identifier[HEAP_IDENTIFICATION_SIZE - 1] = '\0';
#endif
            hNode->Address = CurrNode->Address;
            hNode->Flags = NODE_ALLOCATED;
            hNode->Link = CurrNode;

            // Handle allocation alignment
            if (Alignment != 0
                && !ISALIGNED(hNode->Address, Alignment)) {
                Length += Alignment - (hNode->Address % Alignment);
                hNode->Length = Length;
                ReturnAddress = ALIGN(hNode->Address, Alignment, 1);
            }
            else {
                hNode->Length = Length;
                ReturnAddress = hNode->Address;
            }
            CurrNode->Address = hNode->Address + Length;
            CurrNode->Length -= Length;

            // Update links
            if (PrevNode != NULL) PrevNode->Link = hNode;
            else Block->Nodes = hNode;
            break;
        }

    Skip:
        PrevNode = CurrNode;
        CurrNode = CurrNode->Link;
    }

    // Done
    MutexUnlock(&Block->Lock);
    return ReturnAddress;
}

/* HeapCommitPages
 * Helper for mapping in pages as soon as they
 * are allocated, used by allocation flags */
void
HeapCommitPages(
    _In_ uintptr_t  Address,
    _In_ size_t     Size,
    _In_ uintptr_t  Mask)
{
    // Variables
    uintptr_t PageMask = ~(AddressSpaceGetPageSize() - 1);
    size_t Pages    = DIVUP(Size, AddressSpaceGetPageSize());
    size_t i        = 0;

    // Sanitize the page boundary
    // Do we step across page boundary?
    if ((Address & PageMask) != ((Address + Size - 1) & PageMask)) {
        Pages++;
    }

    // Do the actual mapping
    for (; i < Pages; i++) {
        uintptr_t VirtualPage = Address + (i * AddressSpaceGetPageSize());
        if (!AddressSpaceGetMapping(AddressSpaceGetCurrent(), VirtualPage)) {
            AddressSpaceMap(AddressSpaceGetCurrent(), NULL, &VirtualPage, 
                AddressSpaceGetPageSize(), ASPACE_FLAG_SUPPLIEDVIRTUAL, Mask);
        }
    }
}

/* HeapAllocate
 * Finds a suitable block for allocation in the heap. Auto-expends if neccessary. */
uintptr_t
HeapAllocate(
    _In_ Heap_t         *Heap,
#ifdef HEAP_USE_IDENTIFICATION
    _In_ __CONST char   *Identifier,
#endif
    _In_ size_t          Length, 
    _In_ Flags_t         Flags,
    _In_ size_t          Alignment,
    _In_ uintptr_t       Mask)
{
    // Variables
    HeapBlock_t *CurrentBlock   = NULL;
    size_t AdjustedAlign        = Alignment;
    size_t AdjustedSize         = Length;
    uintptr_t RetVal            = 0;

    // Add some block-type flags 
    // based upon size of requested allocation 
    // we want to use our normal blocks for smaller allocations 
    // and the page-aligned for larger ones 
    if (ALLOCISNORMAL(Flags) && Length >= HEAP_NORMAL_BLOCK) {
        AdjustedSize = ALIGN(Length, AddressSpaceGetPageSize(), 1);
        Flags |= ALLOCATION_PAGEALIGN;
    }
    if (ALLOCISNOTBIG(Flags) && AdjustedSize >= HEAP_LARGE_BLOCK) {
        Flags |= ALLOCATION_BIG;
    }
    if (ALLOCISPAGE(Flags) && !ISALIGNED(AdjustedSize, AddressSpaceGetPageSize())) {
        AdjustedSize = ALIGN(AdjustedSize, AddressSpaceGetPageSize(), 1);
    }

    // Select the proper child list
    MutexLock(&Heap->Lock);
    if (Flags & ALLOCATION_BIG) {
        CurrentBlock = Heap->CustomBlocks;
    }
    else if (Flags & ALLOCATION_PAGEALIGN) {
        CurrentBlock = Heap->PageBlocks;
    }
    else {
        CurrentBlock = Heap->Blocks;
    }
    MutexUnlock(&Heap->Lock);
    while (CurrentBlock) {
        // Check which type of allocation is 
        // being made, we have three types: 
        // Standard-aligned allocations
        // Page-aligned allocations 
        // Custom allocations (large)

        // Sanitize both that enough space is free 
        // but ALSO that the block has higher mask
        if ((CurrentBlock->BytesFree < AdjustedSize)
            || (CurrentBlock->Mask < Mask)) {
            goto Skip;
        }

        // Try to make an allocation, if it fails it returns 0
        RetVal = HeapAllocateSizeInBlock(Heap, IDENTIFIER CurrentBlock, 
            AdjustedSize, AdjustedAlign);
        if (RetVal != 0) {
            break;
        }
    Skip:
        MutexLock(&Heap->Lock);
        CurrentBlock = CurrentBlock->Link;
        MutexUnlock(&Heap->Lock);
    }

    // Sanitize
    // If return value is not 0 it means our allocation was made!
    if (RetVal != 0) {
        if (Flags & ALLOCATION_COMMIT) {
            HeapCommitPages(RetVal, AdjustedSize, Mask);
        }
        if (Flags & ALLOCATION_ZERO) {
            memset((void*)RetVal, 0, Length);
        }
        return RetVal;
    }

    // We reach this point if no available block was made, expand our
    // heap and retry the allocation
    HeapExpand(Heap, IDENTIFIER AdjustedSize, Mask, Flags);
    return HeapAllocate(Heap, IDENTIFIER AdjustedSize, Flags, AdjustedAlign, Mask);
}

/* HeapFreeAddressInNode
 * Helper for the primary freeing routine, it free's 
 * an address in the correct block, it's a bit more complicated
 * as it supports merging with sibling nodes  */
OsStatus_t
HeapFreeAddressInNode(
    _In_ Heap_t         *Heap,
    _In_ HeapBlock_t    *Block,
    _In_ uintptr_t       Address)
{
    // Variables
    HeapNode_t *CurrNode    = Block->Nodes, 
               *PrevNode    = NULL;
    OsStatus_t Result       = OsError;

    // Basic algorithm for freeing
    MutexLock(&Block->Lock);
    while (CurrNode != NULL) {
        uintptr_t aStart    = CurrNode->Address;
        uintptr_t aEnd      = CurrNode->Address + CurrNode->Length;

        if (Address >= aStart && Address < aEnd) {
            CurrNode->Flags = 0;
            Block->BytesFree += CurrNode->Length;
            Result = OsSuccess;

            // Can we merge?
            if (PrevNode == NULL && CurrNode->Link == NULL) {
                break;
            }

            // Try to merge with previous header
            if (PrevNode != NULL && !(PrevNode->Flags & NODE_ALLOCATED)) {
                PrevNode->Length += CurrNode->Length;
                PrevNode->Link = CurrNode->Link;
                
                // Recycle
                CurrNode->Link = NULL;
                CurrNode->Address = 0;
                CurrNode->Length = 0;
                MutexLock(&Heap->Lock);
                if (Heap->NodeRecycler == NULL)
                    Heap->NodeRecycler = CurrNode;
                else {
                    CurrNode->Link = Heap->NodeRecycler;
                    Heap->NodeRecycler = CurrNode;
                }
                MutexUnlock(&Heap->Lock);
            }
            // Merge with our link
            else if (CurrNode->Link != NULL
                && !(CurrNode->Flags & NODE_ALLOCATED)) {
                CurrNode->Link->Address = CurrNode->Address;
                CurrNode->Link->Length += CurrNode->Length;

                // Unlink our node
                if (PrevNode == NULL) {
                    Block->Nodes = CurrNode->Link;
                }
                else {
                    PrevNode->Link = CurrNode->Link;
                }

                // Recycle header
                CurrNode->Link = NULL;
                CurrNode->Address = 0;
                CurrNode->Length = 0;
                MutexLock(&Heap->Lock);
                if (Heap->NodeRecycler == NULL) {
                    Heap->NodeRecycler = CurrNode;
                }
                else {
                    CurrNode->Link = Heap->NodeRecycler;
                    Heap->NodeRecycler = CurrNode;
                }
                MutexUnlock(&Heap->Lock);
            }
            break;
        }

        // Next node
        PrevNode = CurrNode;
        CurrNode = CurrNode->Link;
    }

    // Unlock and return
    MutexUnlock(&Block->Lock);
    return Result;
}

/* HeapFreeLocator
 * Helper for locating the correct block, as we have
 * three different lists with blocks in, not very quick
 * untill we convert it to a binary tree */
HeapBlock_t*
HeapFreeLocator(
    _In_ HeapBlock_t    *List,
    _In_ uintptr_t       Address)
{
    // Variables
    HeapBlock_t *Block = List;
    while (Block) {
        if (Block->BaseAddress <= Address
            && Block->EndAddress > Address) {
            return Block;
        }
        Block = Block->Link;
    }
    return NULL;
}

/* HeapFree
 * Unallocates the header that contains the address. */
OsStatus_t
HeapFree(
    _In_ Heap_t     *Heap,
    _In_ uintptr_t   Address)
{
    // Variables
    HeapBlock_t *Block = NULL;

    // Locate the relevant block, unlocked
    Block = HeapFreeLocator(Heap->Blocks, Address);
    if (Block == NULL) {
        Block = HeapFreeLocator(Heap->PageBlocks, Address);
    }
    if (Block == NULL) {
        Block = HeapFreeLocator(Heap->CustomBlocks, Address);
    }

    // Sanitize
    if (Block == NULL) {
        return OsError;
    }
    else {
        return HeapFreeAddressInNode(Heap, Block, Address);
    }
}

/* HeapQueryAddressInNode
 * This function is basically just a node-lookup function
 * that's used to find information about the node */
HeapNode_t*
HeapQueryAddressInNode(
    HeapBlock_t     *Block,
    uintptr_t        Address)
{
    // Variables
    HeapNode_t *CurrNode = Block->Nodes, 
               *PrevNode = NULL;

    // Iterate nodes in the block
    MutexLock(&Block->Lock);
    while (CurrNode != NULL) {
        uintptr_t aStart    = CurrNode->Address;
        uintptr_t aEnd      = CurrNode->Address + CurrNode->Length;
        if (aStart <= Address && aEnd >= Address) {
            break;
        }
        PrevNode = CurrNode;
        CurrNode = CurrNode->Link;
    }

    // Unlock and return result
    MutexUnlock(&Block->Lock);
    return CurrNode;
}

/* HeapQuery
 * Finds the appropriate block that should contain requested node */
HeapNode_t*
HeapQuery(
    _In_ Heap_t     *Heap, 
    _In_ uintptr_t   Address)
{
    // Variables
    HeapBlock_t *CurrBlock = Heap->Blocks;
    while (CurrBlock) {
        if (CurrBlock->BaseAddress <= Address
            && CurrBlock->EndAddress > Address) {
            return HeapQueryAddressInNode(CurrBlock, Address);
        }
        CurrBlock = CurrBlock->Link;
    }
    return NULL;
}

/* HeapQueryMemoryInformation
 * Queries memory information about the given heap. */
OsStatus_t
HeapQueryMemoryInformation(
    _In_ Heap_t *Heap,
    _Out_ size_t *BytesInUse,
    _Out_ size_t *BlocksAllocated)
{
    // Variables
    HeapBlock_t *CurrentBlock   = NULL, 
                *PreviousBlock  = NULL;
    size_t BytesAllocated       = 0;
    size_t NodesAllocated       = 0;
    size_t NodeCount            = 0;

    // Sanitize input
    if (Heap == NULL) {
        return OsError;
    }

    // Calculate statistics
    CurrentBlock = Heap->Blocks, PreviousBlock = NULL;
    while (CurrentBlock) {
        HeapNode_t *CurrentNode = CurrentBlock->Nodes, *PreviousNode = NULL;
        while (CurrentNode) {
            NodesAllocated = (CurrentNode->Flags & NODE_ALLOCATED) ? NodesAllocated + 1 : NodesAllocated;
            BytesAllocated = (CurrentNode->Flags & NODE_ALLOCATED) ? (BytesAllocated + CurrentNode->Length) : BytesAllocated;
            NodeCount++;

            PreviousNode = CurrentNode;
            CurrentNode = CurrentNode->Link;
        }
        PreviousBlock = CurrentBlock;
        CurrentBlock = CurrentBlock->Link;
    }

    // Update outs and return
    if (BytesInUse != NULL) {
        *BytesInUse = BytesAllocated;
    }
    if (BlocksAllocated != NULL) {
        *BlocksAllocated = NodesAllocated;
    }
    return OsSuccess;
}

/* HeapConstruct
 * Constructs a new heap, the structure must be pre-allocated
 * or static. Use this for creating the system heap. */
OsStatus_t
HeapConstruct(
    _In_ Heap_t     *Heap,
    _In_ uintptr_t   BaseAddress,
    _In_ uintptr_t   EndAddress,
    _In_ int         UserHeap)
{
    // Variables
#ifdef HEAP_USE_IDENTIFICATION
    const char *Identifier  = GlbKernelUnknown;
#endif
    size_t PercentageStep   = 0;

    // Calculate header area
    PercentageStep = ((EndAddress - BaseAddress) / HEAP_HEADER_MEMORYDIVISOR);
    PercentageStep *= HEAP_HEADER_MEMORYSIZE;
    PercentageStep += 0x1000 - (PercentageStep % 0x1000); // Round up

    // Debug
    TRACE("HeapConstruct(Base 0x%x, End 0x%x, HeaderArea 0x%x)", 
        BaseAddress, EndAddress, PercentageStep);

    // Zero out the heap
    memset(Heap, 0, sizeof(Heap_t));

    // Initialize members to default values
    MutexConstruct(&Heap->Lock);
    Heap->BaseHeaderAddress = Heap->HeaderCurrent = BaseAddress;
    Heap->EndAddress = EndAddress;
    Heap->BaseDataAddress = Heap->DataCurrent = BaseAddress + PercentageStep;
    Heap->IsUser = UserHeap;

    // Create default blocks
    Heap->Blocks = HeapCreateBlock(Heap, IDENTIFIER HEAP_NORMAL_BLOCK, 
        __MASK,  BLOCK_NORMAL);
    Heap->PageBlocks = HeapCreateBlock(Heap, IDENTIFIER HEAP_LARGE_BLOCK,
        __MASK, BLOCK_ALIGNED);
    Heap->CustomBlocks = NULL;

    // No errors
    return OsSuccess;
}

/* HeapDestroy
 * Destroys a heap, frees all memory allocated and pages. */
OsStatus_t
HeapDestroy(
    _In_ Heap_t *Heap)
{
    // @todo
    return OsError;
}

/* HeapMaintain
 * Maintaining procedure cleaning up un-used pages to the system.
 * Should be called once in a while. Returns number of bytes freed. */
size_t
HeapMaintain(
    _In_ Heap_t *Heap)
{
    // @todo
    return 0;
}

/* HeapValidateAddress
 * Used for validation that an address is allocated within the given heap, 
 * this can be used for security or validation purposes. */
OsStatus_t
HeapValidateAddress(
    _In_ Heap_t     *Heap,
    _In_ uintptr_t   Address)
{
    // Variables
    HeapNode_t *Node = NULL;

    // Sanitize input
    if (Heap == NULL) {
        return OsError;
    }

    // Lookup node, if it exists and is allocated
    // then all is good
    Node = HeapQuery(Heap, Address);
    if (Node == NULL || !(Node->Flags & NODE_ALLOCATED)) {
        return OsError;
    }
    return OsSuccess;
}

/* HeapGetKernel
 * Retrieves a pointer to the static system heap. */
Heap_t*
HeapGetKernel(void)
{
    return &GlbKernelHeap;
}

/* k(ernel) memory allocator
 * There are several variants of the allocator, the keywords are:
 * <a> - (align) page-aligned allocation
 * <p> - (physical) returns the physical address of the allocation
 * <m> - (mask) a memory mask for allowing physical addresses that fit the mask. */
void*
kmalloc_base(
    _In_ size_t      Length,
    _In_ Flags_t     Flags,
    _In_ size_t      Alignment,
    _In_ uintptr_t   Mask,
    _Out_ uintptr_t *PhysicalAddress)
{
    // Variables
#ifdef HEAP_USE_IDENTIFICATION
    const char *Identifier  = GlbKernelUnknown;
#endif
    uintptr_t ReturnAddress = 0;

    // Fail on 0 allocations
    if (Length == 0) {
        FATAL(FATAL_SCOPE_KERNEL, "Allocation of 0");
        return NULL;
    }

    // Perform the allocation
    ReturnAddress = HeapAllocate(HeapGetKernel(), IDENTIFIER Length, Flags, Alignment, Mask);

    // Fail on NULL allocations
    if (ReturnAddress == 0) {
        FATAL(FATAL_SCOPE_KERNEL, "Allocation result 0x%x", ReturnAddress);
        return NULL;
    }

    // Check alignment
    if ((Flags & ALLOCATION_PAGEALIGN) && ((ReturnAddress & (AddressSpaceGetPageSize() - 1)) != 0)) {
        FATAL(FATAL_SCOPE_KERNEL, "Allocation result 0x%x (alignment failed)", ReturnAddress);
        return NULL;
    } 

    // Lookup physical
    if (PhysicalAddress != NULL) {
        *PhysicalAddress =  AddressSpaceGetMapping(AddressSpaceGetCurrent(), ReturnAddress);
    }
    return (void*)ReturnAddress;
}

void*
kmalloc_apm(
    _In_ size_t      Length,
    _In_ uintptr_t   Mask,
    _Out_ uintptr_t *PhysicalAddress) {
    return kmalloc_base(Length, ALLOCATION_COMMIT | ALLOCATION_PAGEALIGN, 
        0, Mask, PhysicalAddress);
}

void*
kmalloc_ap(
    _In_ size_t      Length,
    _Out_ uintptr_t *PhysicalAddress) {
    return kmalloc_base(Length, ALLOCATION_COMMIT | ALLOCATION_PAGEALIGN, 0, __MASK, PhysicalAddress);
}

void*
kmalloc_p(
    _In_ size_t      Length,
    _Out_ uintptr_t *PhysicalAddress) {
    return kmalloc_base(Length, ALLOCATION_COMMIT, HEAP_STANDARD_ALIGN, __MASK, PhysicalAddress);
}

void*
kmalloc_a(
    _In_ size_t      Length) {
    return kmalloc_base(Length, ALLOCATION_COMMIT | ALLOCATION_PAGEALIGN, 0, __MASK, NULL);
}

void*
kmalloc(
    _In_ size_t      Length) {
    return kmalloc_base(Length, ALLOCATION_COMMIT, HEAP_STANDARD_ALIGN, __MASK, NULL);
}

/* kfree 
 * Wrapper for the HeapFree that essentially 
 * just calls it with the kernel heap as argument */
void
kfree(
    _In_ void *Pointer) {
    assert(Pointer != NULL);
    HeapFree(HeapGetKernel(), (uintptr_t)Pointer);
}

/**************************************/
/******** Heap Testing Suite  *********/
/**************************************/
void heap_test(void)
{
    /* Do some debugging */
    uintptr_t phys1 = 0, phys2 = 0, i = 0;
    void *res1, *res2, *res3, *res4, *res5, *res6;

    HeapStatisticsPrint(&GlbKernelHeap);

    printf(" >> Performing small allocs & frees (a few)\n");
    res1 = kmalloc(0x30);
    res2 = kmalloc(0x50);
    res3 = kmalloc(0x130);
    res4 = kmalloc(0x180);
    res5 = kmalloc(0x600);
    res6 = kmalloc(0x3000);

    HeapStatisticsPrint(&GlbKernelHeap);

    printf(" Alloc1 (0x30): 0x%x, Alloc2 (0x50): 0x%x, Alloc3 (0x130): 0x%x, Alloc4 (0x180): 0x%x\n",
        (uintptr_t)res1, (uintptr_t)res2, (uintptr_t)res3, (uintptr_t)res4);
    printf(" Alloc5 (0x600): 0x%x, Alloc6 (0x3000): 0x%x\n", (uintptr_t)res5, (uintptr_t)res6);

    printf(" Freeing Alloc5, 2 & 3...\n");
    kfree(res5);
    kfree(res2);
    kfree(res3);

    HeapStatisticsPrint(&GlbKernelHeap);

    printf(" Re-allocing 5, 2 & 3\n");
    res2 = kmalloc(0x90);
    res3 = kmalloc(0x20);
    res5 = kmalloc(0x320);

    printf(" Alloc2 (0x90): 0x%x, Alloc3 (0x20): 0x%x, Alloc5 (0x320): 0x%x\n", (uintptr_t)res2, (uintptr_t)res3, (uintptr_t)res5);
    printf(" Freeing all...\n");

    kfree(res1);
    kfree(res2);
    kfree(res3);
    kfree(res4);
    kfree(res5);
    kfree(res6);

    HeapStatisticsPrint(&GlbKernelHeap);

    printf(" Making special allocations (aligned & aligned /w phys)\n");
    res1 = kmalloc_a(0x30);
    res2 = kmalloc_a(0x210);
    res3 = kmalloc_a(0x900);
    res4 = kmalloc_a(0x4500);

    res5 = kmalloc_ap(0x32, &phys1);
    res6 = kmalloc_ap(0x1000, &phys2);

    printf(" Alloc1 (0x30): 0x%x, Alloc2 (0x210): 0x%x, Alloc3 (0x900): 0x%x, Alloc4 (0x4500): 0x%x\n",
        (uintptr_t)res1, (uintptr_t)res2, (uintptr_t)res3, (uintptr_t)res4);
    printf(" Alloc5 (0x32): 0x%x, Alloc6 (0x1000): 0x%x\n", (uintptr_t)res5, (uintptr_t)res6);
    printf(" Alloc5 Physical: 0x%x, Alloc6 Physical: 0x%x\n", phys1, phys2);

    printf(" Freeing all...\n");
    kfree(res1);
    kfree(res2);
    kfree(res3);
    kfree(res4);
    kfree(res5);
    kfree(res6);

    HeapStatisticsPrint(&GlbKernelHeap);

    printf(" >> Performing allocations (150)\n");
    
    for (i = 0; i < 50; i++)
    {
        kmalloc(0x50);
        kmalloc(0x180);
        kmalloc(0x3000);
    }

    HeapStatisticsPrint(&GlbKernelHeap);
    
    printf(" >> Performing allocs & frees (150)\n");
    for (i = 0; i < 50; i++)
    {
        void* r1 = kmalloc(0x30);
        void* r2 = kmalloc(0x130);
        void* r3 = kmalloc(0x600);

        kfree(r1);
        kfree(r2);
        kfree(r3);
    }
    
    //Done
    printf(" >> Memory benchmarks are done!\n");

}
