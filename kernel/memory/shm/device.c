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
#include <limits.h>
#include <machine.h>
#include <shm/shm.h>
#include <shm/view.h>
#include <string.h>

#include "private.h"
#include "device_private.h"

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

/**
 * Temporary memory a device uses instead of the buffer itself. Copy is zero
 * when the mapping uses the buffer directly. Both views are kernel mappings,
 * so the sync calls can copy between them with plain memory copies.
 */
struct __SHMDeviceBounce {
    vaddr_t                       Source;       // Kernel view of the buffer's pages.
    size_t                        SourceLength;
    size_t                        SourceOffset; // Where the mapped range starts inside that view.
    vaddr_t                       Copy;         // Kernel view of the bounce pages. The range starts at 0.
    struct SystemMemoryAllocation Allocation;   // Allocation used for the bounce pages.
};

/**
 * Transfer state and copy storage for serialized streaming DMA. Calls on the
 * same stream must not overlap. Access.Phase records whether a transfer is active.
 */
struct __SHMDeviceStream {
    size_t                   Offset;
    size_t                   Length;
    struct __SHMDeviceBounce Bounce;
};

/**
 * Buffer and context references with fixed address and cache information.
 * Extents and Segments describe the actual device backing, including bounce
 * pages. Segments contains SegmentCount entries. Kind selects streaming data
 * or a persistent use count. Access holds the buffer reference and range state.
 * Each mapping requires one allocation, with no allocation during transfers.
 * Calls on each mapping must be serialized.
 */
struct SHMDeviceMapping {
    struct SHMAccess               Access;
    struct SHMDeviceMapping*       QuarantineNext;
    bool                           Cached;
    struct SHMDeviceContext*       Context;
    size_t                         Offset;
    size_t                         Length;
    enum DMADirection              Direction;
    enum SHMDeviceMappingKind      Kind;
    union {
        struct __SHMDeviceStream Stream;
        struct {
            unsigned int Uses;
        } Persistent;
    };
    SHMSG_t*                       Extents;
    int                            ExtentCount;
    uint32_t                       SegmentCount;
    struct DMASegment              Segments[];
};

static bool
__IsStreaming(
    _In_ const struct SHMDeviceMapping* mapping)
{
    return mapping->Kind == SHMDeviceMappingStreamingBounce ||
        mapping->Kind == SHMDeviceMappingStreamingDirect;
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

    // Shared mappings have no cache handoffs. Pool allocations describe the
    // cache policy of every alias. Other SHM storage cannot guarantee this.
    if (buffer->Allocation.Owner == NULL) {
        return OS_ENOTSUPPORTED;
    }

    if (buffer->Allocation.Cached && context->CachePolicy == SHMDeviceCacheNonCoherent) {
        return OS_ENOTSUPPORTED;
    }
    return OS_EOK;
}

/**
 * @brief Check whether cache operations would reach bytes outside the range.
 *
 * Writes from non-coherent devices require invalidating whole cache lines.
 * If a line extends outside the range, invalidation could discard CPU changes
 * to those bytes. Such ranges must use bounce pages. Coherent devices and
 * uncached pool memory need no cache operations. For devices that only read,
 * cache lines are written back without discarding data.
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

    // Pages start on a line boundary, so the position inside the buffer
    // decides alignment.
    line = CpuDataCacheLineSize();
    start = buffer->Offset + offset;
    return start % line != 0 || length % line != 0;
}

/**
 * @brief Build physical blocks for the requested bytes of a page list.
 *
 * SHMBuildSG reports missing pages as address zero and extends its last entry
 * to the end of the final page. A device must only see allocated memory within
 * the requested range, so this walks the page list directly. Consecutive pages
 * with adjacent physical addresses are joined, as in SHMBuildSG, to reduce the
 * entry count. Used for both buffer pages and bounce pages.
 *
 * @param start First byte, counted from the start of the first page.
 * @param extents Room for one entry per page touched by the range.
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
        // SHM marks a page that was never allocated with address zero.
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

static void
__ReleaseBounce(
    _In_ struct __SHMDeviceBounce* bounce)
{
    // Both views use MAPPING_PERSISTENT, so unmapping leaves their pages
    // allocated. Return bounce pages to the pool separately.
    if (bounce->Copy != 0) {
        (void)MemorySpaceUnmap(GetDomainMemorySpace(), bounce->Copy, bounce->Allocation.Length);
        bounce->Copy = 0;
    }

    (void)SystemMemoryReleaseAllocation(&bounce->Allocation);
    if (bounce->Source != 0) {
        (void)MemorySpaceUnmap(GetDomainMemorySpace(), bounce->Source, bounce->SourceLength);
        bounce->Source = 0;
    }
}

/**
 * @brief Map the buffer's pages into kernel memory for copying.
 *
 * The process that mapped the buffer may have removed its view. Copies need
 * a kernel view that stays valid for the lifetime of the device mapping.
 * SHM uses the same kind of view for IPC buffers.
 */
