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
 * SHM device mapping and transfer API. Maps SHM buffers for DMA devices,
 * allocates shared device-pool buffers, synchronizes CPU caches for streaming
 * and persistent transfers, and returns device segment lists.
 */

//#define __TRACE

#include <shm/device.h>
#include <shm/access.h>
#include <arch/utils.h>
#include <ddk/barrier.h>
#include <handles/global.h>
#include <handles/handle.h>
#include <debug.h>
#include <limits.h>
#include <shm/shm.h>
#include <shm/view.h>
#include <string.h>

#include "private.h"

/**
 * @brief The size and limits to ask the device pool for, and how to map the result.
 *
 * Length is the number of bytes to allocate. PhysicalAlignment and
 * PhysicalBoundary are limits on the physical address, where zero means no
 * limit. MapRequirements applies to the device address and is only used when
 * HasMapRequirements is set.
 */
struct __AllocationPlan {
    size_t                    Length;
    size_t                    PhysicalAlignment;
    size_t                    PhysicalBoundary;
    struct DMAMapRequirements MapRequirements;
    bool                      HasMapRequirements;
};

static bool
__IsPowerOfTwoOrZero(
    _In_ uint64_t value)
{
    return (value & (value - 1)) == 0;
}

static bool
__MapRequirementsValid(
    _In_ const struct DMAMapRequirements* requirements,
    _In_ size_t                           length)
{
    if (requirements == NULL) {
        return true;
    }

    if (!__IsPowerOfTwoOrZero(requirements->AddressAlignment) ||
        !__IsPowerOfTwoOrZero(requirements->LengthAlignment)) {
        return false;
    }

    return requirements->LengthAlignment == 0 ||
        (length & (requirements->LengthAlignment - 1)) == 0;
}

static oserr_t
__CheckBufferKind(
    _In_ const struct SHMBuffer*        buffer,
    _In_ const struct SHMDeviceContext* context,
    _In_ enum SHMDeviceMappingKind      kind)
{
    if (kind != SHMDeviceMappingShared) {
        return OS_EOK;
    }

    // Shared mappings never sync caches, so the CPU and the device must see
    // the same memory with no stale cached copies in between. Only device pool
    // allocations record the cache setting used by every CPU mapping of the
    // same pages, so other buffers are refused. A non-coherent device (one that
    // does not see the CPU caches) cannot safely share pages the CPU caches.
    if (buffer->Allocation.Owner == NULL) {
        return OS_ENOTSUPPORTED;
    }

    if (buffer->Allocation.Cached && context->CachePolicy == SHMDeviceCacheNonCoherent) {
        return OS_ENOTSUPPORTED;
    }
    return OS_EOK;
}

/**
 * @brief Check the arguments of a map request before any reference is taken.
 *
 * @return OS_EOK, OS_EINVALPARAMS for a bad argument or an invalid operation
 *         token, or OS_EBUSY if *mappingOut already holds a mapping.
 */
static oserr_t
__CheckMapRequest(
    _In_ SHMDeviceOperation_t*            operation,
    _In_ struct SHMDeviceContext*         context,
    _In_ size_t                           length,
    _In_ enum DMADirection                direction,
    _In_ enum SHMDeviceMappingKind        kind,
    _In_ const struct DMAMapRequirements* requirements,
    _In_ struct SHMDeviceMapping**        mappingOut)
{
    if (context == NULL || mappingOut == NULL) {
        return OS_EINVALPARAMS;
    }

    // The output slot must be empty. Otherwise the existing mapping would be
    // lost.
    if (*mappingOut != NULL) {
        WARNING("__CheckMapRequest: context %p already has a mapping %p in the output slot",
                context, *mappingOut);
        return OS_EBUSY;
    }

    // The token is created by the kernel before the call, so a bad one means
    // the caller broke the calling rules.
    if (!SHMDeviceOperationIsValid(operation, context, NULL, SHMDeviceOperationSubmit)) {
        WARNING("__CheckMapRequest: context %p operation %p is not a valid submit token",
                context, operation);
        return OS_EINVALPARAMS;
    }

    if (!__MapRequirementsValid(requirements, length)) {
        return OS_EINVALPARAMS;
    }

    // Cache work depends on the direction, so reject unknown values here.
    if (direction != DMAToDevice && direction != DMAFromDevice &&
        direction != DMABidirectional) {
        return OS_EINVALPARAMS;
    }

    switch (kind) {
        case SHMDeviceMappingStreamingBounce:
        case SHMDeviceMappingStreamingDirect:
        case SHMDeviceMappingPersistent:
        case SHMDeviceMappingShared:
        case SHMDeviceMappingRegions:
            return OS_EOK;
        default:
            return OS_EINVALPARAMS;
    }
}

