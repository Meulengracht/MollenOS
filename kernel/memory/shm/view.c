/**
 * MollenOS
 *
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
 */

#include <assert.h>
#include <handles/global.h>
#include <handles/grants.h>
#include <handles/handle.h>
#include <heap.h>
#include <shm/shm.h>
#include <shm/view.h>

#include "private.h"
#include "../space/private.h"

static oserr_t
__ValidateExportPolicy(
    _In_ const GrantTokenOptions_t* options,
    _In_ const SHMReference_t* source)
{
    unsigned int knownFlags = GrantTokenScopeRead | GrantTokenScopeWrite;

    if (options->Scope == 0 || ((unsigned int)options->Scope & ~knownFlags) != 0) {
        return OS_EINVALPARAMS;
    }
    
    if (source->Buffer->Flags & SHM_PRIVATE) {
        return OS_EPERMISSIONS;
    }

    if ((options->Scope & source->Scope) != options->Scope) {
        return OS_EPERMISSIONS;
    }

    if (options->Data.SHM.Length == 0) {
        return OS_EINVALPARAMS;
    }

    if (!__SHMRangeContains(0, source->Length, options->Data.SHM.Offset, options->Data.SHM.Length)) {
        return OS_EINVALPARAMS;
    }
    return OS_EOK;
}

oserr_t
SHMReferenceForExport(
    _In_  uuid_t                     handle,
    _In_  const GrantTokenOptions_t* options,
    _Out_ HandleType_t*              handleTypeOut)
{
    SHMReference_t source;
    oserr_t             status;

    status = SHMResolve(handle, &source);
    if (status != OS_EOK) {
        return status;
    }
    
    status = __ValidateExportPolicy(options, &source);
    if (status != OS_EOK) {
        ReleaseGlobalHandle(source.ReferenceID);
        return status;
    }

    *handleTypeOut = source.Type;
    return OS_EOK;
}

oserr_t
SHMViewValidateMapping(
    _In_ const SHMView_t* view,
    _In_ size_t           offset,
    _In_ size_t           length,
    _In_ unsigned int     flags)
{
    unsigned int scope = GrantTokenScopeRead;
    size_t       pageSize = GetMemorySpacePageSize();

    if (length == 0 || offset > SIZE_MAX - view->Buffer->Offset) {
        return OS_EINVALPARAMS;
    }

    if (((offset + view->Buffer->Offset) % pageSize) != 0 || length % pageSize != 0) {
        return OS_EINVALPARAMS;
    }

    if (!__SHMRangeContains(view->Offset, view->Length, offset, length)) {
        return OS_EPERMISSIONS;
    }

    if (!(flags & MAPPING_READONLY)) {
        scope |= GrantTokenScopeWrite;
    }

    if ((scope & view->Scope) != scope || (flags & MAPPING_EXECUTABLE)) {
        return OS_EPERMISSIONS;
    }

    return OS_EOK;
}

static void
__ResolveBuffer(
    _In_  struct SHMBuffer*    buffer,
    _Out_ SHMReference_t* reference)
{
    *reference = (SHMReference_t) {
        .ReferenceID = buffer->ID,
        .Type = HandleTypeSHM,
        .Buffer = buffer,
        .Length = buffer->Length,
        .Scope = GrantTokenScopeRead | GrantTokenScopeWrite
    };
}

static void
__ResolveView(
    _In_  uuid_t          handle,
    _In_  SHMView_t*      view,
    _Out_ SHMReference_t* reference)
{
    *reference = (SHMReference_t) {
        .ReferenceID = handle,
        .Type = HandleTypeSHMView,
        .Buffer = view->Buffer,
        .View = view,
        .Offset = view->Offset,
        .Length = view->Length,
        .Scope = view->Scope
    };
}