static oserr_t
__MapSourceView(
    _In_    struct SHMBuffer*         buffer,
    _In_    size_t                    offset,
    _In_    size_t                    length,
    _In_    size_t                    pageSize,
    _InOut_ struct __SHMDeviceBounce* bounce)
{
    size_t       start = buffer->Offset + offset;
    unsigned int flags = MAPPING_COMMIT | MAPPING_PERSISTENT;

    // The cache policy must match the buffer's other views so they see the
    // same data.
    if (SHMBufferUncached(buffer)) {
        flags |= MAPPING_NOCACHE;
    }

    bounce->SourceOffset = start % pageSize;
    bounce->SourceLength = DIVUP(bounce->SourceOffset + length, pageSize) * pageSize;
    return MemorySpaceMap(
        GetCurrentMemorySpace(),
        &(struct MemorySpaceMapOptions) {
            .SHMTag = buffer->ID,
            .Pages = &buffer->Pages[start / pageSize],
            .Length = bounce->SourceLength,
            .Mask = __MASK,
            .Flags = flags,
            .PlacementFlags = MAPPING_PHYSICAL_FIXED | MAPPING_VIRTUAL_GLOBAL
        },
        &bounce->Source
    );
}

/**
 * @brief Allocate bounce pages from the device pool and build their extents.
 *
 * The pool's memory is selected at boot to be reachable by devices. On ARM64
 * it has no cached views. The caller must still check reachability. Pages are
 * zeroed so a device cannot read data from a previous allocation.
 * A full pool fails the mapping without falling back to other memory.
 *
 * @param extents Room for one entry per bounce page. Overwritten on success.
 */
static oserr_t
__AllocateCopy(
    _In_    size_t                           length,
    _In_    size_t                           pageSize,
    _In_    const struct DMAMapRequirements* requirements,
    _InOut_ struct __SHMDeviceBounce*        bounce,
    _Out_   SHMSG_t*                         extents,
    _Out_   int*                             extentCountOut)
{
    paddr_t*           pages;
    size_t             pageCount = DIVUP(length, pageSize);
    unsigned int       flags = MAPPING_COMMIT | MAPPING_PERSISTENT | MAPPING_CLEAN;
    uint64_t           alignment = requirements != NULL ? requirements->AddressAlignment : 0;
    oserr_t            status;

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
        &bounce->Copy
    );
    if (status == OS_EOK) {
        status = __GatherExtents(pages, 0, length, pageSize, extents, extentCountOut);
    }
    kfree(pages);
    return status;
}

/**
 * @brief Prepare bounce pages for a new mapping.
 *
 * Used when the device cannot reach part of the buffer, segment alignment
 * fails, or cache work would reach bytes outside the range. The caller must
 * validate the translated bounce segments. On success extents describes the
 * bounce pages instead of the buffer.
 */
static oserr_t
__PrepareBounce(
    _In_    struct SHMBuffer*                buffer,
    _In_    size_t                           offset,
    _In_    size_t                           length,
    _In_    size_t                           pageSize,
    _In_    const struct DMAMapRequirements* requirements,
    _InOut_ struct __SHMDeviceBounce*        bounce,
    _Out_   SHMSG_t*                         extents,
    _Out_   int*                             extentCountOut)
{
    oserr_t status;

    status = __MapSourceView(buffer, offset, length, pageSize, bounce);
    if (status != OS_EOK) {
        return status;
    }

    status = __AllocateCopy(length, pageSize, requirements, bounce, extents, extentCountOut);
    if (status != OS_EOK) {
        __ReleaseBounce(bounce);
    }
    return status;
}