/**
 * @brief Take a reference on the SHM buffer and check that it can be mapped.
 *
 * The reference keeps the pages allocated for as long as the device may use
 * them, because SHM frees a buffer's pages when the last reference is
 * released. On success the caller holds the reference in source and must
 * release it. On failure no reference is held.
 *
 * @param offset Start of the range, counted from the start of the handle.
 * @return OS_EOK, OS_ENOENT if the buffer does not exist, OS_ENOTSUPPORTED
 *         for a buffer that cannot be mapped for this kind, OS_EPERMISSIONS if
 *         the handle does not allow the access, or OS_EINVALPARAMS for an empty
 *         range or one outside the handle.
 */
static oserr_t
__AcquireSource(
    _In_  struct SHMDeviceContext*  context,
    _In_  uuid_t                    shmID,
    _In_  size_t                    offset,
    _In_  size_t                    length,
    _In_  enum DMADirection         direction,
    _In_  enum SHMDeviceMappingKind kind,
    _Out_ struct SHMReference*      source)
{
    unsigned int scope;
    oserr_t      status;

    status = SHMResolve(shmID, source);
    if (status != OS_EOK) {
        TRACE("__AcquireSource: SHMResolve buffer %u failed: %u", shmID, status);
        return OS_ENOENT;
    }

    // Buffers created with SHM_REGIONS must use SHMDeviceMappingRegions, and
    // ordinary buffers cannot use that mapping kind.
    if (((source->Buffer->Flags & SHM_REGIONS) != 0) != (kind == SHMDeviceMappingRegions)) {
        status = OS_ENOTSUPPORTED;
        goto release;
    }

    // The handle must allow the kind of access the device will do.
    scope = direction == DMAToDevice ? GrantTokenScopeRead : GrantTokenScopeWrite;
    if (direction == DMABidirectional) {
        scope |= GrantTokenScopeRead;
    }

    if ((source->Scope & scope) != scope) {
        status = OS_EPERMISSIONS;
        goto release;
    }

    if (length == 0 || !__SHMRangeContains(0, source->Length, offset, length)) {
        status = OS_EINVALPARAMS;
        goto release;
    }

    status = __CheckBufferKind(source->Buffer, context, kind);
    if (status != OS_EOK) {
        goto release;
    }
    return OS_EOK;

release:
    // These refusals come from the caller's request, so they are only traced.
    TRACE("__AcquireSource: buffer %u refused for offset %zu, length %zu: %u",
          shmID, offset, length, status);
    __ReleaseMemoryHandleReference(source->GlobalHandleID, __func__);
    return status;
}

/**
 * @brief Create a device mapping for a range of a SHM buffer.
 *
 * The order of the steps matters:
 * 1. Check the arguments, then take a reference on the buffer. The reference
 *    keeps the pages alive for as long as the device may use them.
 * 2. Take a reference on the context and prepare the mapping. Nothing is
 *    visible to the device yet, so a failure only needs to release references.
 * 3. Link the mapping into the context. From this point a cancellation can
 *    find it, so every later failure is handed to SHMDeviceAbandon.
 * 4. Install the mapping in the address space, which is the first step that
 *    can let the device reach the pages.
 * 5. Mark the mapping Ready. Only now may the caller use it.
 *
 * The operation is checked between the steps, so a context that is stopped in
 * the meantime is noticed before the mapping becomes Ready.
 */