oserr_t
SHMResolve(
    _In_  uuid_t          handle,
    _Out_ SHMReference_t* reference)
{
    void*   resource;
    oserr_t status;

    status = AcquireHandleReference(handle, HandleTypeSHM, &resource);
    if (status == OS_EOK) {
        __ResolveBuffer(resource, reference);
        return OS_EOK;
    }

    if (status != OS_EUNKNOWN) {
        return status;
    }

    status = AcquireHandleReference(handle, HandleTypeSHMView, &resource);
    if (status == OS_EOK) {
        __ResolveView(handle, resource, reference);
    }
    return status;
}

void
SHMResolveVerified(
    _In_  uuid_t          handle,
    _Out_ SHMReference_t* reference)
{
    struct SHMBuffer* buffer;
    SHMView_t*        view;

    buffer = LookupGlobalHandle(handle, HandleTypeSHM);
    if (buffer != NULL) {
        __ResolveBuffer(buffer, reference);
        return;
    }

    view = LookupGlobalHandle(handle, HandleTypeSHMView);
    assert(view != NULL);
    __ResolveView(handle, view, reference);
}

static void
__DeleteView(
    _In_ void* resource)
{
    SHMView_t* view = resource;

    ReleaseGlobalHandle(view->Buffer->ID);
    kfree(view);
}

oserr_t
SHMViewCreate(
    _In_  const SHMReference_t* source,
    _In_  size_t                offset,
    _In_  size_t                length,
    _In_  GrantTokenScope_t     scope,
    _Out_ SHMHandle_t*          handle)
{
    SHMView_t*  view;
    SHMHandle_t result = { 0 };
    oserr_t     status;

    if (length == 0 || !__SHMRangeContains(0, source->Length, offset, length)) {
        return OS_EINVALPARAMS;
    }

    if (scope == 0 || ((unsigned int)scope & ~(GrantTokenScopeRead | GrantTokenScopeWrite))) {
        return OS_EINVALPARAMS;
    }

    if ((scope & source->Scope) != scope) {
        return OS_EPERMISSIONS;
    }

    view = kmalloc(sizeof(*view));
    if (view == NULL) {
        return OS_EOOM;
    }

    status = AcquireGlobalHandle(source->Buffer->ID, HandleTypeSHM, NULL);
    if (status != OS_EOK) {
        kfree(view);
        return status;
    }
    view->Buffer = source->Buffer;
    view->Offset = source->Offset + offset;
    view->Length = length;
    view->Scope = scope;
    
    result.ID = CreateHandle(HandleTypeSHMView, NULL, __DeleteView, view);
    if (result.ID == UUID_INVALID) {
        __DeleteView(view);
        return OS_EOOM;
    }

    result.Capacity = length;
    *handle = result;
    return OS_EOK;
}

oserr_t
SHMAttachToken(
    _In_  const GrantToken_t* token,
    _Out_ SHMHandle_t*        handle)
{
    GrantToken_t   tokenCopy;
    SHMReference_t source;
    uuid_t         sourceID;
    oserr_t        status;

    if (token == NULL || handle == NULL) {
        return OS_EINVALPARAMS;
    }

    tokenCopy = *token;

    status = GrantTokenVerify(&tokenCopy, GrantTokenResourceTypeSHM, tokenCopy.Options.Scope, &sourceID);
    if (status != OS_EOK) {
        return status;
    }

    SHMResolveVerified(sourceID, &source);
    status = SHMViewCreate(
        &source,
        tokenCopy.Options.Data.SHM.Offset,
        tokenCopy.Options.Data.SHM.Length,
        tokenCopy.Options.Scope,
        handle
    );
    ReleaseGlobalHandle(sourceID);
    return status;
}

oserr_t
SHMViewCleanup(
    _In_ uuid_t viewID)
{
    MemorySpace_t*    memorySpace = GetCurrentMemorySpace();
    struct MSContext* context = __MSContextGet(memorySpace);
    oserr_t           status;

    if (context == NULL) {
        return OS_EOK;
    }

    // Keep each record valid until unmap removes it or leaves it for retry.
    MutexLock(&context->UserCopyLock);
    status = MSMappingCleanup(memorySpace, viewID);
    MutexUnlock(&context->UserCopyLock);
    return status;
}