/**
 * @brief Build the device segments for a buffer range into a new mapping.
 *
 * SHMDeviceMap handles the references. This function stores the physical
 * blocks in the mapping for cache operations. If the device cannot use the
 * buffer's pages for the range, it uses bounce pages unless the caller
 * requires direct access to the buffer.
 */
static oserr_t
__CreateMapping(
    _In_  struct SHMBuffer*                buffer,
    _In_  struct SHMDeviceContext*         context,
    _In_  size_t                           offset,
    _In_  size_t                           length,
    _In_  enum DMADirection                direction,
    _In_  enum SHMDeviceMappingKind        kind,
    _In_  const struct DMAMapRequirements* requirements,
    _Out_ struct SHMDeviceMapping**        mappingOut)
{
    struct SHMDeviceMapping* mapping = NULL;
    struct __SHMDeviceBounce bounce = { 0 };
    SHMSG_t*                 extents;
    size_t                   pageSize = GetMemorySpacePageSize();
    size_t                   pageCount;
    size_t                   maxSegments;
    int                      extentCount;
    uint32_t                 segmentCount;
    bool                     needsBounce;
    oserr_t                  status;

    // Allow one extent per page touched by the range. Bounce storage needs
    // at most this many pages because it starts on a page boundary.
    pageCount = DIVUP((buffer->Offset + offset) % pageSize + length, pageSize);
    extents = kmalloc(pageCount * sizeof(SHMSG_t));
    if (extents == NULL) {
        return OS_EOOM;
    }

    // Pages are filled in on demand under this lock, so hold it while reading.
    MutexLock(&buffer->Mutex);
    status = __GatherExtents(buffer->Pages, buffer->Offset + offset, length, pageSize, extents, &extentCount);
    MutexUnlock(&buffer->Mutex);
    if (status != OS_EOK) {
        goto cleanup;
    }

    // Bounce pages may satisfy address and alignment constraints that the
    // buffer's original pages cannot meet.
    status = SHMDeviceContextWalkSegments(
        context,
        extents,
        extentCount,
        requirements,
        NULL,
        &segmentCount
    );
    if (status != OS_EOK && status != OS_ENOENT && status != OS_ENOTSUPPORTED) {
        goto cleanup;
    }
    needsBounce = status != OS_EOK || __SharesCacheLines(buffer, context, offset, length, direction);

    // Preserve the error for unreachable bytes. Other reasons to bounce
    // mean this range cannot be used directly.
    if (needsBounce && kind != SHMDeviceMappingStreamingBounce) {
        if (status == OS_EOK) {
            status = OS_ENOTSUPPORTED;
        }
        goto cleanup;
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

        status = SHMDeviceContextWalkSegments(
            context,
            extents,
            extentCount,
            requirements,
            NULL,
            &segmentCount
        );
        if (status != OS_EOK) {
            goto cleanup;
        }
    }

    // Guard the size calculation, which could otherwise wrap on 32-bit kernels.
    maxSegments = (SIZE_MAX - sizeof(struct SHMDeviceMapping)) / sizeof(struct DMASegment);
    if (segmentCount > maxSegments) {
        status = OS_EOOM;
        goto cleanup;
    }
    mapping = kmalloc(sizeof(struct SHMDeviceMapping) + segmentCount * sizeof(struct DMASegment));
    if (mapping == NULL) {
        status = OS_EOOM;
        goto cleanup;
    }

    // The count pass validated these private extents against an immutable
    // context. Fill directly instead of repeating validation and counting.
    status = SHMDeviceContextWalkSegments(
        context,
        extents,
        extentCount,
        requirements,
        mapping->Segments,
        &segmentCount
    );
    if (status != OS_EOK) {
        goto cleanup;
    }

    SHMAccessInitialize(&mapping->Access, buffer, offset, length);
    mapping->Context = context;
    mapping->Offset = offset;
    mapping->Length = length;
    mapping->Cached = needsBounce ? bounce.Allocation.Cached : !SHMBufferUncached(buffer);
    
    mapping->Kind = kind;
    if (__IsStreaming(mapping)) {
        mapping->Stream = (struct __SHMDeviceStream) { .Bounce = bounce };
    } else if (kind == SHMDeviceMappingPersistent) {
        mapping->Persistent.Uses = 0;
    }
    mapping->Direction = direction;

    mapping->Extents = extents;
    mapping->ExtentCount = extentCount;
    mapping->SegmentCount = segmentCount;
    
    *mappingOut = mapping;
    return OS_EOK;

cleanup:
    __ReleaseBounce(&bounce);
    if (mapping != NULL) {
        kfree(mapping);
    }
    kfree(extents);
    return status;
}