static oserr_t
__MapBuffer(
    _In_    SHMDeviceOperation_t*            operation,
    _In_    struct SHMDeviceContext*         context,
    _In_    uuid_t                           shmID,
    _In_    size_t                           offset,
    _In_    size_t                           length,
    _In_    enum DMADirection                direction,
    _In_    enum SHMDeviceMappingKind        kind,
    _In_    const struct DMAMapRequirements* requirements,
    _In_    const struct DMARequirements*    allocationRequirements,
    _InOut_ struct SHMDeviceMapping**        mappingOut)
{
    struct SHMReference      source;
    struct SHMDeviceMapping* mapping = NULL;
    oserr_t                  status;
    oserr_t                  cleanupStatus;

    status = __CheckMapRequest(
        operation,
        context,
        length,
        direction,
        kind,
        requirements,
        mappingOut
    );
    if (status != OS_EOK) {
        return status;
    }

    status = __AcquireSource(
        context,
        shmID,
        offset,
        length,
        direction,
        kind,
        &source
    );
    if (status != OS_EOK) {
        return status;
    }

    // Range reservations for the CPU and the device are tracked by position
    // in the backing buffer. The caller's offset is relative to its own handle,
    // which can cover only part of the buffer, so add the handle's start here.
    offset += source.Offset;

    // Take a context reference. The mapping needs the context for as long as
    // its segments can be read.
    status = SHMDeviceContextAcquire(context);
    if (status != OS_EOK) {
        ERROR("__MapBuffer: SHMDeviceContextAcquire context %p buffer %u failed: %u",
              context, source.GlobalHandleID, status);
        goto release;
    }

    status = SHMDeviceMappingPrepare(
        source.Buffer,
        context,
        offset,
        length,
        direction,
        kind,
        requirements,
        allocationRequirements,
        &mapping
    );
    if (status != OS_EOK) {
        // The preparation logs the reason for the failure.
        SHMDeviceContextRelease(&context);
        goto release;
    }

    mapping->Access.ReferenceID = source.GlobalHandleID;
    mapping->Access.Scope = source.Scope;

    // Link the mapping before installing it, so that cancellation can find it
    // with its source grant already set. The context operation keeps the stop
    // cleanup from running until this function has finished, whether it ends
    // in success or rollback.
    SHMDeviceMappingLink(mapping);
    status = SHMDeviceOperationCheck(operation);
    if (status != OS_EOK) {
        goto abandon;
    }

    // From here on, a failure may still leave the device able to reach the
    // pages. Do not retry with bounce pages and do not free the pages on the
    // normal error path.
    status = DMAAddressMappingInstall(mapping->AddressMapping);
    if (status != OS_EOK) {
        ERROR("__MapBuffer: DMAAddressMappingInstall buffer %u failed: %u",
              source.GlobalHandleID, status);
        goto abandon;
    }

    status = SHMDeviceOperationCheck(operation);
    if (status != OS_EOK) {
        goto abandon;
    }

    // Cancellation reads State under MappingLock, even while this operation is
    // still counted. Set Ready under that lock and leave Access alone. Another
    // thread may already have marked it cancelled or locked its range.
    MutexLock(&context->MappingLock);
    mapping->State = SHMDeviceMappingReady;
    MutexUnlock(&context->MappingLock);
    TRACE("__MapBuffer: mapping %p ready for buffer %u, offset %zu, length %zu",
          mapping, source.GlobalHandleID, offset, length);
    *mappingOut = mapping;
    return OS_EOK;

abandon:
    // Once linked, even a Preparing mapping can be locked by a cancellation.
    // So every failure after linking hands the references over to recovery.
    // That includes the binding closing before installation.
    cleanupStatus = SHMDeviceAbandon(&mapping);
    if (cleanupStatus != OS_EOK) {
        ERROR("__MapBuffer: SHMDeviceAbandon buffer %u failed: %u (original status %u)",
              source.GlobalHandleID, cleanupStatus, status);
    }
    return status;

release:
    __ReleaseMemoryHandleReference(source.GlobalHandleID, __func__);
    return status;
}

oserr_t
SHMDeviceMap(
    _In_    SHMDeviceOperation_t*            operation,
    _In_    struct SHMDeviceContext*         context,
    _In_    uuid_t                           shmID,
    _In_    size_t                           offset,
    _In_    size_t                           length,
    _In_    enum DMADirection                direction,
    _In_    enum SHMDeviceMappingKind        kind,
    _In_    const struct DMAMapRequirements* requirements,
    _InOut_ struct SHMDeviceMapping**        mappingOut)
{
    return __MapBuffer(
        operation,
        context,
        shmID,
        offset,
        length,
        direction,
        kind,
        requirements,
        NULL,
        mappingOut
    );
}

