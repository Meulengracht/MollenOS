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
 */

#ifndef __DMA_IOVA_H__
#define __DMA_IOVA_H__

#include <os/osdefs.h>

/**
 * An IOVA (I/O virtual address) is the address a device uses when it reads or
 * writes memory through an IOMMU. These addresses are separate from CPU
 * addresses. The allocator in this file only hands out ranges of them. It does
 * not map any memory or touch hardware.
 */
typedef struct DMAIOVAAllocator DMAIOVAAllocator_t;
typedef struct DMAIOVAAllocation DMAIOVAAllocation_t;

/** A range of device addresses with at least one byte. First and Last are both part of the range. */
typedef struct DMAIOVARange {
    uint64_t First;
    uint64_t Last;
} DMAIOVARange_t;

/**
 * @brief Describes the address space an allocator hands out from.
 *
 * Granularity is the size of one granule, the smallest block of address space
 * that can be handed out, for example the IOMMU page size. It must be a power
 * of two and not zero. Bounds and every Reserved range must start and end on a
 * granule boundary.
 *
 * Reserved lists address ranges that must never be handed out, such as ranges
 * the hardware already uses. The entries may come in any order and may touch
 * each other, but they must not overlap. They must all lie inside Bounds.
 */
typedef struct DMAIOVAConfig {
    DMAIOVARange_t        Bounds;
    uint64_t              Granularity;
    const DMAIOVARange_t* Reserved;
    uint32_t              ReservedCount;
} DMAIOVAConfig_t;

/**
 * @brief Describes one block of device addresses that the caller wants.
 *
 * Length must not be zero and must be a multiple of the allocator's granule.
 * It is never rounded up for you.
 *
 * Alignment and Boundary must each be zero or a power of two. An Alignment of
 * zero means the block only needs to start on a granule boundary. A Boundary
 * of zero means there is no boundary rule. Otherwise the whole block must fit
 * between two multiples of Boundary, counted from address zero. In other words
 * it must not cross a Boundary-sized line.
 *
 * Limit is the highest address the block may use, and that address is allowed.
 * A Limit of zero is valid and means only address zero can be used.
 */
typedef struct DMAIOVARequest {
    uint64_t Length;
    uint64_t Alignment;
    uint64_t Boundary;
    uint64_t Limit;
} DMAIOVARequest_t;

/**
 * @brief Create an allocator. The reserved ranges are copied.
 *
 * The config and its Reserved array are only read during this call, so the
 * caller may change or free them afterwards. Reserved may be NULL only when
 * ReservedCount is zero. Creating an allocator does not map memory or change
 * hardware. All allocator functions must be called from thread context, not
 * from an interrupt handler.
 *
 * @param config Address bounds, granule and reserved ranges.
 * @param allocatorOut Must point to a NULL pointer. It is left untouched on failure.
 * @return OS_EOK on success.
 *         OS_EINVALPARAMS if the config is invalid.
 *         OS_EBUSY if allocatorOut already points to an object.
 *         OS_EOVERFLOW if ReservedCount is too large to size the allocator.
 *         OS_EOOM if memory could not be allocated.
 *         OS_EUNKNOWN if the internal tree rejected an entry.
 */
KERNELABI oserr_t KERNELAPI
DMAIOVAAllocatorCreate(
    _In_    const DMAIOVAConfig_t* config,
    _InOut_ DMAIOVAAllocator_t**   allocatorOut);

/**
 * @brief Reserve the lowest free block of device addresses that fits the request.
 *
 * The allocator must stay alive until the returned allocation is released.
 * The request is only read during this call. Several threads may allocate and
 * release at the same time, because the allocator takes an internal lock while
 * it searches and records the result.
 *
 * The block starts on at least a granule boundary, and every byte of it must be
 * inside the allocator bounds and at or below request Limit. Address zero can
 * be returned.
 *
 * @param allocator Allocator created with DMAIOVAAllocatorCreate.
 * @param request Length and placement rules for the block.
 * @param allocationOut Must point to a NULL pointer. It is left untouched on failure.
 * @return OS_EOK on success.
 *         OS_EINVALPARAMS if the request is invalid.
 *         OS_EBUSY if allocationOut already points to an object.
 *         OS_EOOM if memory for the bookkeeping could not be allocated.
 *         OS_EOVERFLOW if the allocator cannot count any more allocations.
 *         OS_ENOENT if no free gap is large enough.
 *         OS_EUNKNOWN if the internal tree rejected the new entry.
 *         The request is checked before memory is allocated, and memory is
 *         allocated before the search. A low memory failure can therefore be
 *         reported even when no gap would have fit.
 */
KERNELABI oserr_t KERNELAPI
DMAIOVAAllocate(
    _In_    DMAIOVAAllocator_t*     allocator,
    _In_    const DMAIOVARequest_t* request,
    _InOut_ DMAIOVAAllocation_t**   allocationOut);

/**
 * @brief Get the device address range of an allocation.
 *
 * This does not lock and does not allocate. The range never changes while the
 * allocation exists. Having the range does not mean a device is allowed to use
 * it, as that depends on what has been mapped.
 *
 * @param allocation An allocation that has not been released. The caller must
 *                   keep it alive for as long as the returned pointer is used.
 * @return Pointer to the First/Last pair. It is valid until the allocation is released.
 */
KERNELABI KERNELAPI const DMAIOVARange_t*
DMAIOVAAllocationRange(
    _In_ const DMAIOVAAllocation_t* allocation);

/**
 * @brief Give an allocation's addresses back so they can be handed out again.
 *
 * This never allocates memory. Before calling it, the caller must make sure
 * the device is no longer using the addresses, and that any cached translations
 * for them are flushed. The allocator cannot check either. If cleaning up the
 * mapping failed, do not release. Keep the allocation instead.
 *
 * Copying the pointer does not count as another reference. Calls for the same
 * allocation must not run at the same time.
 *
 * @param allocation Pointer to the allocation pointer. It is set to NULL on
 *                   success. Passing NULL or a pointer to NULL does nothing.
 * @return OS_EOK on success.
 *         OS_EUNKNOWN if the allocation does not match the allocator's records.
 *         In that case the pointer is left as it was.
 */
KERNELABI oserr_t KERNELAPI
DMAIOVAAllocationRelease(
    _InOut_ DMAIOVAAllocation_t** allocation);

/**
 * @brief Destroy an allocator that has no allocations left.
 *
 * The caller must first stop all new calls into the allocator and wait for
 * running ones to finish. An allocation count of zero is not enough on its own,
 * because a release may have updated the count but not yet returned.
 * Reserved ranges are freed together with the allocator. There is no way to
 * force destruction while allocations still exist.
 *
 * @param allocator Pointer to the allocator pointer. It is set to NULL on
 *                  success. Passing NULL or a pointer to NULL does nothing.
 * @return OS_EOK on success.
 *         OS_EBUSY if allocations still exist. The allocator is left unchanged.
 */
KERNELABI oserr_t KERNELAPI
DMAIOVAAllocatorDestroy(
    _InOut_ DMAIOVAAllocator_t** allocator);

#endif //!__DMA_IOVA_H__
