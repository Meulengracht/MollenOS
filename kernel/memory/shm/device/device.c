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

//#define __TRACE

#include <shm/device.h>
#include <shm/access.h>
#include <arch/utils.h>
#include <ddk/barrier.h>
#include <handles/global.h>
#include <handles/handle.h>
#include <heap.h>
#include <assert.h>
#include <debug.h>
#include <limits.h>
#include <machine.h>
#include <shm/shm.h>
#include <shm/view.h>
#include <string.h>

#include "private.h"

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
 * @brief Check whether cache work on this range would also hit bytes outside it.
 *
 * A cache line is the smallest block the CPU cache handles, commonly 64 bytes.
 * Before a non-coherent device writes, the CPU must discard (invalidate) every
 * cache line that covers the range. If a line reaches past the range, that
 * would also throw away recent CPU writes to the neighboring bytes. The caller
 * uses bounce pages for such ranges. Coherent devices and uncached pool memory
 * need no cache work. When the device only reads, the CPU writes cached data
 * back to RAM and keeps the lines, so neighboring bytes are safe.
 */
static bool
__SharesCacheLines(
    _In_ const struct SHMBuffer*        buffer,
    _In_ const struct SHMDeviceContext* context,
    _In_ size_t                         offset,
    _In_ size_t                         length,
    _In_ enum DMADirection              direction)
{
    size_t line;
    size_t start;

    if (context->CachePolicy == SHMDeviceCacheCoherent || direction == DMAToDevice) {
        return false;
    }

    if (SHMBufferUncached(buffer)) {
        return false;
    }

    // Pages start on cache line boundaries, so only the position inside the
    // buffer's pages decides the alignment.
    line = CpuDataCacheLineSize();
    start = buffer->Offset + offset;
    return start % line != 0 || length % line != 0;
}

/**
 * @brief Build a list of physical blocks (extents) for the requested bytes of
 *        a page list.
 *
 * A page address of zero means the page was never allocated. Walking the page
 * list directly lets us reject such pages and keep every entry inside the
 * requested range. Pages that are next to each other in physical memory are
 * joined into one entry. This works for both buffer pages and bounce pages.
 *
 * @param start Byte offset into the page list.
 * @param extents Storage for one entry per page touched by the range.
 * @return OS_EOK, or OS_EINCOMPLETE if a page in the range was never allocated.
 */
static oserr_t
__GatherExtents(
    _In_  const paddr_t* pages,
    _In_  size_t         start,
    _In_  size_t         length,
    _In_  size_t         pageSize,
    _Out_ SHMSG_t*       extents,
    _Out_ int*           countOut)
{
    size_t  page = start / pageSize;
    size_t  pageOffset = start % pageSize;
    size_t  remaining = length;
    size_t  chunk;
    paddr_t address;
    int     count = 0;

    while (remaining != 0) {
        // SHM uses address zero for a page that was never allocated.
        if (pages[page] == 0) {
            return OS_EINCOMPLETE;
        }

        address = pages[page] + pageOffset;
        chunk = pageSize - pageOffset;
        if (chunk > remaining) {
            chunk = remaining;
        }

        if (count != 0 && extents[count - 1].Address + extents[count - 1].Length == address) {
            extents[count - 1].Length += chunk;
        } else {
            extents[count] = (SHMSG_t){ .Address = address, .Length = chunk };
            count++;
        }

        remaining -= chunk;
        pageOffset = 0;
        page++;
    }

    *countOut = count;
    return OS_EOK;
}

/**
 * @brief Map the buffer's pages into kernel memory for copying.
 *
 * The process can remove its own view of the buffer while the device is still
 * using it. This kernel view stays valid until the device mapping is released,
 * so copying to and from the bounce pages keeps working.
 */