/**
 * @brief Check that the requested allocation limits make sense.
 *
 * Doing this before allocating keeps a bad request from looking like a
 * shortage of memory. The boundary is a power-of-two sized block of addresses
 * that the allocation must not cross.
 */
static bool
__RequirementsValid(
    _In_ const struct DMARequirements* requirements)
{
    if (requirements->Length == 0) {
        return false;
    }

    if (!__IsPowerOfTwoOrZero(requirements->Alignment) || !__IsPowerOfTwoOrZero(requirements->Boundary)) {
        return false;
    }

    // A contiguous allocation longer than the boundary would have to cross it,
    // so it can never fit.
    if (requirements->Contiguous && requirements->Boundary != 0) {
        return requirements->Length <= requirements->Boundary;
    }
    return true;
}

/**
 * @brief Plan an allocation for a context that has an IOMMU.
 *
 * The buffer is rounded up to whole granules. The handle returned to the
 * caller covers all of that zeroed storage, including the extra tail. The pool
 * only has to align the memory to a granule. The alignment and boundary the
 * device asked for apply to its IOVA (the address the device uses), not to
 * the physical address.
 *
 * @return OS_EOK, OS_ENOTSUPPORTED if the granule or the rounded length cannot
 *         be handled, or OS_EOVERFLOW if rounding the length overflows.
 */
static oserr_t
__PlanTranslatedAllocation(
    _In_  const struct DMARequirements* requirements,
    _In_  uint64_t                      granularity,
    _In_  size_t                        pageSize,
    _Out_ struct __AllocationPlan*      plan)
{
    size_t allocationUnit;

    if (granularity > SIZE_MAX) {
        return OS_ENOTSUPPORTED;
    }

    allocationUnit = granularity > pageSize ? (size_t)granularity : pageSize;
    if (requirements->Length > SIZE_MAX - (allocationUnit - 1)) {
        return OS_EOVERFLOW;
    }

    plan->Length = DIVUP(requirements->Length, allocationUnit) * allocationUnit;
    if (requirements->Boundary != 0 && plan->Length > requirements->Boundary) {
        return OS_ENOTSUPPORTED;
    }

    plan->PhysicalAlignment = (size_t)granularity;
    plan->PhysicalBoundary = 0;

    // The device sees the buffer as one segment. If that segment starts on a
    // boundary and is no longer than the boundary, it cannot cross one. So the
    // larger of the alignment and the boundary is requested as the address
    // alignment. This reuses the existing map requirement, so no new field is
    // needed.
    plan->MapRequirements.AddressAlignment = requirements->Alignment;
    if (requirements->Boundary > plan->MapRequirements.AddressAlignment) {
        plan->MapRequirements.AddressAlignment = requirements->Boundary;
    }
    plan->HasMapRequirements = true;
    return OS_EOK;
}

/**
 * @brief Plan an allocation for a context without an IOMMU.
 *
 * The device address comes from the physical address, so the pool must meet
 * the alignment and boundary itself.
 *
 * @return OS_EOK, or OS_EINVALPARAMS if rounding the length overflows.
 */
static oserr_t
__PlanDirectAllocation(
    _In_  const struct DMARequirements* requirements,
    _In_  size_t                        pageSize,
    _Out_ struct __AllocationPlan*      plan)
{
    if (requirements->Length > SIZE_MAX - pageSize) {
        return OS_EINVALPARAMS;
    }

    plan->Length = DIVUP(requirements->Length, pageSize) * pageSize;
    plan->PhysicalAlignment = (size_t)requirements->Alignment;
    plan->PhysicalBoundary = (size_t)requirements->Boundary;
    plan->HasMapRequirements = false;
    return OS_EOK;
}

