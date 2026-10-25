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

#include <ddk/utils.h>
#include <internal/_syscalls.h>
#include <os/types/syscall.h>
#include <stdlib.h>

#include "dma_private.h"

struct DMABuffer {
    struct __DmaResource Resource;
    SHMHandle_t          Storage;
};

struct DMAMapping {
    struct __DmaResource Resource;
    size_t               Length;
    size_t               ActiveOffset;
    // Zero means managed by CPU. 
    // Begin requires nonzero length, including for receives.
    size_t               ActiveLength;
};

static oserr_t
__DmaRelease(
    _In_ struct __DmaResource* resource)
{
    oserr_t status;

    // Unmap first so storage cannot disappear beneath a live device mapping.
    // A successful stage is recorded before the next fallible operation; retries
    // neither double-unmap nor discard the SHM attachment when detach fails.
    if (resource->Handle != UUID_INVALID) {
        status = Syscall_DmaUnmap(resource->Handle);
        if (status != OS_EOK) {
            WARNING("DMA: mapping %u release failed (%u); retaining owner", resource->Handle, status);
            return status;
        }
        resource->Handle = UUID_INVALID;
    }

    if (resource->Storage != NULL) {
        status = Syscall_SHMDetach(resource->Storage);
        if (status != OS_EOK) {
            WARNING("DMA: buffer %u detach failed (%u); retaining owner", resource->Storage->ID, status);
            return status;
        }
    }
    
    atomic_fetch_sub(&resource->Context->Children, 1);
    free(resource->Segments);
    free(resource);
    return OS_EOK;
}

oserr_t
__DmaCollect(
    _In_ struct DMAContext* context)
{
    struct __DmaResource* resource;
    struct __DmaResource* next;
    oserr_t               status;

    // This is only the cold rollback path. Normal transfers never walk a list.
    // Save the next link before release, which frees the complete child object.
    while (context->Pending != NULL) {
        resource = context->Pending;
        next = resource->Next;

        status = __DmaRelease(resource);
        if (status != OS_EOK) {
            return status;
        }
        context->Pending = next;
    }
    return OS_EOK;
}

static oserr_t
__DmaPrepareResource(
    _In_ struct __DmaResource*            resource,
    _In_ struct DMAContext*               context,
    _In_ const struct OSDmaMappingResult* result)
{
    uint32_t count = 0;
    size_t   bytes;
    oserr_t  status;

    resource->Context = context;
    resource->Handle = result->Handle;
    resource->SegmentCount = result->SegmentCount;
    resource->InlineSegment = result->InlineSegment;
    atomic_fetch_add(&context->Children, 1);

    // One segment already arrived with creation. This path uses no additional
    // allocations or syscalls and covers contiguous controller buffers/rings.
    if (resource->SegmentCount == 1) {
        return OS_EOK;
    }

    bytes = (size_t)resource->SegmentCount * sizeof(*resource->Segments);
    if (resource->SegmentCount == 0 || bytes / sizeof(*resource->Segments) != resource->SegmentCount) {
        status = OS_EOVERFLOW;
        goto rollback;
    }
    
    resource->Segments = malloc(bytes);
    if (resource->Segments == NULL) {
        status = OS_EOOM;
        goto rollback;
    }

    // Fetch the immutable fragmented list exactly once. Publishing only after
    // this succeeds makes accessors infallible and removes setup failures from
    // descriptor construction and periodic transfer rearm.
    status = Syscall_DmaGetSegments(
        resource->Handle,
        resource->Segments,
        resource->SegmentCount,
        &count
    );

    if (status == OS_EOK && count != resource->SegmentCount) {
        status = OS_EUNKNOWN;
    }

    if (status == OS_EOK) {
        return OS_EOK;
    }

rollback:
    // No driver has seen this mapping and hardware cannot own it. Even if its
    // cleanup fails, keep the resource under the context for close to retry.
    WARNING("DMA: segment setup failed (%u); rolling back mapping %u", status, resource->Handle);
    if (__DmaRelease(resource) != OS_EOK) {
        __DmaDeferCleanup(resource);
    }
    return status;
}

