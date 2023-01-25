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

//#define __TRACE

#define __need_minmax
#include <assert.h>
#include <debug.h>
#include <memoryspace.h>
#include <utils/memory_stack.h>
#include "string.h"

struct MemoryStackItem {
    uintptr_t base;
    int       block_count;
};

void
MemoryStackConstruct(
        _In_ MemoryStack_t* stack,
        _In_ size_t         blockSize,
        _In_ uintptr_t      dataAddress,
        _In_ size_t         dataSize)
{
    assert(stack != NULL);
    assert(blockSize != 0);
    assert(dataSize != 0);

    stack->index      = 0;
    stack->capacity   = (int)(dataSize / sizeof(struct MemoryStackItem));
    stack->items      = (struct MemoryStackItem*)dataAddress;
    stack->block_size = blockSize;
    stack->data_size  = dataSize;
}

static void
__ExtendStack(
        _In_ MemoryStack_t* stack)
{
    // double up each time
    oserr_t   oserr;
    size_t    newSize = stack->data_size << 1;
    size_t    pageCount = newSize / GetMemorySpacePageSize();
    vaddr_t   space;
    uintptr_t pages[pageCount];

    oserr = MemorySpaceMap(
            GetCurrentMemorySpace(),
            &(struct MemorySpaceMapOptions) {
                .Pages = &pages[0],
                .Length = newSize,
                .Mask = __MASK,
                .Flags = MAPPING_COMMIT | MAPPING_DOMAIN,
                .PlacementFlags = MAPPING_VIRTUAL_GLOBAL
            },
            &space
    );
    assert(oserr == OS_EOK);

    // copy the data
    memcpy((void*)space, stack->items, stack->data_size);

    // free the old space
    oserr = MemorySpaceUnmap(
            GetCurrentMemorySpace(),
            (vaddr_t)stack->items,
            stack->data_size
    );
    assert(oserr == OS_EOK);

    // update the stack with new space, capacity and pointer
    stack->capacity = (int)(newSize / sizeof(struct MemoryStackItem));
    stack->data_size = newSize;
    stack->items = (struct MemoryStackItem*)space;
}

void
MemoryStackRelocate(
        _In_ MemoryStack_t* stack,
        _In_ uintptr_t      address)
{
    assert(stack != NULL);

    // We simply just update the address pointer. This function is only here
    // because the OS needs to relocate the stack to a virtual address instead
    // of having this stack identity mapped.
    stack->items = (struct MemoryStackItem*)address;
}

void
MemoryStackPush(
        _In_ MemoryStack_t* stack,
        _In_ uintptr_t      address,
        _In_ int            blockCount)
{
    uintptr_t lastAddress;

    // can we extend the last entry instead of going to next
    if (stack->index != 0) {
        lastAddress = stack->items[stack->index - 1].base + (stack->items[stack->index - 1].block_count * stack->block_size);
        if (lastAddress == address) {
            stack->items[stack->index - 1].block_count += blockCount;
            return;
        }
    }

    if (stack->index == stack->capacity) {
        __ExtendStack(stack);
    }

    stack->items[stack->index].base = address;
    stack->items[stack->index].block_count = blockCount;
    stack->index++;
}

void
MemoryStackPushMultiple(
        _In_ MemoryStack_t*   stack,
        _In_ const uintptr_t* blocks,
        _In_ int              blockCount)
{
    uintptr_t lastAddress;
    int       blocksLeft = blockCount;
    int       j          = (blockCount - 1);

    assert(stack != NULL);
    assert(blockCount != 0);

    while (blocksLeft) {
        // can we extend the last entry instead of going to next
        if (stack->index != 0) {
            lastAddress = stack->items[stack->index - 1].base + (stack->items[stack->index - 1].block_count * stack->block_size);
            if (lastAddress == blocks[j]) {
                stack->items[stack->index - 1].block_count++;
                blocksLeft--;
                j--;
                continue;
            }
        }

        if (stack->index == stack->capacity) {
            __ExtendStack(stack);
        }

        stack->items[stack->index].base = blocks[j--];
        stack->items[stack->index].block_count = 1;
        stack->index++;
    }
}

oserr_t
MemoryStackPop(
        _In_ MemoryStack_t* stack,
        _In_ int*           blockCount,
        _In_ uintptr_t*     blocks)
{
    int blocksLeft = *blockCount;
    int i, j = 0;
    TRACE("MemoryStackPop(stack=0x%" PRIxIN ", blockCount=%i)",
          stack, *blockCount);

    assert(stack != NULL);
    assert(blocksLeft != 0);

    if (!stack->index) {
        return OS_EOOM;
    }

    // check if we can remove from the last entry instead of removing an entire
    // entry in the stack.
    while (blocksLeft) {
        int       blocksToTake = MIN(stack->items[stack->index - 1].block_count, blocksLeft);
        size_t    offset  = (stack->items[stack->index - 1].block_count - blocksToTake) * stack->block_size;
        uintptr_t address = stack->items[stack->index - 1].base + offset;
        TRACE("MemoryStackPop items[i-1].block_count=%i items[i-1].base=0x%" PRIxIN,
              stack->items[stack->index - 1].block_count, stack->items[stack->index - 1].base);
        TRACE("MemoryStackPop blocksToTake=%i, offset=0x%" PRIxIN ", address=0x%" PRIxIN,
              blocksToTake, offset, address);
        for (i = 0; i < blocksToTake; i++, address += stack->block_size) {
            blocks[j++] = address;
        }

        // adjust remaining block count
        blocksLeft -= blocksToTake;

        // go to next block if we took all
        if (blocksToTake == stack->items[stack->index - 1].block_count) {
            stack->index--;
            if (!stack->index && blocksLeft) {
                *blockCount = j;
                return OS_EINCOMPLETE;
            }
        }
        else {
            // reduce the block count
            stack->items[stack->index - 1].block_count -= blocksToTake;
        }
    }

    return OS_EOK;
}