static oserr_t
__MapSourceView(
    _In_    struct SHMBuffer*  buffer,
    _In_    size_t             offset,
    _In_    size_t             length,
    _In_    size_t             pageSize,
    _InOut_ SHMDeviceBounce_t* bounce)
{
    size_t       start = buffer->Offset + offset;
    unsigned int flags = MAPPING_COMMIT | MAPPING_PERSISTENT;

    // Use the same cache setting as the buffer's other views. Mixed settings
    // on the same pages can make the views see different data.
    if (SHMBufferUncached(buffer)) {
        flags |= MAPPING_NOCACHE;
    }

    bounce->SourceOffset = start % pageSize;
    bounce->SourceMappingLength = DIVUP(bounce->SourceOffset + length, pageSize) * pageSize;
    return MemorySpaceMap(
        GetCurrentMemorySpace(),
        &(struct MemorySpaceMapOptions) {
            .SHMTag = buffer->ID,
            .Pages = &buffer->Pages[start / pageSize],
            .Length = bounce->SourceMappingLength,
            .Mask = __MASK,
            .Flags = flags,
            .PlacementFlags = MAPPING_PHYSICAL_FIXED | MAPPING_VIRTUAL_GLOBAL
        },
        &bounce->SourceMapping
    );
}

/**
 * @brief Allocate bounce pages from the device pool and build their extents.
 *
 * The pool is chosen at boot from memory that devices can reach. On ARM64 the
 * kernel views of it are uncached. The caller must still check the device
 * addresses after translating them. The pages are zeroed so a device cannot
 * read data left by an earlier user. If the pool is full, the mapping fails.
 * Ordinary memory is not used as a fallback, because the device may not be
 * able to reach it.
 *
 * @param extents Storage for one entry per bounce page. Filled on success.
 */
static oserr_t
__AllocateBouncePages(
    _In_    size_t                           length,
    _In_    size_t                           pageSize,
    _In_    const struct DMAMapRequirements* requirements,
    _InOut_ SHMDeviceBounce_t*               bounce,
    _Out_   SHMSG_t*                         extents,
    _Out_   int*                             extentCountOut)
{
    paddr_t*     pages;
    size_t       pageCount = DIVUP(length, pageSize);
    unsigned int flags = MAPPING_COMMIT | MAPPING_PERSISTENT | MAPPING_CLEAN;
    uint64_t     alignment = requirements != NULL ? requirements->AddressAlignment : 0;
    oserr_t      status;

    if (alignment > SIZE_MAX) {
        return OS_ENOTSUPPORTED;
    }

    status = SystemMemoryAllocateDevice(
        &GetMachine()->PhysicalMemory,
        pageCount * pageSize,
        (size_t)alignment,
        0,
        &bounce->Allocation
    );
    if (status != OS_EOK) {
        return status;
    }

    // MemorySpaceMap takes the pages as a list, one entry per page.
    pages = kmalloc(pageCount * sizeof(paddr_t));
    if (pages == NULL) {
        return OS_EOOM;
    }

    for (size_t i = 0; i < pageCount; i++) {
        pages[i] = bounce->Allocation.PhysicalBase + ((paddr_t)i * pageSize);
    }

    if (!bounce->Allocation.Cached) {
        flags |= MAPPING_NOCACHE;
    }

    status = MemorySpaceMap(
        GetCurrentMemorySpace(),
        &(struct MemorySpaceMapOptions) {
            .Pages = pages,
            .Length = bounce->Allocation.Length,
            .Mask = __MASK,
            .Flags = flags,
            .PlacementFlags = MAPPING_PHYSICAL_FIXED | MAPPING_VIRTUAL_GLOBAL
        },
        &bounce->BounceMapping
    );
    if (status != OS_EOK) {
        kfree(pages);
        return status;
    }

    status = __GatherExtents(
        pages,
        0,
        length,
        pageSize,
        extents,
        extentCountOut
    );
    kfree(pages);
    return status;
}

/**
 * @brief Prepare bounce pages for a new mapping.
 *
 * Bounce pages are temporary pages that the device can reach. They are used
 * when the device cannot reach the buffer's own addresses, when the buffer
 * does not meet the segment alignment rules, or when cache invalidation would
 * hit neighboring bytes outside the range. The caller must still check the
 * device addresses of the bounce pages. On success, extents describes the
 * bounce pages.
 */
