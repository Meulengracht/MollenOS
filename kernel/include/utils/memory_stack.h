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

#ifndef __UTILS_MEMORYSTACK_H__
#define __UTILS_MEMORYSTACK_H__

#include <os/osdefs.h>

struct MemoryStackItem;

typedef struct {
    int                     capacity;
    int                     index;
    size_t                  block_size;
    size_t                  data_size;
    struct MemoryStackItem* items;
} MemoryStack_t;

/**
 * @brief Calculate storage for tracking free blocks in a memory range.
 * 
 * @param blockCount Total number of blocks in the range, whether free or in use.
 * @return Bytes needed for the largest number of free ranges this range can
 *         contain, or zero if the count is zero or the size cannot be represented.
 *         If one stack tracks separate ranges of memory, add the sizes needed
 *         for each range. Less storage may work, but adding or splitting free
 *         ranges can then fail with OS_EOOM.
 */
KERNELAPI size_t KERNELABI
MemoryStackCalculateSize(
    _In_ size_t blockCount);

/**
 * @brief Set up an empty stack using storage provided by the caller.
 * 
 * @param stack Stack to set up; unchanged if setup fails.
 * @param blockSize Size of each block in bytes; must be a power of two.
 * @param dataAddress Address of the storage for free-range records; must have
 *                    the alignment required by those records.
 * @param dataSize Size of that storage in bytes; it must fit at least one record.
 * @return OS_EOK, OS_EINVALPARAMS for invalid inputs, or OS_ENOTSUPPORTED if
 *         the storage could hold more records than the stack can track.
 */
KERNELAPI oserr_t KERNELABI
MemoryStackConstruct(
    _In_ MemoryStack_t* stack,
    _In_ size_t         blockSize,
    _In_ uintptr_t      dataAddress,
    _In_ size_t         dataSize);

/**
 * @brief Update the storage address when the same memory is available at a new address.
 * @param stack Set-up stack; no other code may use it during this call.
 * @param address New address of the same storage, with the required alignment.
 */
KERNELAPI void KERNELABI
MemoryStackRelocate(
    _In_ MemoryStack_t* stack,
    _In_ uintptr_t      address);

/**
 * @brief Add a run of blocks back as free memory and join touching free ranges.
 * 
 * @param stack Set-up stack.
 * @param address Address of the first block; it must be aligned to the block size.
 * @param blockCount Number of consecutive blocks; must be greater than zero.
 * @return OS_EOK; OS_EINVALPARAMS for invalid inputs or blocks that are already
 *         free; OS_EOOM if there is no room for another range; or
 *         OS_ENOTSUPPORTED if the combined block count would overflow.
 *         On failure, the stack is unchanged. The stack only records which
 *         blocks are free; it cannot check who owns them. Do not add blocks
 *         back after they have been allocated to someone else.
 */
KERNELAPI oserr_t KERNELABI
MemoryStackPush(
    _In_ MemoryStack_t* stack,
    _In_ uintptr_t      address,
    _In_ int            blockCount);

/**
 * @brief Add separate blocks back as free memory, in any order.
 * Adjacent free blocks are joined into ranges.
 * 
 * @param stack Set-up stack with enough storage for temporary ranges while the
 *              blocks are being added.
 * @param blocks List of distinct block addresses that the caller owns. Each
 *               address must be aligned to the block size.
 * @param blockCount Number of addresses in the list; must be greater than zero.
 * @return OS_EOK or an error from adding a block. On failure, the original
 *         free ranges are restored.
 */
KERNELAPI oserr_t KERNELABI
MemoryStackPushMultiple(
    _In_ MemoryStack_t*   stack,
    _In_ const uintptr_t* blocks,
    _In_ int              blockCount);

/**
 * @brief Remove up to the requested number of blocks from the highest addresses.
 * The returned blocks do not need to be next to one another.
 * 
 * @param stack Set-up stack.
 * @param blockCount Number requested; receives the number actually removed.
 * @param blocks Output array with room for the requested number of addresses.
 * @return OS_EOK if all requested blocks were removed; OS_EINCOMPLETE if fewer
 *         were available (the available blocks are still removed); OS_EOOM if
 *         no blocks are free (outputs are unchanged); or OS_EINVALPARAMS.
 */
KERNELAPI oserr_t KERNELABI
MemoryStackPop(
    _In_    MemoryStack_t* stack,
    _InOut_ int*           blockCount,
    _Out_   uintptr_t*     blocks);

/**
 * @brief Remove one consecutive run that meets the requested address rules.
 * 
 * @param stack Set-up stack.
 * @param length Number of bytes requested; must be greater than zero. The stack
 *               reserves enough whole blocks to cover this length.
 * @param alignment Required address alignment, or zero for the block size.
 *                   Nonzero values must be powers of two.
 * @param boundary Optional power-of-two address boundary, or zero for none.
 *                 The requested length must fit without crossing a boundary.
 * @param addressOut Receives the starting address of the run; address zero is valid.
 * @return OS_EOK; OS_EINVALPARAMS for invalid or impossible requests; or OS_EOOM
 *         if no suitable run or enough storage is available. On failure, the
 *         output and free ranges are unchanged. Any free blocks before and after
 *         the run remain tracked; this function does not add storage itself.
 */
KERNELAPI oserr_t KERNELABI
MemoryStackPopContiguous(
    _In_  MemoryStack_t* stack,
    _In_  size_t         length,
    _In_  size_t         alignment,
    _In_  size_t         boundary,
    _Out_ uintptr_t*     addressOut);

/**
 * @brief Count the blocks that are currently free.
 * 
 * @param stack Set-up stack. The total count must fit in size_t, and the caller
 *              must prevent other code from changing the stack during the count.
 * @return Total number of free blocks.
 */
KERNELAPI size_t KERNELABI
MemoryStackFreeBlockCount(
    _In_ const MemoryStack_t* stack);

#endif //!__UTILS_MEMORYSTACK_H__