static const struct DMASegment*
__DmaSegments(
    _In_  const struct __DmaResource* resource,
    _Out_ uint32_t*                   countOut)
{
    // Both representations are ready before publication and never change.
    // Returning a borrowed pointer avoids copying or allocating per descriptor.
    *countOut = resource->SegmentCount;
    return resource->SegmentCount == 1 ? &resource->InlineSegment : resource->Segments;
}

oserr_t
DMAAllocate(
    _In_    struct DMAContext*            context,
    _In_    const struct DMARequirements* requirements,
    _InOut_ struct DMABuffer**            bufferOut)
{
    struct OSDmaAllocationResult result = { 0 };
    struct DMABuffer*            buffer;
    oserr_t                      status;

    if (context == NULL || requirements == NULL || bufferOut == NULL) {
        return OS_EINVALPARAMS;
    }

    if (*bufferOut != NULL || context->Closing) {
        return OS_EBUSY;
    }

    // Reserve the only local owner before entering the kernel. Embed the raw
    // SHM attachment: wrapping it in OSHandle would add another heap allocation
    // and registry entry for memory which is never independently shared.
    buffer = calloc(1, sizeof(struct DMABuffer));
    if (buffer == NULL) {
        return OS_EOOM;
    }

    status = Syscall_DmaAllocate(context->Handle, requirements, &result);
    if (status != OS_EOK) {
        WARNING("DMAAllocate: device %u, %zu bytes failed (%u)", context->Device, requirements->Length, status);
        free(buffer);
        return status;
    }
    
    buffer->Storage = result.Buffer;
    buffer->Resource.Storage = &buffer->Storage;
    
    status = __DmaPrepareResource(&buffer->Resource, context, &result.Mapping);
    if (status != OS_EOK) {
        return status;
    }
    
    TRACE("DMAAllocate: device %u, %zu bytes, %u segments",
        context->Device, buffer->Storage.Length, buffer->Resource.SegmentCount);
    *bufferOut = buffer;
    return OS_EOK;
}

void*
DMABufferData(
    _In_ const struct DMABuffer* buffer)
{
    return buffer->Storage.Buffer;
}

size_t
DMABufferSize(
    _In_ const struct DMABuffer* buffer)
{
    return buffer->Storage.Length;
}

const struct DMASegment*
DMABufferSegments(
    _In_  const struct DMABuffer* buffer,
    _Out_ uint32_t*               countOut)
{
    return __DmaSegments(&buffer->Resource, countOut);
}

oserr_t
DMAFree(
    _InOut_ struct DMABuffer** buffer)
{
    oserr_t status;

    if (buffer == NULL || *buffer == NULL) {
        return OS_EOK;
    }

    // Hardware quiescence is the driver's responsibility. Unlike streaming
    // mappings this memory is continuously shared and has no handoff state.
    status = __DmaRelease(&(*buffer)->Resource);
    if (status == OS_EOK) {
        *buffer = NULL;
    }
    return status;
}