static oserr_t
__PrepareBounce(
    _In_    struct SHMBuffer*                buffer,
    _In_    size_t                           offset,
    _In_    size_t                           length,
    _In_    size_t                           pageSize,
    _In_    const struct DMAMapRequirements* requirements,
    _InOut_ SHMDeviceBounce_t*               bounce,
    _Out_   SHMSG_t*                         extents,
    _Out_   int*                             extentCountOut)
{
    oserr_t status;

    status = __MapSourceView(buffer, offset, length, pageSize, bounce);
    if (status != OS_EOK) {
        return status;
    }

    status = __AllocateBouncePages(
        length,
        pageSize,
        requirements,
        bounce,
        extents,
        extentCountOut
    );
    if (status != OS_EOK) {
        SHMDeviceReleaseBounce(bounce);
    }
    return status;
}

/**
 * @brief Decide whether a range with a cache line conflict can use bounce pages.
 *
 * Only bounce mappings can. Direct-only mappings fail with OS_ENOTSUPPORTED.
 * An address or segment error from the original pages is reported ahead of
 * that, and any other error passes through unchanged.
 */
static oserr_t
__CheckCacheLineFallback(
    _In_  struct SHMDeviceContext*         context,
    _In_  const SHMSG_t*                   extents,
    _In_  int                              extentCount,
    _In_  enum SHMDeviceMappingKind        kind,
    _In_  const struct DMAMapRequirements* requirements)
{
    uint32_t segmentCount = 0;
    oserr_t  status;

    // Report an address or segment error before the cache line error. For a
    // bounce mapping, bounce pages solve both problems, so carry on.
    status = SHMDeviceContextWalkSegments(
        context,
        extents,
        extentCount,
        requirements,
        NULL,
        &segmentCount
    );
    (void)segmentCount;
    if (status != OS_EOK &&
        status != OS_ENOENT &&
        status != OS_ENOTSUPPORTED) {
        return status;
    }

    if (kind == SHMDeviceMappingStreamingBounce) {
        return OS_EOK;
    }
    return status == OS_EOK ? OS_ENOTSUPPORTED : status;
}

/**
 * @brief Check the device addresses of an allocation against the requirements.
 *
 * The pool already checked the physical addresses. The device can see them at
 * different addresses, or split into several pieces, so the requirements are
 * checked again on the device layout before the device is allowed to use it.
 */
static oserr_t
__CheckRequirements(
    _In_ const struct DMAAddressMapping* mapping,
    _In_ const struct DMARequirements*   requirements)
{
    const struct DMASegment* segments;
    uint32_t                 count;

    segments = DMAAddressMappingSegments(mapping, &count);
    if (requirements->Contiguous && count != 1) {
        return OS_ENOTSUPPORTED;
    }

    // Check the alignment of the first device address.
    if (requirements->Alignment != 0 && (segments[0].Address & (requirements->Alignment - 1)) != 0) {
        return OS_ENOTSUPPORTED;
    }

    // A segment must not cross a boundary. It crosses one when its first and
    // last byte fall in different boundary-sized blocks. The upper address bits
    // tell which block a byte is in.
    for (uint32_t i = 0; requirements->Boundary != 0 && i < count; i++) {
        const struct DMASegment* segment = &segments[i];
        uint64_t                 last = segment->Address + (segment->Length - 1);
        if (((segment->Address ^ last) & ~(requirements->Boundary - 1)) != 0) {
            return OS_ENOTSUPPORTED;
        }
    }
    return OS_EOK;
}

/**
 * @brief Prepare and check a buffer range. The device cannot use it yet.
 *
 * __MapBuffer takes the references this needs before calling. This function
 * records the physical ranges that cache syncs will use and builds the device
 * addresses. If the device cannot address the buffer pages, or the pages break
 * its limits, it switches to bounce pages. Mappings that require direct access
 * fail instead.
 */
