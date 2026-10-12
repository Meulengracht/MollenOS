/**
 * Copyright 2021, Philip Meulengracht
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
 * MollenOS System Component Infrastructure
 * - The Memory component. This component has the task of managing
 *   different memory regions that map to physical components
 */

#include <os/osdefs.h>
#include <limits.h>
#include <string.h>
#include <utils/memory_stack.h>

struct MemoryStackItem {
    uintptr_t Base;
    size_t    BlockCount;
};

size_t
MemoryStackCalculateSize(
    _In_ size_t blockCount)
{
    size_t capacity = blockCount / 2 + blockCount % 2;

    // Alternating free and allocated blocks require the most free ranges.
    if (capacity > INT_MAX || capacity > SIZE_MAX / sizeof(struct MemoryStackItem)) {
        return 0;
    }
    return capacity * sizeof(struct MemoryStackItem);
}

oserr_t
MemoryStackConstruct(
    _In_ MemoryStack_t* stack,
    _In_ size_t         blockSize,
    _In_ uintptr_t      dataAddress,
    _In_ size_t         dataSize)
{
    size_t capacity = dataSize / sizeof(struct MemoryStackItem);

    if (stack == NULL || dataAddress == 0 || !IsPowerOfTwo(blockSize)) {
        return OS_EINVALPARAMS;
    }
    
    if (dataAddress % _Alignof(struct MemoryStackItem) != 0 || capacity == 0) {
        return OS_EINVALPARAMS;
    }
    
    if (capacity > INT_MAX) {
        return OS_ENOTSUPPORTED;
    }
    
    stack->index = 0;
    stack->capacity = (int)capacity;
    stack->items = (struct MemoryStackItem*)dataAddress;
    stack->block_size = blockSize;
    stack->data_size = dataSize;
    return OS_EOK;
}

void
MemoryStackRelocate(
    _In_ MemoryStack_t* stack,
    _In_ uintptr_t      address)
{
    stack->items = (struct MemoryStackItem*)address;
}

static void
__RemoveEntry(
    _In_ MemoryStack_t* stack,
    _In_ int            index)
{
    memmove(&stack->items[index], &stack->items[index + 1],
            (size_t)(stack->index - index - 1) * sizeof(*stack->items));
    stack->index--;
}

static void
__InsertEntry(
    _In_ MemoryStack_t* stack,
    _In_ int            index,
    _In_ uintptr_t      base,
    _In_ size_t         count)
{
    memmove(
        &stack->items[index + 1],
        &stack->items[index],
        (size_t)(stack->index - index) * sizeof(*stack->items)
    );
    stack->items[index].Base = base;
    stack->items[index].BlockCount = count;
    stack->index++;
}

