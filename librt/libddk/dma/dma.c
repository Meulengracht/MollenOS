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
#include <os/shm.h>
#include <internal/_shm.h>
#include <os/types/syscall.h>
#include <stdlib.h>

#include "private.h"

struct DMABuffer {
    struct __DmaResource Resource;
    SHMHandle_t          Storage;
};

struct DMAMapping {
    struct __DmaResource Resource;
    unsigned int         Flags;
    size_t               Length;
};

_Static_assert(offsetof(struct DMAMapping, Resource) == 0, "Resource release frees the mapping");
_Static_assert(offsetof(struct DMABuffer, Resource) == 0, "Resource release frees the buffer");

static oserr_t
__DmaRelease(
    _In_ struct __DmaResource* resource)
{
    oserr_t status;

    // Unmap first so storage cannot disappear beneath a live device mapping.
    // A successful stage is recorded before the next fallible operation; retries
    // neither double-unmap nor discard the SHM attachment when detach fails.
    if (resource->Handle != UUID_INVALID) {
        status = Syscall_ResourceRelease(resource->Handle);
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

    // Fetch the segment list once, here. The resource is only given to the
    // driver after this succeeds, so the segment accessors cannot fail. Building
    // descriptors and resubmitting periodic transfers never hit a setup error.
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
    // No driver has seen this mapping yet, so hardware cannot be using it. If
    // cleanup fails, keep the resource on the context so DMAClose can retry it.
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
    // Both the inline segment and the segment array are set up before the
    // resource is handed out, and never change after that. Returning a pointer
    // into the resource avoids a copy or allocation per descriptor.
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

    // Allocate the local buffer object before entering the kernel. The SHM
    // attachment is embedded directly because this memory is never shared on
    // its own. Wrapping it in an OSHandle would cost another heap allocation
    // and registry entry.
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

static oserr_t
__DmaMapSHM(
    _In_    struct DMAContext*               context,
    _In_    const OSHandle_t*                buffer,
    _In_    size_t                           offset,
    _In_    size_t                           length,
    _In_    enum DMADirection                direction,
    _In_    unsigned int                     flags,
    _In_    const struct DMAMapRequirements* requirements,
    _InOut_ struct DMAMapping**              mappingOut)
{
    struct OSDmaMapParameters parameters = {
        .Offset = offset,
        .Length = length,
        .Direction = direction,
        .Flags = flags
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

    if (requirements != NULL) {
        parameters.Requirements = *requirements;
    }

    // The kernel keeps its own reference to the original storage, so mapping an
    // existing SHM range needs no extra userspace attachment, CPU mapping or RPC.
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
    
    status = __DmaPrepareResource(&mapping->Resource, context, &result);
    if (status != OS_EOK) {
        return status;
    }

    mapping->Flags = flags;
    mapping->Length = length;
    *mappingOut = mapping;
    return OS_EOK;
}

oserr_t
DMAMapSHM(
    _In_    struct DMAContext*               context,
    _In_    const OSHandle_t*                buffer,
    _In_    size_t                           offset,
    _In_    size_t                           length,
    _In_    enum DMADirection                direction,
    _In_    const struct DMAMapRequirements* requirements,
    _InOut_ struct DMAMapping**              mappingOut)
{
    return __DmaMapSHM(
        context,
        buffer,
        offset,
        length,
        direction,
        0,
        requirements,
        mappingOut
    );
}

oserr_t
DMAMapSHMDirect(
    _In_    struct DMAContext*               context,
    _In_    const OSHandle_t*                buffer,
    _In_    size_t                           offset,
    _In_    size_t                           length,
    _In_    enum DMADirection                direction,
    _In_    const struct DMAMapRequirements* requirements,
    _InOut_ struct DMAMapping**              mappingOut)
{
    return __DmaMapSHM(
        context,
        buffer,
        offset,
        length,
        direction,
        DMA_MAP_REQUIRE_DIRECT,
        requirements,
        mappingOut
    );
}

const struct DMASegment*
DMAMappingSegments(
    _In_  const struct DMAMapping* mapping,
    _Out_ uint32_t*                countOut)
{
    return __DmaSegments(&mapping->Resource, countOut);
}

static oserr_t
__DmaWait(
    _In_ struct DMAMapping*   mapping,
    _In_ enum SHMAccessKind   kind,
    _In_ size_t               offset,
    _In_ size_t               length,
    _In_ enum SHMWaitMode     mode,
    _In_ const OSTimestamp_t* deadline)
{
    struct SHMAccessBeginParameters parameters = {
        .Kind = kind,
        .Offset = offset,
        .Length = length
    };

    return __SHMAccessWait(mapping->Resource.Handle, &parameters, mode, deadline);
}

static bool
__RangeValid(
    _In_ const struct DMAMapping* mapping,
    _In_ size_t                   offset,
    _In_ size_t                   length)
{
    return length != 0 && offset <= mapping->Length && length <= mapping->Length - offset;
}

oserr_t
DMABeginTransfer(
    _In_ struct DMAMapping*   mapping,
    _In_ size_t               offset,
    _In_ size_t               length,
    _In_ enum SHMWaitMode     mode,
    _In_ const OSTimestamp_t* deadline)
{
    oserr_t status;

    if (mapping == NULL || (mapping->Flags & DMA_MAP_PERSISTENT) != 0) {
        return OS_EINVALPARAMS;
    }

    if (!__RangeValid(mapping, offset, length)) {
        return OS_EINVALPARAMS;
    }

    // Coherent devices go through the same call. The kernel still checks access
    // conflicts and ordering, even when no cache work or bounce copy is needed.
    status = __DmaWait(mapping, DMAAccessTransfer, offset, length, mode, deadline);

    if (status != OS_EOK) {
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

    if (mapping == NULL || (mapping->Flags & DMA_MAP_PERSISTENT) != 0) {
        return OS_EINVALPARAMS;
    }

    // The completed length is always counted from the start of the range given
    // to begin. Zero ends a cancelled transfer without copying stale bounce
    // bytes, and a short receive leaves the rest of the range untouched. If end
    // fails, the transfer stays active with the same range.
    status = Syscall_ShmAccessEnd(
        mapping->Resource.Handle,
        DMAAccessTransfer,
        completedLength
    );
    if (status != OS_EOK) {
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

    status = __DmaRelease(&(*mapping)->Resource);
    if (status == OS_EOK) {
        *mapping = NULL;
    }
    return status;
}

oserr_t
DMAMapSHMPersistent(
    _In_    struct DMAContext*               context,
    _In_    const OSHandle_t*                buffer,
    _In_    size_t                           offset,
    _In_    size_t                           length,
    _In_    enum DMADirection                direction,
    _In_    const struct DMAMapRequirements* requirements,
    _InOut_ struct DMAMapping**              mappingOut)
{
    return __DmaMapSHM(
        context,
        buffer,
        offset,
        length,
        direction,
        DMA_MAP_REQUIRE_DIRECT | DMA_MAP_PERSISTENT,
        requirements,
        mappingOut
    );
}

oserr_t
DMABeginUse(
    _In_    struct DMAMapping*   mapping,
    _InOut_ struct DMAUse*       use,
    _In_    enum SHMWaitMode     mode,
    _In_    const OSTimestamp_t* deadline)
{
    oserr_t status;

    if (mapping == NULL || use == NULL || (mapping->Flags & DMA_MAP_PERSISTENT) == 0) {
        return OS_EINVALPARAMS;
    }
    if (use->Mapping != NULL) {
        return OS_EBUSY;
    }

    status = __DmaWait(mapping, DMAAccessUse, 0, 0, mode, deadline);

    if (status == OS_EOK) {
        use->Mapping = mapping;
    }
    return status;
}

oserr_t
DMAEndUse(
    _InOut_ struct DMAUse* use)
{
    struct DMAMapping* mapping;
    oserr_t            status;

    if (use == NULL || use->Mapping == NULL) {
        return OS_EINVALPARAMS;
    }
    
    mapping = use->Mapping;

    // Leave the token active on failure. The caller can then retry, and unmap
    // keeps refusing while hardware may still be using the storage.
    status = Syscall_ShmAccessEnd(mapping->Resource.Handle, DMAAccessUse, 0);
    if (status == OS_EOK) {
        use->Mapping = NULL;
    }
    return status;
}