oserr_t
SHMDeviceMap(
    _In_    struct SHMDeviceContext*         context,
    _In_    uuid_t                           shmID,
    _In_    size_t                           offset,
    _In_    size_t                           length,
    _In_    enum DMADirection                direction,
    _In_    enum SHMDeviceMappingKind        kind,
    _In_    const struct DMAMapRequirements* requirements,
    _InOut_ struct SHMDeviceMapping**        mappingOut)
{
    struct SHMBuffer*        buffer;
    struct SHMReference      source;
    struct SHMDeviceMapping* mapping = NULL;
    unsigned int             scope;
    oserr_t                  status;

    // The output slot must be empty. Replacing it would lose its reference.
    if (context == NULL || mappingOut == NULL) {
        return OS_EINVALPARAMS;
    }
    if (*mappingOut != NULL) {
        return OS_EBUSY;
    }

    if (!__MapRequirementsValid(requirements, length)) {
        return OS_EINVALPARAMS;
    }

    // Cache operations depend on the direction. Reject unknown values here.
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

    // This reference keeps the pages allocated while the device may use them.
    // SHM frees a buffer's pages when its last reference is released.
    status = SHMResolve(shmID, &source);
    if (status != OS_EOK) {
        return OS_ENOENT;
    }
    
    buffer = source.Buffer;
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

    // Device and CPU reservations use backing coordinates, including aliases.
    // The public offset remains relative to the caller's restricted handle.
    offset += source.Offset;

    status = __CheckBufferKind(buffer, context, kind);
    if (status != OS_EOK) {
        goto release;
    }

    // The context must stay valid while the mapping's segments can be read.
    status = SHMDeviceContextAcquire(context);
    if (status != OS_EOK) {
        goto release;
    }

    status = __CreateMapping(buffer, context, offset, length, direction, kind, requirements, &mapping);
    if (status != OS_EOK) {
        SHMDeviceContextRelease(&context);
        goto release;
    }

    mapping->Access.ReferenceID = source.GlobalHandleID;
    mapping->Access.Scope = source.Scope;
    *mappingOut = mapping;
    return OS_EOK;

release:
    (void)ReleaseGlobalHandle(source.GlobalHandleID);
    return status;
}

/**
 * @brief Reject requirements that cannot be met.
 *
 * Validate before allocating so invalid requirements are not reported as a
 * temporary shortage of suitable memory.
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

    // One block longer than the boundary would always cross it.
    if (requirements->Contiguous && requirements->Boundary != 0) {
        return requirements->Length <= requirements->Boundary;
    }
    return true;
}

/**
 * @brief Check the device's view of an allocation against the requirements.
 *
 * The pool guarantees the requirements for physical addresses. Translation
 * can offset device addresses or split the allocation across ranges, so check
 * the device addresses before returning the allocation.
 */
static oserr_t
__CheckRequirements(
    _In_ const struct SHMDeviceMapping* mapping,
    _In_ const struct DMARequirements*  requirements)
{
    const struct DMASegment* segment;
    uint64_t                 last;

    if (requirements->Contiguous && mapping->SegmentCount != 1) {
        return OS_ENOTSUPPORTED;
    }

    // Alignment applies to where the allocation starts for the device.
    if (requirements->Alignment != 0 && (mapping->Segments[0].Address & (requirements->Alignment - 1)) != 0) {
        return OS_ENOTSUPPORTED;
    }

    // A block crosses a boundary when its first and last byte differ in the
    // address bits above the boundary size.
    for (uint32_t i = 0; requirements->Boundary != 0 && i < mapping->SegmentCount; i++) {
        segment = &mapping->Segments[i];
        last = segment->Address + (segment->Length - 1);
        if (((segment->Address ^ last) & ~(requirements->Boundary - 1)) != 0) {
            return OS_ENOTSUPPORTED;
        }
    }
    return OS_EOK;
}