oserr_t
SHMDeviceAllocate(
    _In_    SHMDeviceOperation_t*         operation,
    _In_    struct SHMDeviceContext*      context,
    _In_    const struct DMARequirements* requirements,
    _Out_   SHMHandle_t*                  bufferOut,
    _InOut_ struct SHMDeviceMapping**     mappingOut)
{
    struct SHMDeviceMapping* mapping = NULL;
    struct __AllocationPlan  plan = { 0 };
    SHMHandle_t              handle;
    size_t                   pageSize = GetMemorySpacePageSize();
    uint64_t                 granularity;
    oserr_t                  status;
    oserr_t                  cleanupStatus;

    if (context == NULL || requirements == NULL || bufferOut == NULL || mappingOut == NULL) {
        return OS_EINVALPARAMS;
    }

    if (!SHMDeviceOperationIsValid(operation, context, NULL, SHMDeviceOperationSubmit)) {
        return OS_EINVALPARAMS;
    }

    if (*mappingOut != NULL) {
        WARNING("SHMDeviceAllocate: context %p already has a mapping %p in the output slot",
                context, *mappingOut);
        return OS_EBUSY;
    }

    if (!__RequirementsValid(requirements)) {
        return OS_EINVALPARAMS;
    }

    // A granule is the smallest block the IOMMU maps. It is zero when the
    // address space is direct and has no IOMMU.
    granularity = DMAAddressSpaceGetGranularity(context->AddressSpace);
    if (granularity != 0) {
        status = __PlanTranslatedAllocation(
            requirements,
            granularity,
            pageSize,
            &plan
        );
    } else {
        status = __PlanDirectAllocation(requirements, pageSize, &plan);
    }
    if (status != OS_EOK) {
        WARNING("SHMDeviceAllocate: context %p cannot plan %zu bytes (granularity %llu): %u",
                context, (size_t)requirements->Length, (unsigned long long)granularity, status);
        return status;
    }

    // Both modes take the memory from the device pool, which also records how
    // the CPU maps these pages (cached or not). SHMCreateDeviceBuffer zeroes
    // the whole allocation before the mapping is prepared. If the pool is
    // full, the call fails. There is no fallback to ordinary memory.
    status = SHMCreateDeviceBuffer(
        plan.Length,
        plan.PhysicalAlignment,
        plan.PhysicalBoundary,
        &handle
    );
    if (status != OS_EOK) {
        ERROR("SHMDeviceAllocate: SHMCreateDeviceBuffer length %zu failed: %u", plan.Length, status);
        return status;
    }

    // The CPU and the device use this memory at the same time, so both must
    // use the very same pages. A bounce copy could leave one side reading old
    // bytes.
    status = __MapBuffer(
        operation,
        context,
        handle.ID,
        0,
        handle.Length,
        DMABidirectional,
        SHMDeviceMappingShared,
        plan.HasMapRequirements ? &plan.MapRequirements : NULL,
        requirements,
        &mapping
    );
    if (status != OS_EOK) {
        ERROR("SHMDeviceAllocate: __MapBuffer buffer %u failed: %u", handle.ID, status);
        goto detach;
    }

    TRACE("SHMDeviceAllocate: buffer %u, length %zu mapped as %p", handle.ID, plan.Length, mapping);
    *bufferOut = handle;
    *mappingOut = mapping;
    return OS_EOK;

detach:
    cleanupStatus = SHMDetach(&handle);
    if (cleanupStatus != OS_EOK) {
        ERROR("SHMDeviceAllocate: SHMDetach buffer %u failed: %u (mapping status %u)",
              handle.ID, cleanupStatus, status);
    }
    return status;
}

/**
 * @brief Run a cache operation on the physical bytes of a range in a mapping.
 *
 * Offsets are relative to the start of the mapping, but cache operations need
 * physical addresses. So this walks the mapping's physical blocks to find
 * them, and calls the operation once for each contiguous piece. The range must
 * lie inside the mapping.
 */
static void
__SyncCacheRange(
    _In_ const struct SHMDeviceMapping* mapping,
    _In_ size_t                         offset,
    _In_ size_t                         length,
    _In_ void                         (*operation)(uintptr_t, size_t))
{
    size_t skip = offset;

    for (int i = 0; i < mapping->ExtentCount && length != 0; i++) {
        const SHMSG_t* extent = &mapping->Extents[i];
        size_t         chunk;

        if (skip >= extent->Length) {
            skip -= extent->Length;
            continue;
        }

        chunk = extent->Length - skip;
        if (chunk > length) {
            chunk = length;
        }

        operation(extent->Address + skip, chunk);
        length -= chunk;
        skip = 0;
    }
}