static oserr_t
__PrepareMapping(
    _In_  struct SHMBuffer*                buffer,
    _In_  struct SHMDeviceContext*         context,
    _In_  size_t                           offset,
    _In_  size_t                           length,
    _In_  enum DMADirection                direction,
    _In_  enum SHMDeviceMappingKind        kind,
    _In_  const struct DMAMapRequirements* requirements,
    _In_  const struct DMARequirements*    allocationRequirements,
    _Out_ struct SHMDeviceMapping**        mappingOut)
{
    struct SHMDeviceMapping*   mapping = NULL;
    struct DMAAddressMapping* addressMapping = NULL;
    SHMDeviceBounce_t         bounce = { 0 };
    SHMSG_t*                  extents;
    size_t                    pageSize = GetMemorySpacePageSize();
    size_t                    pageCount;
    int                       extentCount;
    bool                      needsBounce;
    oserr_t                   status;
    oserr_t                   cleanupStatus;

    // Reserve one entry for each page the range touches. A bounce copy starts
    // on a page boundary, so it never needs more entries than this.
    pageCount = DIVUP((buffer->Offset + offset) % pageSize + length, pageSize);
    extents = kmalloc(pageCount * sizeof(SHMSG_t));
    if (extents == NULL) {
        return OS_EOOM;
    }

    MutexLock(&buffer->Mutex);
    status = __GatherExtents(buffer->Pages, buffer->Offset + offset, length, pageSize, extents, &extentCount);
    MutexUnlock(&buffer->Mutex);
    if (status != OS_EOK) {
        kfree(extents);
        return status;
    }

    needsBounce = __SharesCacheLines(buffer, context, offset, length, direction);
    if (needsBounce) {
        // Check the original pages before allocating bounce pages for the
        // cache line conflict. A direct-only mapping fails here.
        status = __CheckCacheLineFallback(
            context,
            extents,
            extentCount,
            kind,
            requirements
        );
        if (status != OS_EOK) {
            goto cleanup;
        }
    }

    // Finish all storage and layout work before the device can see any
    // address. Preparation may fall back to bounce pages and try again.
    // Installation runs only once, on the layout that was finally chosen.
    mapping = kmalloc(sizeof(struct SHMDeviceMapping));
    if (mapping == NULL) {
        status = OS_EOOM;
        goto cleanup;
    }

    memset(mapping, 0, sizeof(struct SHMDeviceMapping));
    atomic_init(&mapping->Lifetime.References, 1);

    if (!needsBounce) {
        // Check the physical ranges and build the list of segments as the
        // device sees them.
        status = DMAAddressMappingPrepare(
            context->AddressSpace,
            extents,
            extentCount,
            context->AddressLimit,
            direction,
            requirements,
            &addressMapping
        );
        if (status != OS_EOK) {
            // Fall back to bounce pages only when the device cannot address
            // these pages or their segments break its limits. Pass any other
            // error through unchanged.
            if (kind != SHMDeviceMappingStreamingBounce ||
                (status != OS_ENOENT && status != OS_ENOTSUPPORTED)) {
                goto cleanup;
            }
            needsBounce = true;
        }
    }

    if (needsBounce) {
        status = __PrepareBounce(
            buffer,
            offset,
            length,
            pageSize,
            requirements,
            &bounce,
            extents,
            &extentCount
        );
        if (status != OS_EOK) {
            goto cleanup;
        }
        status = DMAAddressMappingPrepare(
            context->AddressSpace,
            extents,
            extentCount,
            context->AddressLimit,
            direction,
            requirements,
            &addressMapping
        );
        if (status != OS_EOK) {
            goto cleanup;
        }
    }

    if (allocationRequirements != NULL) {
        status = __CheckRequirements(addressMapping, allocationRequirements);
        if (status != OS_EOK) {
            goto cleanup;
        }
    }

    // The mapping is fully filled in before __MapBuffer links it so it can be
    // cancelled. The references to the source buffer and the context stay valid
    // through installation and any cleanup after a hardware stop. Nobody may
    // use the mapping until __MapBuffer marks it Ready.
    SHMAccessInitialize(&mapping->Access, buffer, offset, length);
    mapping->Context = context;
    mapping->Offset = offset;
    mapping->Length = length;
    mapping->Cached = needsBounce ? bounce.Allocation.Cached : !SHMBufferUncached(buffer);
    mapping->Kind = kind;
    mapping->State = SHMDeviceMappingPreparing;

    if (__SHMDeviceMappingIsStreaming(mapping)) {
        mapping->Stream = (SHMDeviceStream_t) {
            .Bounce = bounce
        };
    } else {
        mapping->Persistent.Uses = 0;
    }

    mapping->Direction = direction;
    mapping->Extents = extents;
    mapping->ExtentCount = extentCount;
    mapping->AddressMapping = addressMapping;

    *mappingOut = mapping;
    return OS_EOK;

cleanup:
    // Install has not run, so the device never saw this layout. Releasing a
    // prepared mapping cannot fail, so check that this holds before freeing
    // the bounce pages and the extent list. The original error is kept.
    if (addressMapping != NULL) {
        cleanupStatus = DMAAddressMappingRelease(&addressMapping);
        if (cleanupStatus != OS_EOK) {
            ERROR("__PrepareMapping: prepared address release failed: %u (original status %u)",
                  cleanupStatus, status);
            assert(cleanupStatus == OS_EOK);
        }
    }
    SHMDeviceReleaseBounce(&bounce);
    if (mapping != NULL) {
        kfree(mapping);
    }
    kfree(extents);
    return status;
}

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
    struct SHMBuffer*        buffer;
    struct SHMReference      source;
    struct SHMDeviceMapping* mapping = NULL;
    unsigned int             scope;
    oserr_t                  status;
    oserr_t                  cleanupStatus;

    // The output slot must be empty. Otherwise the existing mapping would be
    // lost.
    if (context == NULL || mappingOut == NULL) {
        return OS_EINVALPARAMS;
    }
    if (*mappingOut != NULL) {
        return OS_EBUSY;
    }

    if (!SHMDeviceOperationIsValid(operation, context, NULL, SHMDeviceOperationSubmit)) {
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
            break;
        default:
            return OS_EINVALPARAMS;
    }

    // Take a reference on the buffer. It keeps the pages allocated for as long
    // as the device may use them, because SHM frees a buffer's pages when the
    // last reference is released.
    status = SHMResolve(shmID, &source);
    if (status != OS_EOK) {
        return OS_ENOENT;
    }

    buffer = source.Buffer;

    // Buffers backed by regions would need device mappings that understand
    // regions. This code does not provide that.
    if (buffer->Flags & SHM_REGIONS) {
        __ReleaseMemoryHandleReference(source.GlobalHandleID, __func__);
        return OS_ENOTSUPPORTED;
    }

    // The handle must allow the kind of access the device will do.
    scope = direction == DMAToDevice ? GrantTokenScopeRead : GrantTokenScopeWrite;

    if (direction == DMABidirectional) {
        scope |= GrantTokenScopeRead;
    }

    if ((source.Scope & scope) != scope) {
        status = OS_EPERMISSIONS;
        goto release;
    }

    if (length == 0 || !__SHMRangeContains(0, source.Length, offset, length)) {
        status = OS_EINVALPARAMS;
        goto release;
    }

    // Range reservations for the CPU and the device are tracked by position
    // in the backing buffer. The caller's offset is relative to its own handle,
    // which can cover only part of the buffer, so add the handle's start here.
    offset += source.Offset;

    status = __CheckBufferKind(buffer, context, kind);
    if (status != OS_EOK) {
        goto release;
    }

    // Take a context reference. The mapping needs the context for as long as
    // its segments can be read.
    status = SHMDeviceContextAcquire(context);
    if (status != OS_EOK) {
        goto release;
    }

    status = __PrepareMapping(
        buffer,
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

oserr_t
SHMDeviceAllocate(
    _In_    SHMDeviceOperation_t*         operation,
    _In_    struct SHMDeviceContext*      context,
    _In_    const struct DMARequirements* requirements,
    _Out_   SHMHandle_t*                  bufferOut,
    _InOut_ struct SHMDeviceMapping**     mappingOut)
{
    struct SHMDeviceMapping* mapping = NULL;
    SHMHandle_t              handle;
    size_t                   pageSize = GetMemorySpacePageSize();
    oserr_t                  status;
    oserr_t                  cleanupStatus;

    if (context == NULL || requirements == NULL || bufferOut == NULL || mappingOut == NULL) {
        return OS_EINVALPARAMS;
    }

    if (!SHMDeviceOperationIsValid(operation, context, NULL, SHMDeviceOperationSubmit)) {
        return OS_EINVALPARAMS;
    }

    if (*mappingOut != NULL) {
        return OS_EBUSY;
    }

    // The length is rounded up to whole pages below, which must not wrap.
    if (!__RequirementsValid(requirements) || requirements->Length > SIZE_MAX - pageSize) {
        return OS_EINVALPARAMS;
    }

    // Pool pages are contiguous and zeroed, and their physical addresses
    // already meet the alignment and boundary requirements. When the device
    // cannot see the CPU caches, the pool is mapped uncached, so no cache
    // syncs are needed. Whole pages keep the memory from sharing cache lines
    // with anything else. If the pool is full, fail. Ordinary RAM is no
    // substitute, because the device may not be able to reach it.
    status = SHMCreateDeviceBuffer(
        DIVUP(requirements->Length, pageSize) * pageSize,
        (size_t)requirements->Alignment,
        (size_t)requirements->Boundary,
        &handle
    );
    if (status != OS_EOK) {
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
        NULL,
        requirements,
        &mapping
    );
    if (status != OS_EOK) {
        ERROR("SHMDeviceAllocate: __MapBuffer buffer %u failed: %u", handle.ID, status);
        goto detach;
    }

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
__MaintainRange(
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

static inline void
__PrepareForDevice(
    _In_ struct SHMDeviceMapping* mapping,
    _In_ size_t                   offset,
    _In_ size_t                   length)
{
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
        if (mapping->Direction == DMAToDevice) {
            __MaintainRange(mapping, offset, length, CpuDataCacheClean);
        } else {
            __MaintainRange(mapping, offset, length, CpuDataCacheCleanInvalidate);
        }
    }
}

void
SHMDeviceCompleteForCpu(
    _In_ struct SHMDeviceMapping* mapping,
    _In_ size_t                   offset,
    _In_ size_t                   length)
{
    // The driver must already have confirmed that the device finished or
    // stopped. The barrier makes that check happen before the CPU reads, so
    // the reads see what the device wrote.
    dma_mb();

    // While the device was working, the CPU may have loaded old values of this
    // memory into its cache, for example through speculative reads. Discard
    // those cached lines for the bytes the device wrote. Mapping setup already
    // checked that these cache lines do not reach outside the mapping.
    if (mapping->Cached && mapping->Context->CachePolicy == SHMDeviceCacheNonCoherent &&
        __DeviceWrites(mapping)) {
        __MaintainRange(mapping, offset, length, CpuDataCacheInvalidate);
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

    if (mapping->State != SHMDeviceMappingReady) {
        return mapping->State == SHMDeviceMappingStopped ? OS_ECANCELLED : OS_EBUSY;
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
        memcpy(
            (void*)(mapping->Stream.Bounce.BounceMapping + offset),
            (const void*)(mapping->Stream.Bounce.SourceMapping + mapping->Stream.Bounce.SourceOffset + offset),
            length
        );
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
    size_t offset;

    // A transfer can stop before the device wrote any bytes, so a length of
    // zero is allowed. The CPU can then use the buffer again.
    if (mapping == NULL || !__SHMDeviceMappingIsStreaming(mapping)) {
        return OS_EINVALPARAMS;
    }

    if (mapping->State != SHMDeviceMappingReady) {
        return mapping->State == SHMDeviceMappingStopped ? OS_ECANCELLED : OS_EBUSY;
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
        memcpy(
            (void*)(mapping->Stream.Bounce.SourceMapping + mapping->Stream.Bounce.SourceOffset + offset),
            (const void*)(mapping->Stream.Bounce.BounceMapping + offset),
            length
        );
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

    if (mapping->State != SHMDeviceMappingReady) {
        return mapping->State == SHMDeviceMappingStopped ? OS_ECANCELLED : OS_EBUSY;
    }
    if (mapping->Persistent.Uses == UINT_MAX) {
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
    if (mapping == NULL || mapping->Kind != SHMDeviceMappingPersistent) {
        return OS_EINVALPARAMS;
    }

    if (mapping->State != SHMDeviceMappingReady) {
        return mapping->State == SHMDeviceMappingStopped ? OS_ECANCELLED : OS_EBUSY;
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
    return DMAAddressMappingSegments(mapping->AddressMapping, countOut);
}