oserr_t
SHMDeviceAllocate(
    _In_    struct SHMDeviceContext*      context,
    _In_    const struct DMARequirements* requirements,
    _Out_   SHMHandle_t*                  bufferOut,
    _InOut_ struct SHMDeviceMapping**     mappingOut)
{
    SHMHandle_t handle;
    size_t      pageSize = GetMemorySpacePageSize();
    oserr_t     status;

    if (context == NULL || requirements == NULL || bufferOut == NULL || mappingOut == NULL) {
        return OS_EINVALPARAMS;
    }
    if (*mappingOut != NULL) {
        return OS_EBUSY;
    }

    // The length is rounded up to whole pages below, which must not wrap.
    if (!__RequirementsValid(requirements) || requirements->Length > SIZE_MAX - pageSize) {
        return OS_EINVALPARAMS;
    }

    // Pool pages are contiguous and zeroed. Their physical addresses meet the
    // alignment and boundary requirements. Views are uncached where devices
    // cannot see the CPU cache, so no sync calls are needed. Whole pages keep
    // cache lines from being shared with other memory. A full pool fails
    // without falling back to other memory.
    status = SHMCreateDeviceBuffer(
        DIVUP(requirements->Length, pageSize) * pageSize,
        (size_t)requirements->Alignment,
        (size_t)requirements->Boundary,
        &handle
    );
    if (status != OS_EOK) {
        return status;
    }

    // The CPU and device use this memory at the same time. Both must access
    // the buffer's pages, as a bounce copy would become stale.
    status = SHMDeviceMap(
        context,
        handle.ID,
        0,
        handle.Length,
        DMABidirectional,
        SHMDeviceMappingShared,
        NULL,
        mappingOut
    );
    if (status == OS_EOK) {
        status = __CheckRequirements(*mappingOut, requirements);
        if (status != OS_EOK) {
            (void)SHMDeviceUnmap(mappingOut);
        }
    }
    if (status != OS_EOK) {
        (void)SHMDetach(&handle);
        return status;
    }

    *bufferOut = handle;
    return OS_EOK;
}

oserr_t
SHMDeviceUnmap(
    _InOut_ struct SHMDeviceMapping** mapping)
{
    struct SHMDeviceMapping* owned;

    // Nothing to release. Repeated cleanup succeeds.
    if (mapping == NULL || *mapping == NULL) {
        return OS_EOK;
    }

    // Queued and active device work keep the mapping in use.
    // Explicit unmap of shared storage is the driver's confirmation of a stop.
    if ((*mapping)->Access.Phase != SHMAccessIdle) {
        return OS_EBUSY;
    }

    // Clear the slot before releasing so repeated cleanup cannot release the
    // buffer or context references twice.
    owned = *mapping;
    *mapping = NULL;

    SHMDeviceContextRelease(&owned->Context);
    
    // Unmap the buffer view before releasing the reference that keeps its pages.
    if (__IsStreaming(owned)) {
        __ReleaseBounce(&owned->Stream.Bounce);
    }
    (void)ReleaseGlobalHandle(owned->Access.ReferenceID);
    kfree(owned->Extents);
    kfree(owned);
    return OS_EOK;
}

/**
 * @brief Run a cache operation on the physical bytes in a sync range.
 *
 * The range must lie inside the mapping. Sync offsets are relative to the
 * mapping, so walk its physical blocks to find the addresses for the cache
 * operation.
 */