static inline bool
__DeviceWrites(
    _In_ const struct SHMDeviceMapping* mapping)
{
    return mapping->Direction != DMAToDevice;
}

/**
 * @brief Check that a mapping can be used by the device right now.
 *
 * @return OS_EOK for a Ready mapping, OS_ECANCELLED if it was stopped, or
 *         OS_EBUSY if it is still being prepared or is being released.
 */
static oserr_t
__CheckMappingReady(
    _In_ const struct SHMDeviceMapping* mapping)
{
    if (mapping->State == SHMDeviceMappingReady) {
        return OS_EOK;
    }
    return mapping->State == SHMDeviceMappingStopped ? OS_ECANCELLED : OS_EBUSY;
}

// Copy the CPU's data into the bounce pages, so the device can read it. The
// offset counts from the first requested byte, in the source and in the
// payload inside the bounce storage.
static void
__CopyToBounce(
    _In_ const struct SHMDeviceMapping* mapping,
    _In_ size_t                         offset,
    _In_ size_t                         length)
{
    const SHMDeviceBounce_t* bounce = &mapping->Stream.Bounce;

    memcpy(
        (void*)(bounce->BounceMapping + __SHMDeviceBouncePayloadOffset(bounce) + offset),
        (const void*)(bounce->SourceMapping + bounce->SourceOffset + offset),
        length
    );
}

// Copy the data the device wrote from the bounce pages back to the buffer.
static void
__CopyFromBounce(
    _In_ const struct SHMDeviceMapping* mapping,
    _In_ size_t                         offset,
    _In_ size_t                         length)
{
    const SHMDeviceBounce_t* bounce = &mapping->Stream.Bounce;

    memcpy(
        (void*)(bounce->SourceMapping + bounce->SourceOffset + offset),
        (const void*)(bounce->BounceMapping + __SHMDeviceBouncePayloadOffset(bounce) + offset),
        length
    );
}

/**
 * @brief Widen a cache range to the whole mapped span of translated bounce
 *        storage.
 *
 * The device can reach every byte of the mapped granules, the padding after
 * the request included, and can write there without reporting it. The
 * storage is private to this mapping, idle between transfers, and used by one
 * transfer at a time, so cache work over all of it cannot drop a neighbor's
 * data. It also covers bytes left from an earlier transfer. Every other
 * mapping keeps its exact range, because its cache lines can be shared with
 * bytes the CPU is still using.
 */
static void
__CacheRange(
    _In_    const struct SHMDeviceMapping* mapping,
    _InOut_ size_t*                        offset,
    _InOut_ size_t*                        length)
{
    if (__SHMDeviceMappingIsTranslatedBounce(mapping)) {
        *offset = 0;
        *length = mapping->Stream.Bounce.MappedLength;
    }
}

static inline void
__PrepareForDevice(
    _In_ struct SHMDeviceMapping* mapping,
    _In_ size_t                   offset,
    _In_ size_t                   length)
{
    size_t cacheOffset = offset;
    size_t cacheLength = length;

    // All earlier CPU writes to the buffer must be finished before the cache
    // work below and before the device starts the transfer.
    dma_mb();

    // A non-coherent device reads RAM directly and does not see data that is
    // only in the CPU cache. For device reads, write the cached data back to
    // RAM first. For device writes, also invalidate the lines, so old cached
    // CPU data cannot later be written back over what the device wrote.
    // Uncached memory still needs the barrier above but has no cache lines to
    // maintain. Bounce pages follow the cache setting of the device pool.
    if (mapping->Cached && mapping->Context->CachePolicy == SHMDeviceCacheNonCoherent) {
        __CacheRange(mapping, &cacheOffset, &cacheLength);
        if (mapping->Direction == DMAToDevice) {
            __SyncCacheRange(mapping, cacheOffset, cacheLength, CpuDataCacheClean);
        } else {
            __SyncCacheRange(mapping, cacheOffset, cacheLength, CpuDataCacheCleanInvalidate);
        }
    }
}