oserr_t
DMAMapSHM(
    _In_    struct DMAContext*  context,
    _In_    const OSHandle_t*   buffer,
    _In_    size_t              offset,
    _In_    size_t              length,
    _In_    enum DMADirection   direction,
    _InOut_ struct DMAMapping** mappingOut)
{
    struct OSDmaMapParameters parameters = {
        .Offset = offset,
        .Length = length,
        .Direction = direction
    };
    struct OSDmaMappingResult result = { 0 };
    struct DMAMapping*        mapping;
    oserr_t                   status;

    if (context == NULL || buffer == NULL || mappingOut == NULL || length == 0) {
        return OS_EINVALPARAMS;
    }

    if (buffer->Type != OSHANDLE_SHM || buffer->ID == UUID_INVALID) {
        return OS_EINVALPARAMS;
    }

    if (direction < DMAToDevice || direction > DMABidirectional) {
        return OS_EINVALPARAMS;
    }

    if (*mappingOut != NULL || context->Closing) {
        return OS_EBUSY;
    }

    // The kernel retains the original storage. No extra userspace attachment,
    // CPU mapping or RPC is needed to map an existing SHM transfer range.
    mapping = calloc(1, sizeof(struct DMAMapping));
    if (mapping == NULL) {
        return OS_EOOM;
    }

    status = Syscall_DmaMapShm(context->Handle, buffer->ID, &parameters, &result);
    if (status != OS_EOK) {
        WARNING("DMAMapSHM: device %u, buffer %u, range %zu+%zu failed (%u)",
            context->Device, buffer->ID, offset, length, status);
        free(mapping);
        return status;
    }
    
    mapping->Length = length;

    status = __DmaPrepareResource(&mapping->Resource, context, &result);
    if (status != OS_EOK) {
        return status;
    }

    *mappingOut = mapping;
    return OS_EOK;
}

const struct DMASegment*
DMAMappingSegments(
    _In_  const struct DMAMapping* mapping,
    _Out_ uint32_t*                countOut)
{
    return __DmaSegments(&mapping->Resource, countOut);
}

oserr_t
DMABeginTransfer(
    _In_ struct DMAMapping* mapping,
    _In_ size_t             offset,
    _In_ size_t             length)
{
    oserr_t status;

    if (mapping == NULL || length == 0) {
        return OS_EINVALPARAMS;
    }

    if (offset > mapping->Length || length > mapping->Length - offset) {
        return OS_EINVALPARAMS;
    }

    if (mapping->ActiveLength != 0) {
        return OS_EBUSY;
    }

    // Keep the coherent path on this same interface: the kernel still enforces
    // ownership and ordering even when no cache work or bounce copy is needed.
    // Commit the range only on success so failed publication can be retried.
    status = Syscall_DmaSyncForDevice(mapping->Resource.Handle, offset, length);
    if (status == OS_EOK) {
        mapping->ActiveOffset = offset;
        mapping->ActiveLength = length;
    } else {
        WARNING("DMABeginTransfer: mapping %u failed (%u)", mapping->Resource.Handle, status);
    }
    return status;
}

oserr_t
DMAEndTransfer(
    _In_ struct DMAMapping* mapping,
    _In_ size_t             completedLength)
{
    oserr_t status;

    if (mapping == NULL || mapping->ActiveLength == 0) {
        return OS_EINVALPARAMS;
    }

    if (completedLength > mapping->ActiveLength) {
        return OS_EINVALPARAMS;
    }

    // Completion always refers to the prefix of the exact range handed over.
    // Zero reclaims cancellation without copying stale bounce bytes; a short
    // receive leaves the uncompleted tail untouched. Failed end keeps the range.
    status = Syscall_DmaSyncForCpu(mapping->Resource.Handle, mapping->ActiveOffset, completedLength);
    if (status == OS_EOK) {
        mapping->ActiveLength = 0;
    } else {
        WARNING("DMAEndTransfer: mapping %u failed (%u)", mapping->Resource.Handle, status);
    }
    return status;
}

oserr_t
DMAUnmap(
    _InOut_ struct DMAMapping** mapping)
{
    oserr_t status;

    if (mapping == NULL || *mapping == NULL) {
        return OS_EOK;
    }

    // Refuse locally before making a syscall, while the kernel also checks
    // ownership independently. Unmap never hides a missing completion handoff.
    if ((*mapping)->ActiveLength != 0) {
        return OS_EBUSY;
    }
    
    status = __DmaRelease(&(*mapping)->Resource);
    if (status == OS_EOK) {
        *mapping = NULL;
    }
    return status;
}