oserr_t
SHMViewMap(
    _In_    uuid_t       viewID,
    _In_    SHMView_t*   view,
    _InOut_ SHMHandle_t* handle,
    _In_    size_t       offset,
    _In_    size_t       length,
    _In_    unsigned int access)
{
    struct SHMBuffer* buffer = view->Buffer;
    MemorySpace_t*    memorySpace = GetCurrentMemorySpace();
    struct MSContext* context = __MSContextGet(memorySpace);
    size_t            pageSize = GetMemorySpacePageSize();
    size_t            previousDisplacement = (vaddr_t)handle->Buffer % pageSize;
    size_t            actual;
    size_t            first;
    size_t            span;
    unsigned int      flags = MAPPING_USERSPACE | MAPPING_PERSISTENT | MAPPING_COMMIT;
    vaddr_t           address;
    oserr_t           status;
    oserr_t           cleanupStatus;

    if (length == 0 || !__SHMRangeContains(0, view->Length, offset, length)) {
        return OS_EINVALPARAMS;
    }

    if ((access & ~(SHM_ACCESS_READ | SHM_ACCESS_WRITE | SHM_ACCESS_EXECUTE | SHM_ACCESS_COMMIT)) != 0 ||
        (access & (SHM_ACCESS_READ | SHM_ACCESS_WRITE | SHM_ACCESS_EXECUTE)) == 0) {
        return OS_EINVALPARAMS;
    }

    if (!(access & SHM_ACCESS_READ) || (access & SHM_ACCESS_EXECUTE)) {
        return OS_EPERMISSIONS;
    }

    if (view->Offset > SIZE_MAX - buffer->Offset ||
        offset > SIZE_MAX - (view->Offset + buffer->Offset)) {
        return OS_EINVALPARAMS;
    }

    actual = buffer->Offset + view->Offset + offset;
    first = actual & ~(pageSize - 1);
    if (first < buffer->Offset || length > SIZE_MAX - (actual - first) - (pageSize - 1)) {
        return OS_EPERMISSIONS;
    }

    span = DIVUP(length + actual - first, pageSize) * pageSize;
    if (!(access & SHM_ACCESS_WRITE)) {
        flags |= MAPPING_READONLY;
    }

    if (SHMBufferUncached(buffer)) {
        flags |= MAPPING_NOCACHE;
    }
    
    status = SHMViewValidateMapping(
        view,
        first - buffer->Offset,
        span,
        flags
    );
    if (status != OS_EOK) {
        return status;
    }

    if (context == NULL) {
        return OS_ENOTSUPPORTED;
    }

    if (handle->Buffer != NULL) {
        if (handle->Length == 0 || handle->Length > SIZE_MAX - previousDisplacement) {
            return OS_EINVALPARAMS;
        }
    }

    if (access & SHM_ACCESS_COMMIT) {
        status = SHMEnsurePhysicalPages(buffer, (int)(first / pageSize), (int)(span / pageSize));
        if (status != OS_EOK) {
            return status;
        }
    }

    // Keep UserCopyLock held while replacing the mapping and updating handle.
    // Rollback uses the same lock.
    MutexLock(&context->UserCopyLock);
    
    status = MSMappingCleanup(memorySpace, viewID);
    if (status != OS_EOK) {
        goto exit;
    }

    status = MSMap(
        memorySpace,
        &(struct MemorySpaceMapOptions) {
            .SHMTag = buffer->ID,
            .SHMView = view,
            .SHMViewID = viewID,
            .SHMOffset = first - buffer->Offset,
            .Pages = &buffer->Pages[first / pageSize],
            .Length = span,
            .Mask = __MASK,
            .Flags = flags,
            .PlacementFlags = MAPPING_PHYSICAL_FIXED | MAPPING_VIRTUAL_PROCESS
        },
        &address
    );
    if (status != OS_EOK) {
        goto exit;
    }

    if (handle->Buffer != NULL) {
        status = MSUnmap(
            memorySpace,
            (vaddr_t)handle->Buffer - previousDisplacement,
            handle->Length + previousDisplacement
        );
        if (status != OS_EOK) {
            cleanupStatus = MSMappingRollback(
                memorySpace,
                address,
                span
            );
            if (cleanupStatus != OS_EOK) {
                status = cleanupStatus;
            }
            goto exit;
        }
    }
    
    handle->Buffer = (void*)(address + actual - first);
    handle->Offset = offset;
    handle->Length = length;
    handle->Capacity = view->Length;

exit:
    MutexUnlock(&context->UserCopyLock);
    return status;
}