void
SHMDeviceCompleteForCpu(
    _In_ struct SHMDeviceMapping* mapping,
    _In_ size_t                   offset,
    _In_ size_t                   length)
{
    size_t cacheOffset = offset;
    size_t cacheLength = length;

    // The driver must already have confirmed that the device finished or
    // stopped. The barrier makes that check happen before the CPU reads, so
    // the reads see what the device wrote.
    dma_mb();

    // While the device was working, the CPU may have loaded old values of this
    // memory into its cache, for example through speculative reads. Discard
    // those cached lines for the bytes the device wrote. Mapping setup already
    // checked that these cache lines do not reach outside the mapping.
    // Translated bounce storage is discarded in full, even for zero completed
    // bytes, because the device may have written bytes it cannot report.
    if (mapping->Cached && mapping->Context->CachePolicy == SHMDeviceCacheNonCoherent &&
        __DeviceWrites(mapping)) {
        __CacheRange(mapping, &cacheOffset, &cacheLength);
        __SyncCacheRange(mapping, cacheOffset, cacheLength, CpuDataCacheInvalidate);
    }
}

oserr_t
SHMDeviceSyncForDevice(
    _In_ SHMDeviceOperation_t*       operation,
    _In_ struct SHMDeviceMapping*    mapping,
    _In_ size_t                      offset,
    _In_ size_t                      length,
    _In_ const struct SHMAccessWait* wait)
{
    oserr_t status;

    // A transfer must cover at least one byte.
    if (mapping == NULL || !__SHMDeviceMappingIsStreaming(mapping) || length == 0) {
        return OS_EINVALPARAMS;
    }

    status = __CheckMappingReady(mapping);
    if (status != OS_EOK) {
        return status;
    }
    
    if (!__SHMRangeContains(0, mapping->Length, offset, length)) {
        return OS_EINVALPARAMS;
    }

    if (!SHMDeviceOperationIsValid(operation, mapping->Context, mapping, SHMDeviceOperationSubmit)) {
        return OS_EINVALPARAMS;
    }

    status = SHMDeviceOperationBeginAccess(
        operation,
        &mapping->Access,
        offset,
        length,
        __DeviceWrites(mapping),
        wait
    );
    if (status != OS_EOK) {
        return status;
    }

    // If bounce pages are in use, copy the CPU's data into them before the
    // device reads it. A device that only writes does not need the old
    // contents, so skip the copy.
    if (mapping->Stream.Bounce.BounceMapping != 0 && mapping->Direction != DMAFromDevice) {
        __CopyToBounce(mapping, offset, length);
    }

    __PrepareForDevice(mapping, offset, length);
    mapping->Stream.Offset = offset;
    mapping->Stream.Length = length;
    return SHMDeviceOperationCheck(operation);
}

oserr_t
SHMDeviceSyncForCpu(
    _In_ SHMDeviceOperation_t*    operation,
    _In_ struct SHMDeviceMapping* mapping,
    _In_ size_t                   length)
{
    size_t  offset;
    oserr_t status;

    // A transfer can stop before the device wrote any bytes, so a length of
    // zero is allowed. The CPU can then use the buffer again.
    if (mapping == NULL || !__SHMDeviceMappingIsStreaming(mapping)) {
        return OS_EINVALPARAMS;
    }

    status = __CheckMappingReady(mapping);
    if (status != OS_EOK) {
        return status;
    }

    if (!SHMDeviceOperationIsValid(operation, mapping->Context, mapping, SHMDeviceOperationComplete)) {
        return OS_EINVALPARAMS;
    }

    // The mapping must have an active device transfer.
    if (SHMAccessGetPhase(&mapping->Access) != SHMAccessActive) {
        return OS_EINVALPARAMS;
    }

    if (length > mapping->Stream.Length) {
        return OS_EINVALPARAMS;
    }

    offset = mapping->Stream.Offset;
    SHMDeviceCompleteForCpu(mapping, offset, length);

    // If bounce pages are in use, copy only the bytes the device completed
    // back to the buffer. Data outside that range stays as the CPU left it.
    if (mapping->Stream.Bounce.BounceMapping != 0 && __DeviceWrites(mapping)) {
        __CopyFromBounce(mapping, offset, length);
    }

    SHMAccessEnd(&mapping->Access);
    return OS_EOK;
}