static void
__MaintainRange(
    _In_ const struct SHMDeviceMapping* mapping,
    _In_ size_t                         offset,
    _In_ size_t                         length,
    _In_ void                         (*operation)(uintptr_t, size_t))
{
    const SHMSG_t* extent;
    size_t         skip = offset;
    size_t         chunk;

    for (int i = 0; i < mapping->ExtentCount && length != 0; i++) {
        extent = &mapping->Extents[i];
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
    // CPU writes to the buffer must complete before cache operations and
    // before the device starts the transfer.
    dma_mb();

    // Non-coherent devices read memory directly. Write cached data back before
    // device reads. Before device writes, also invalidate the lines so stale
    // CPU data cannot overwrite the result. Uncached memory still needs
    // ordering and access checks, but has no cache lines to maintain.
    // Bounce storage uses its pool's cache policy.
    if (mapping->Cached && mapping->Context->CachePolicy == SHMDeviceCacheNonCoherent) {
        if (mapping->Direction == DMAToDevice) {
            __MaintainRange(mapping, offset, length, CpuDataCacheClean);
        } else {
            __MaintainRange(mapping, offset, length, CpuDataCacheCleanInvalidate);
        }
    }
}

static inline void
__CompleteForCpu(
    _In_ struct SHMDeviceMapping* mapping,
    _In_ size_t                   offset,
    _In_ size_t                   length)
{
    // The driver must have confirmed that the device finished or stopped.
    // Order that check before CPU reads so they see the device's writes.
    dma_mb();

    // CPU read-ahead may have cached old values while the device was using
    // memory. Invalidate those lines for the bytes the device wrote.
    // Mapping setup checked that the lines stay within the mapping.
    if (mapping->Cached && mapping->Context->CachePolicy == SHMDeviceCacheNonCoherent &&
        __DeviceWrites(mapping)) {
        __MaintainRange(mapping, offset, length, CpuDataCacheInvalidate);
    }
}

oserr_t
SHMDeviceSyncForDevice(
    _In_ struct SHMDeviceMapping*    mapping,
    _In_ size_t                      offset,
    _In_ size_t                      length,
    _In_ const struct SHMAccessWait* wait)
{
    oserr_t status;

    // A device transfer requires a non-empty range.
    if (mapping == NULL || !__IsStreaming(mapping) || length == 0) {
        return OS_EINVALPARAMS;
    }

    if (!__SHMRangeContains(0, mapping->Length, offset, length)) {
        return OS_EINVALPARAMS;
    }

    status = SHMAccessBegin(
        &mapping->Access,
        offset,
        length,
        SHMAccessHolderDevice,
        __DeviceWrites(mapping),
        wait
    );
    if (status != OS_EOK) {
        return status;
    }

    // Copy CPU data to bounce storage before the device reads it.
    // A device that only writes does not need the previous contents.
    if (mapping->Stream.Bounce.Copy != 0 && mapping->Direction != DMAFromDevice) {
        memcpy(
            (void*)(mapping->Stream.Bounce.Copy + offset),
            (const void*)(mapping->Stream.Bounce.Source + mapping->Stream.Bounce.SourceOffset + offset),
            length
        );
    }

    __PrepareForDevice(mapping, offset, length);
    mapping->Stream.Offset = offset;
    mapping->Stream.Length = length;
    return OS_EOK;
}

oserr_t
SHMDeviceSyncForCpu(
    _In_ struct SHMDeviceMapping* mapping,
    _In_ size_t                   length)
{
    size_t offset;

    // A transfer may stop before the device writes any bytes. Allow an empty
    // completed range so the CPU can still access the buffer again.
    if (mapping == NULL || !__IsStreaming(mapping)) {
        return OS_EINVALPARAMS;
    }

    // The mapping must have an active device transfer.
    if (mapping->Access.Phase != SHMAccessActive) {
        return OS_EINVALPARAMS;
    }

    if (length > mapping->Stream.Length) {
        return OS_EINVALPARAMS;
    }

    offset = mapping->Stream.Offset;
    __CompleteForCpu(mapping, offset, length);

    // Copy only completed bytes from bounce storage to the buffer.
    // Leave the CPU's data outside the completed range unchanged.
    if (mapping->Stream.Bounce.Copy != 0 && __DeviceWrites(mapping)) {
        memcpy(
            (void*)(mapping->Stream.Bounce.Source + mapping->Stream.Bounce.SourceOffset + offset),
            (const void*)(mapping->Stream.Bounce.Copy + offset),
            length
        );
    }

    SHMAccessEnd(&mapping->Access);
    return OS_EOK;
}

oserr_t
SHMDeviceBeginUse(
    _In_ struct SHMDeviceMapping*    mapping,
    _In_ const struct SHMAccessWait* wait)
{
    oserr_t status;

    if (mapping == NULL || mapping->Kind != SHMDeviceMappingPersistent) {
        return OS_EINVALPARAMS;
    }

    if (mapping->Persistent.Uses == UINT_MAX) {
        return OS_EOVERFLOW;
    }

    // CPU access is excluded until every queued use has completed. Repeating
    // cache work here could write stale CPU data over a running device's writes.
    if (mapping->Persistent.Uses == 0) {
        status = SHMAccessBegin(
            &mapping->Access,
            0,
            mapping->Length,
            SHMAccessHolderDevice,
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
    return OS_EOK;
}

oserr_t
SHMDeviceEndUse(
    _In_ struct SHMDeviceMapping* mapping)
{
    if (mapping == NULL || mapping->Kind != SHMDeviceMappingPersistent) {
        return OS_EINVALPARAMS;
    }

    if (mapping->Persistent.Uses == 0) {
        return OS_EINVALPARAMS;
    }

    // Each use must have completed or been stopped before it is removed. Only
    // the final completion permits CPU access to any byte of the mapping.
    if (mapping->Persistent.Uses == 1) {
        __CompleteForCpu(mapping, 0, mapping->Length);
        SHMAccessEnd(&mapping->Access);
    }
    mapping->Persistent.Uses--;
    return OS_EOK;
}

const struct DMASegment*
SHMDeviceMappingSegments(
    _In_  const struct SHMDeviceMapping* mapping,
    _Out_ uint32_t*                      countOut)
{
    // The stored list stays valid and unchanged for the mapping's lifetime.
    // Return it directly without copying.
    if (mapping == NULL || countOut == NULL) {
        return NULL;
    }

    *countOut = mapping->SegmentCount;
    return mapping->Segments;
}

oserr_t
SHMDeviceAbandon(
    _InOut_ struct SHMDeviceMapping** mapping)
{
    struct SHMDeviceMapping* abandoned = *mapping;

    if (abandoned == NULL) {
        return OS_EOK;
    }

    if (abandoned->Kind == SHMDeviceMappingShared ||
        abandoned->Access.Phase == SHMAccessActive) {
        SHMAccessQuarantine(&abandoned->Access);
        MutexLock(&abandoned->Context->QuarantineLock);
        abandoned->QuarantineNext = abandoned->Context->Quarantine;
        abandoned->Context->Quarantine = abandoned;
        MutexUnlock(&abandoned->Context->QuarantineLock);
        
        *mapping = NULL;
        return OS_EOK;
    }

    // All threads in the group are gone. Queued calls cannot submit hardware
    // work, even if their interrupted syscall never returned.
    SHMAccessEnd(&abandoned->Access);
    return SHMDeviceUnmap(mapping);
}

void
SHMDeviceReleaseQuarantine(
    _In_ struct SHMDeviceContext* context)
{
    struct SHMDeviceMapping* mapping;
    struct SHMDeviceMapping* next;

    // The caller must hold a context reference and have stopped all device use.
    MutexLock(&context->QuarantineLock);
    mapping = context->Quarantine;
    context->Quarantine = NULL;
    MutexUnlock(&context->QuarantineLock);

    // Order the confirmed stop before releasing any device-visible pages.
    dma_mb();
    for (; mapping != NULL; mapping = next) {
        next = mapping->QuarantineNext;
        if (__IsStreaming(mapping)) {
            // A reset does not report completed bytes. Discard bounce contents.
            // Direct device writes must be visible before another CPU can enter.
            if (mapping->Stream.Bounce.Copy == 0) {
                __CompleteForCpu(mapping, mapping->Stream.Offset, mapping->Stream.Length);
            }
        } else if (mapping->Kind == SHMDeviceMappingPersistent) {
            __CompleteForCpu(mapping, 0, mapping->Length);
            mapping->Persistent.Uses = 0;
        }
        
        SHMAccessEnd(&mapping->Access);
        SHMDeviceUnmap(&mapping);
    }
}
