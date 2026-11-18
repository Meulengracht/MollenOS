/**
 * Copyright, Philip Meulengracht
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
 * ARM SMMU Control Memory Interface
 * Manages a pre-allocated pool of 4 KiB pages ("arena") for one SMMU unit.
 * SMMUv3 and MMU-500 drivers allocate hardware page tables, stream tables, and
 * command or event queues from this pool so DMA mapping operations never depend
 * on the general kernel heap.
 */

#ifndef __ARM_SMMU_MEMORY_H__
#define __ARM_SMMU_MEMORY_H__

#include <component/memory.h>

DECL_STRUCT(MemorySpace);

// Control structures always use 4 KiB pages, up to 2 MiB (512 pages) per unit.
#define ARM_SMMU_MEMORY_PAGE_SIZE 4096U
#define ARM_SMMU_MEMORY_MAX_SIZE  (2U * 1024U * 1024U)

// Lifecycle states for an SMMU control memory pool.
enum ArmSMMUMemoryState {
    ArmSMMUMemoryEmpty,     // Zeroed record before initialization or after full cleanup.
    ArmSMMUMemoryPreparing, // Setup is underway. Partial resources stay attached for Destroy.
    ArmSMMUMemoryReady,     // Mapped, zeroed, and ready to hand out control pages.
    ArmSMMUMemoryClosing,   // Teardown is underway. New allocations are refused.
    ArmSMMUMemoryFailed     // Internal error latched. Pages stay allocated forever for safety.
};

/**
 * @brief One contiguous chunk of SMMU control pages.
 *
 * Length is always a multiple of 4 KiB. Treat a live block as read-only and
 * stop using any copy of it once ArmSMMUMemoryFree succeeds.
 */
typedef struct ArmSMMUMemoryBlock {
    paddr_t PhysicalAddress;
    void*   VirtualAddress;
    size_t  Length;
} ArmSMMUMemoryBlock_t;

/**
 * @brief Pre-allocated control memory pool for a single SMMU unit.
 *
 * Callers must hold the hardware unit's mutex around every operation on this
 * struct. Allocation holds the underlying device-memory range, while Mapping
 * and Space describe the kernel virtual view of those same pages.
 *
 * FreePages tracks available page ranges. BlockPages mirrors every page in the
 * pool to validate frees: the first page of an active block stores its page
 * count, interior pages hold a sentinel value, and free pages are zero. If any
 * internal check fails, State moves to ArmSMMUMemoryFailed and the pool stays
 * locked forever so hardware can never read freed RAM.
 */
typedef struct ArmSMMUMemory {
    enum ArmSMMUMemoryState       State;
    struct SystemMemoryAllocation Allocation;
    MemorySpace_t*                Space;
    vaddr_t                       Mapping;
    MemoryStack_t                 FreePages;
    void*                         Storage;
    uint16_t*                     BlockPages;
    size_t                        PageCount;
    size_t                        BlockCount;
} ArmSMMUMemory_t;

/**
 * @brief Allocates and maps a bounded control memory pool for an SMMU unit.
 *
 * Hold the unit mutex and pass a zeroed memory struct. length is rounded up to
 * a multiple of 4 KiB and cannot exceed ARM_SMMU_MEMORY_MAX_SIZE. Every page in
 * the pool must lie at or below physicalLimit (inclusive), and the host kernel
 * must run with 4 KiB pages. The virtual mapping uses the device-memory pool's
 * cache policy, disallows execution, and is zeroed before returning OS_EOK.
 *
 * @param memory        Zeroed control memory pool to initialize.
 * @param length        Requested size in bytes (1 to ARM_SMMU_MEMORY_MAX_SIZE).
 * @param physicalLimit Highest physical address the SMMU hardware can reach.
 * @return OS_EOK on success, OS_EINVALPARAMS for invalid arguments, OS_EBUSY if
 *         already initialized, OS_ENOTSUPPORTED if page sizes or physicalLimit
 *         cannot be met, or a memory allocation/mapping error. Partial work
 *         stays recorded in memory so ArmSMMUMemoryDestroy can clean it up,
 *         unless an internal consistency check enters ArmSMMUMemoryFailed and
 *         returns OS_EUNKNOWN.
 */
KERNELABI oserr_t KERNELAPI
ArmSMMUMemoryInitialize(
    _In_ ArmSMMUMemory_t* memory,
    _In_ size_t           length,
    _In_ uint64_t         physicalLimit);

/**
 * @brief Allocates and zeroes a contiguous block of control pages.
 *
 * Hold the unit mutex. length is rounded up to 4 KiB pages, and alignment
 * applies to PhysicalAddress (0 means 4 KiB alignment, otherwise a power of two).
 * Do not pass a blockOut that already holds an unfreed block, or its reference
 * will be overwritten. blockOut stays untouched on failure.
 *
 * @param memory    Initialized pool in ArmSMMUMemoryReady state.
 * @param length    Number of bytes needed (1 to ARM_SMMU_MEMORY_MAX_SIZE).
 * @param alignment Required physical alignment in bytes, or 0 for one page.
 * @param blockOut  Receives the physical address, virtual address, and rounded length.
 * @return OS_EOK on success, OS_EINVALPARAMS for bad parameters, OS_EOOM when
 *         no contiguous span fits, OS_EBUSY if the pool is not Ready,
 *         OS_ECANCELLED if the pool is Failed, or OS_EUNKNOWN if a corrupted
 *         free list is detected (which latches ArmSMMUMemoryFailed).
 */
KERNELABI oserr_t KERNELAPI
ArmSMMUMemoryAllocate(
    _In_  ArmSMMUMemory_t*      memory,
    _In_  size_t                length,
    _In_  size_t                alignment,
    _Out_ ArmSMMUMemoryBlock_t* blockOut);

/**
 * @brief Returns a control block to the pool once hardware has stopped reading it.
 *
 * Hold the unit mutex. Before calling this, the driver must finish any device
 * disconnects and TLB or cache invalidations so the SMMU can no longer touch
 * these pages. Never allocates memory or talks to hardware. Clears block on
 * success, or leaves it unchanged on error.
 *
 * @param memory Initialized pool in ArmSMMUMemoryReady state.
 * @param block  Previously allocated block to return. Zeroed on success.
 * @return OS_EOK on success, OS_EINVALPARAMS for an unknown, misaligned, or
 *         already freed block, OS_EBUSY if not Ready, OS_ECANCELLED if Failed,
 *         or OS_EUNKNOWN on internal metadata corruption.
 */
KERNELABI oserr_t KERNELAPI
ArmSMMUMemoryFree(
    _In_    ArmSMMUMemory_t*      memory,
    _InOut_ ArmSMMUMemoryBlock_t* block);

/**
 * @brief Unmaps and releases the control memory pool during unit teardown.
 *
 * Hold the unit mutex and ensure hardware is disconnected first. A zeroed
 * record succeeds right away. Returns OS_EBUSY if blocks are still allocated,
 * or OS_ECANCELLED if the pool is in ArmSMMUMemoryFailed (leaving resources
 * pinned). If unmapping or releasing physical pages fails, unfinished state
 * stays in memory so the caller can retry later. Clears memory on success.
 *
 * @param memory Pool to tear down.
 * @return OS_EOK on success, OS_EINVALPARAMS if memory is NULL, OS_EBUSY if
 *         blocks remain allocated, OS_ECANCELLED if Failed, or the unmap/free
 *         error on a partial teardown.
 */
KERNELABI oserr_t KERNELAPI
ArmSMMUMemoryDestroy(
    _In_ ArmSMMUMemory_t* memory);

#endif //!__ARM_SMMU_MEMORY_H__