oserr_t
SHMViewCommit(
    _In_ SHMView_t* view,
    _In_ vaddr_t    address,
    _In_ size_t     length)
{
    struct MSContext*    context = __MSContextGet(GetCurrentMemorySpace());
    struct MSAllocation* allocation;
    struct SHMBuffer*    buffer = view->Buffer;
    size_t               pageSize = GetMemorySpacePageSize();
    size_t               pageOffset = address % pageSize;
    size_t               offset;
    size_t               index;
    oserr_t              status;
    bool                 allowFreedPages;

    if (context == NULL || length == 0 || length > SIZE_MAX - pageOffset - (pageSize - 1)) {
        return OS_EINVALPARAMS;
    }

    address -= pageOffset;
    length = DIVUP(length + pageOffset, pageSize) * pageSize;
    
    MutexLock(&context->UserCopyLock);
    allowFreedPages = false;
    status = MSAllocationFindSHM(
        context,
        address,
        length,
        allowFreedPages,
        &allocation
    );
    if (status != OS_EOK || allocation == NULL) {
        MutexUnlock(&context->UserCopyLock);
        return status == OS_EOK ? OS_EINVALPARAMS : status;
    }

    if (allocation->SHM.View->Buffer != buffer) {
        MutexUnlock(&context->UserCopyLock);
        return OS_EPERMISSIONS;
    }

    offset = allocation->SHM.Offset + address - allocation->Address;
    status = SHMViewValidateMapping(view, offset, length, allocation->Flags);
    if (status != OS_EOK) {
        MutexUnlock(&context->UserCopyLock);
        return status;
    }

    // Prevent unmap while allocation of physical pages may release VM locks.
    allocation = MSAllocationAcquire(context, address);
    MutexUnlock(&context->UserCopyLock);
    
    index = (buffer->Offset + offset) / pageSize;
    status = SHMEnsurePhysicalPages(
        buffer,
        (int)index,
        (int)(length / pageSize)
    );
    if (status == OS_EOK) {
        status = MemorySpaceCommit(
            GetCurrentMemorySpace(),
            address,
            &buffer->Pages[index],
            length,
            __MASK,
            MAPPING_PHYSICAL_FIXED
        );
    }
    MSAllocationRelease(context, allocation);
    return status;
}

oserr_t
SHMCommitFault(
    _In_ uuid_t  handle,
    _In_ vaddr_t base,
    _In_ vaddr_t address)
{
    struct MSContext*    context = __MSContextGet(GetCurrentMemorySpace());
    struct MSAllocation* allocation;
    SHMView_t*           view = NULL;
    uuid_t               referenceID = UUID_INVALID;
    size_t               pageSize = GetMemorySpacePageSize();
    oserr_t              status;
    bool                 allowFreedPages;

    address &= ~(pageSize - 1);
    if (context != NULL) {
        MutexLock(&context->UserCopyLock);
        allowFreedPages = false;
        status = MSAllocationFindSHM(
            context,
            address,
            pageSize,
            allowFreedPages,
            &allocation
        );
        if (status == OS_EOK && allocation != NULL) {
            referenceID = allocation->SHM.ReferenceID;
            status = AcquireGlobalHandle(referenceID, HandleTypeSHMView, (void**)&view);
        }
        MutexUnlock(&context->UserCopyLock);
        if (status != OS_EOK) {
            return status;
        }
    }

    if (view != NULL) {
        status = SHMViewCommit(view, address, pageSize);
        ReleaseGlobalHandle(referenceID);
        return status;
    }

    return SHMCommit(
        handle,
        (void*)base,
        (void*)address,
        pageSize
    );
}