// Find the first free range whose base is at least address.
static int
__LowerBound(
    _In_ const MemoryStack_t* stack,
    _In_ uintptr_t            address)
{
    int low = 0;
    int high = stack->index;
    int middle;

    while (low < high) {
        middle = low + (high - low) / 2;
        if (stack->items[middle].Base < address) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return low;
}

// Free ranges are kept in address order and never overlap. This lets a push
// check only the ranges immediately before and after the new range, then
// combine any ranges that touch it instead of storing separate entries.
oserr_t
MemoryStackPush(
    _In_ MemoryStack_t* stack,
    _In_ uintptr_t      address,
    _In_ int            blockCount)
{
    struct MemoryStackItem* previous;
    struct MemoryStackItem* next;
    size_t                  count;
    size_t                  distance;
    bool                    joinPrevious = false;
    bool                    joinNext = false;
    int                     index;

    if (stack == NULL || blockCount <= 0) {
        return OS_EINVALPARAMS;
    }
    
    count = (size_t)blockCount;
    // Require block alignment and make sure the last block stays in the
    // address space. Checking the last block, rather than an exclusive end,
    // also permits a range that includes the highest addressable block.
    if (address % stack->block_size != 0 || count - 1 > (UINTPTR_MAX - address) / stack->block_size) {
        return OS_EINVALPARAMS;
    }
    
    // Find where this range belongs. Because entries are ordered, these are
    // the only existing ranges that can overlap it or touch either end.
    index = __LowerBound(stack, address);
    previous = index != 0 ? &stack->items[index - 1] : NULL;
    next = index != stack->index ? &stack->items[index] : NULL;

    // Compare block distances instead of end addresses, allowing the final
    // block of the physical address space without wrapping an exclusive end.
    if (previous != NULL) {
        distance = (address - previous->Base) / stack->block_size;
        // A shorter distance means the new range starts inside the previous
        // range; an equal distance means the two ranges can be combined.
        if (distance < previous->BlockCount) {
            return OS_EINVALPARAMS;
        }
        joinPrevious = distance == previous->BlockCount;
    }
    if (next != NULL) {
        distance = (next->Base - address) / stack->block_size;
        // If the next range starts before this range ends, the ranges overlap.
        // If it starts exactly at the end, the ranges can be combined.
        if (distance < count) {
            return OS_EINVALPARAMS;
        }
        joinNext = distance == count;
    }
    if (joinPrevious) {
        // Check each addition before combining counts so a large range cannot
        // wrap around and appear smaller than it really is.
        if (count > SIZE_MAX - previous->BlockCount) {
            return OS_ENOTSUPPORTED;
        }
        count += previous->BlockCount;
    }
    if (joinNext) {
        if (count > SIZE_MAX - next->BlockCount) {
            return OS_ENOTSUPPORTED;
        }
        count += next->BlockCount;
    }

    // Prefer extending an existing entry, which needs no spare metadata. If
    // the new range touches both sides, extending the previous entry bridges
    // the gap and the now-redundant next entry is removed.
    if (joinPrevious) {
        previous->BlockCount = count;
        if (joinNext) {
            __RemoveEntry(stack, index);
        }
    } else if (joinNext) {
        next->Base = address;
        next->BlockCount = count;
    } else {
        // No neighboring range can be extended, so a new entry is required.
        if (stack->index == stack->capacity) {
            return OS_EOOM;
        }
        __InsertEntry(stack, index, address, count);
    }
    return OS_EOK;
}

// Remove a known subrange, preserving both free sides when necessary.
static oserr_t
__TakeRange(
    _In_ MemoryStack_t* stack,
    _In_ int            index,
    _In_ uintptr_t      address,
    _In_ size_t         count)
{
    struct MemoryStackItem* item = &stack->items[index];
    size_t                  prefix = (address - item->Base) / stack->block_size;
    size_t                  suffix = item->BlockCount - prefix - count;

    if (prefix != 0 && suffix != 0) {
        // If both prefix and suffix are non-zero, we need to split the block into two.
        if (stack->index == stack->capacity) {
            return OS_EOOM;
        }

        // First, reduce the current item's block count to the prefix size.
        item->BlockCount = prefix;
        // Then, insert a new entry for the suffix part.
        __InsertEntry(stack, index + 1, address + count * stack->block_size, suffix);
    } else if (prefix != 0) {
        // If only the prefix is non-zero, we just reduce the current item's block count to the prefix size.
        item->BlockCount = prefix;
    } else if (suffix != 0) {
        // If only the suffix is non-zero, we adjust the base and block count accordingly.
        item->Base = address + count * stack->block_size;
        item->BlockCount = suffix;
    } else {
        // If both prefix and suffix are zero, the entire block is being taken, so we remove the entry.
        __RemoveEntry(stack, index);
    }
    return OS_EOK;
}

oserr_t
MemoryStackPushMultiple(
    _In_ MemoryStack_t*   stack,
    _In_ const uintptr_t* blocks,
    _In_ int              blockCount)
{
    if (stack == NULL || blocks == NULL || blockCount <= 0) {
        return OS_EINVALPARAMS;
    }
    
    for (int inserted = 0; inserted < blockCount; inserted++) {
        oserr_t status;
        
        status = MemoryStackPush(stack, blocks[inserted], 1);
        if (status == OS_EOK) {
            // On success, continue to the next block.
            continue;
        }

        // Reverse each successful insertion. Undoing the most recent
        // change only needs the capacity that existed before that change.
        while (inserted != 0) {
            int index;

            inserted--;
            index = __LowerBound(stack, blocks[inserted]);
            if (index == stack->index || stack->items[index].Base != blocks[inserted]) {
                index--;
            }
            (void)__TakeRange(stack, index, blocks[inserted], 1);
        }
        return status;
    }
    return OS_EOK;
}

oserr_t
MemoryStackPop(
    _In_    MemoryStack_t* stack,
    _InOut_ int*           blockCount,
    _Out_   uintptr_t*     blocks)
{
    int written = 0;
    int remaining;

    if (stack == NULL || blockCount == NULL || blocks == NULL) {
        return OS_EINVALPARAMS;
    }
    if (*blockCount <= 0) {
        return OS_EINVALPARAMS;
    }
    if (stack->index == 0) {
        return OS_EOOM;
    }
    
    remaining = *blockCount;
    while (remaining != 0 && stack->index != 0) {
        struct MemoryStackItem* item = &stack->items[stack->index - 1];
        size_t                  take;
        uintptr_t               address;
        
        // Determine how many blocks to take from the current item.
        take = item->BlockCount < (size_t)remaining ? item->BlockCount : (size_t)remaining;
        // Calculate the starting address for the blocks to be taken.
        address = item->Base + (item->BlockCount - take) * stack->block_size;

        // Copy the blocks to the output array.
        for (size_t i = 0; i < take; i++) {
            blocks[written++] = address + i * stack->block_size;
        }

        // Update the block count for the current item and remove it if empty.
        item->BlockCount -= take;
        if (item->BlockCount == 0) {
            stack->index--;
        }
        remaining -= (int)take;
    }
    *blockCount = written;
    return remaining == 0 ? OS_EOK : OS_EINCOMPLETE;
}

oserr_t
MemoryStackPopContiguous(
    _In_  MemoryStack_t* stack,
    _In_  size_t         length,
    _In_  size_t         alignment,
    _In_  size_t         boundary,
    _Out_ uintptr_t*     addressOut)
{
    size_t count;

    if (stack == NULL || addressOut == NULL || length == 0) {
        return OS_EINVALPARAMS;
    }
    if (alignment != 0 && !IsPowerOfTwo(alignment)) {
        return OS_EINVALPARAMS;
    }
    if (boundary != 0 && (!IsPowerOfTwo(boundary) || length > boundary)) {
        return OS_EINVALPARAMS;
    }
    if (length > SIZE_MAX - (stack->block_size - 1)) {
        return OS_EINVALPARAMS;
    }
    
    count = DIVUP(length, stack->block_size);
    if (alignment < stack->block_size) {
        alignment = stack->block_size;
    }

    for (int i = 0; i < stack->index; i++) {
        struct MemoryStackItem* item = &stack->items[i];
        uintptr_t               candidate = item->Base;
        size_t                  offset;
        size_t                  delta;
        oserr_t                 status;

        // Calculate the alignment adjustment for the candidate address, we will
        // need to account for the alignment when allocating the blocks.
        delta = (alignment - (candidate & (alignment - 1))) & (alignment - 1);
        if (delta > UINTPTR_MAX - candidate) {
            continue;
        }

        // Apply the alignment adjustment to the candidate address.
        candidate += delta;
        if (boundary != 0) {
            // For example, with 0x100-byte blocks and a free range from 0x1F800
            // through 0x217FF, requesting length 0x900, alignment 0x100, and
            // boundary 0x1000 returns 0x20000: starting at 0x1F800 would cross
            // the 0x20000 boundary. Without the boundary constraint, it returns
            // 0x1F800. The boundary-constrained allocation requires metadata room
            // to preserve the free prefix and suffix.
            offset = candidate & (boundary - 1);

            if (offset > boundary - length) {
                // The candidate address crosses the boundary, so we need to advance it to the next boundary.
                delta = boundary - offset;
                if (delta > UINTPTR_MAX - candidate) {
                    continue;
                }
                // Both constraints are powers of two. Crossing is possible
                // only when alignment is smaller than boundary, so advancing
                // to the next boundary preserves the required alignment.
                candidate += delta;
            }
        }

        offset = (candidate - item->Base) / stack->block_size;
        if (offset > item->BlockCount || count > item->BlockCount - offset) {
            continue;
        }

        // Try to take the range of blocks starting at the candidate address,
        // otherwise continue searching for a suitable range.
        status = __TakeRange(stack, i, candidate, count);
        if (status == OS_EOK) {
            *addressOut = candidate;
            return OS_EOK;
        }
    }
    return OS_EOOM;
}

size_t
MemoryStackFreeBlockCount(
    _In_ const MemoryStack_t* stack)
{
    size_t count = 0;

    for (int i = 0; i < stack->index; i++) {
        count += stack->items[i].BlockCount;
    }
    return count;
}