oserr_t
SHMDeviceBeginUse(
    _In_ SHMDeviceOperation_t*       operation,
    _In_ struct SHMDeviceMapping*    mapping,
    _In_ const struct SHMAccessWait* wait)
{
    oserr_t status;

    if (mapping == NULL || mapping->Kind != SHMDeviceMappingPersistent) {
        return OS_EINVALPARAMS;
    }

    status = __CheckMappingReady(mapping);
    if (status != OS_EOK) {
        return status;
    }
    if (mapping->Persistent.Uses == UINT_MAX) {
        WARNING("SHMDeviceBeginUse: mapping %p use count is at its maximum", mapping);
        return OS_EOVERFLOW;
    }

    if (!SHMDeviceOperationIsValid(operation, mapping->Context, mapping, SHMDeviceOperationSubmit)) {
        return OS_EINVALPARAMS;
    }

    // The first use reserves the whole range, which keeps the CPU away until
    // every use has completed. Later uses only extend that reservation and skip
    // the cache work. Repeating it could write old cached CPU data over bytes
    // that a running device is writing.
    if (mapping->Persistent.Uses == 0) {
        status = SHMDeviceOperationBeginAccess(
            operation,
            &mapping->Access,
            0,
            mapping->Length,
            __DeviceWrites(mapping),
            wait
        );
        if (status != OS_EOK) {
            return status;
        }
        __PrepareForDevice(mapping, 0, mapping->Length);
    } else {
        status = SHMAccessExtend(&mapping->Access);
        if (status != OS_EOK) {
            return status;
        }
    }
    mapping->Persistent.Uses++;
    return SHMDeviceOperationCheck(operation);
}

oserr_t
SHMDeviceEndUse(
    _In_ SHMDeviceOperation_t*    operation,
    _In_ struct SHMDeviceMapping* mapping)
{
    oserr_t status;

    if (mapping == NULL || mapping->Kind != SHMDeviceMappingPersistent) {
        return OS_EINVALPARAMS;
    }

    status = __CheckMappingReady(mapping);
    if (status != OS_EOK) {
        return status;
    }

    if (mapping->Persistent.Uses == 0) {
        return OS_EINVALPARAMS;
    }

    if (!SHMDeviceOperationIsValid(operation, mapping->Context, mapping, SHMDeviceOperationComplete)) {
        return OS_EINVALPARAMS;
    }

    // The driver must have confirmed that this use finished or was stopped.
    // The CPU may touch the mapping again only after the last use ends, so the
    // cache sync and the release of the range reservation happen only then.
    if (mapping->Persistent.Uses == 1) {
        SHMDeviceCompleteForCpu(mapping, 0, mapping->Length);
        SHMAccessEnd(&mapping->Access);
    }
    mapping->Persistent.Uses--;
    return OS_EOK;
}

const struct DMASegment*
SHMDeviceMappingSegments(
    _In_  SHMDeviceOperation_t*          operation,
    _In_  const struct SHMDeviceMapping* mapping,
    _Out_ uint32_t*                      countOut)
{
    const struct DMASegment* segments;
    uint32_t                 count;

    // Return the stored segments only while the device addresses are still
    // usable. Once release starts, the caller must not use them, so return
    // nothing. A token for this mapping or for its context is accepted.
    if (mapping == NULL || countOut == NULL) {
        return NULL;
    }

    if (!SHMDeviceOperationIsValid(operation, mapping->Context, mapping, SHMDeviceOperationSubmit) &&
        !SHMDeviceOperationIsValid(operation, mapping->Context, NULL, SHMDeviceOperationSubmit)) {
        *countOut = 0;
        return NULL;
    }
    
    if (mapping->State != SHMDeviceMappingReady) {
        *countOut = 0;
        return NULL;
    }
    
    // A translated bounce mapping reaches whole granules, but only the payload
    // belongs to the request. Publish that one segment instead of the padded
    // span, so the driver never programs the private padding.
    if (__SHMDeviceMappingIsTranslatedBounce(mapping)) {
        segments = &mapping->Stream.PayloadSegment;
        count = 1;
    } else {
        segments = DMAAddressMappingSegments(mapping->AddressMapping, &count);
    }

    // The IOMMU unit can fail after the token was accepted. Check again before
    // handing out the segment list. The check also passes any failure it finds
    // on to the recovery code. No IOMMU lock is held at this point.
    if (SHMDeviceOperationCheck(operation) != OS_EOK) {
        *countOut = 0;
        return NULL;
    }

    *countOut = count;
    return segments;
}
