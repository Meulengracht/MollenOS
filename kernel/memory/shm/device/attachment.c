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

#include <heap.h>
#include <assert.h>
#include <debug.h>
#include <shm/record.h>
#include <string.h>

#include "private.h"

oserr_t
SHMDeviceContextPrepareBound(
    _In_  uuid_t                            binding,
    _In_  const OSDmaBoundOpenParameters_t* parameters,
    _Out_ SHMDeviceContext_t**              contextOut)
{
    SHMDeviceContext_t*         context;
    DMABindingContextConfig_t   configuration;
    oserr_t                     status;

    context = kmalloc(sizeof(SHMDeviceContext_t));
    if (context == NULL) {
        ERROR("SHMDeviceContextPrepareBound: failed to allocate a context for binding %u", binding);
        return OS_EOOM;
    }

    SHMDeviceContextInitialize(context);

    // As soon as the binding points to this context, a cancel pass (the code
    // that cuts off a binding's contexts) may visit it. That is why the
    // reference count and mapping list are set up first. The binding call
    // also sets up the address space the context will use. No mappings can be
    // added until that is done and the context is published.
    status = DMABindingAttachContext(
        binding,
        parameters,
        context,
        &context->Binding,
        &configuration
    );
    if (status != OS_EOK) {
        ERROR("SHMDeviceContextPrepareBound: DMABindingAttachContext binding %u failed: %u", binding, status);
        SHMDeviceContextRelease(&context);
        return status;
    }

    context->CachePolicy = configuration.CachePolicy;
    context->AddressLimit = configuration.AddressLimit;
    context->AddressSpace = configuration.AddressSpace;

    TRACE("SHMDeviceContextPrepareBound: binding %u prepared context %p", binding, context);
    *contextOut = context;
    return OS_EOK;
}

oserr_t
SHMDeviceContextPublishBound(
    _In_ SHMDeviceContext_t* context)
{
    oserr_t status;

    status = DMABindingPublishContext(context->Binding);
    if (status != OS_EOK) {
        WARNING("SHMDeviceContextPublishBound: DMABindingPublishContext context %p failed: %u",
                context, status);
    }
    return status;
}

void
SHMDeviceContextCancelBound(
    _InOut_ SHMDeviceContext_t** context)
{
    SHMDeviceContextRelease(context);
}

void
SHMDeviceContextAbandon(
    _InOut_ SHMDeviceContext_t** context)
{
    SHMDeviceContext_t* current = *context;
    bool                hasMappings;

    if (current == NULL) {
        return;
    }

    MutexLock(&current->MappingLock);
    hasMappings = current->Mappings != NULL;
    MutexUnlock(&current->MappingLock);
    if (hasMappings) {
        // The context handle and a mapping handle can close in either order,
        // and both paths must block the same recovery group from starting new
        // DMA work. This happens after MappingLock is released, so that lock is
        // never held while manager locks are taken.
        DMABindingRevoke(current->Binding);
    }
    SHMDeviceContextRelease(context);
}

// Find the next mapping to visit on the context's list. Pass NULL as current
// to start from the head. Mappings whose device resources are already freed are
// skipped. When no mapping is left, *mappingOut is NULL and the result is
// OS_EOK. If a mapping is being cleaned right now, the result is OS_EBUSY and
// the caller must try again later.
//
// The caller must hold a context reference. The mapping returned carries an
// added reference. That keeps its list entry readable under MappingLock even
// after its device resources are freed.
static oserr_t
__NextMapping(
    _In_     SHMDeviceContext_t*  context,
    _In_Opt_ SHMDeviceMapping_t*  current,
    _Out_    SHMDeviceMapping_t** mappingOut)
{
    SHMDeviceMapping_t* mapping;
    oserr_t             status = OS_EOK;

    *mappingOut = NULL;
    
    MutexLock(&context->MappingLock);
    mapping = current == NULL ? context->Mappings : current->Lifetime.Next;
    for (; mapping != NULL; mapping = mapping->Lifetime.Next) {
        if (mapping->Lifetime.Cleaning) {
            status = OS_EBUSY;
            break;
        }

        if (mapping->State == SHMDeviceMappingStopped) {
            continue;
        }

        status = SHMDeviceMappingAcquire(mapping);
        if (status == OS_ECANCELLED) {
            // The last reference was already dropped, and that thread is
            // waiting for MappingLock to unlink this entry. It cannot free the
            // struct until we release the lock, so skip the entry.
            status = OS_EOK;
            continue;
        } else if (status == OS_EOK) {
            *mappingOut = mapping;
        }
        break;
    }
    MutexUnlock(&context->MappingLock);
    return status;
}

oserr_t
SHMDeviceContextRevoke(
    _In_ SHMDeviceContext_t* context)
{
    SHMDeviceMapping_t* mapping;
    SHMDeviceMapping_t* next;
    oserr_t             status;

    status = __NextMapping(context, NULL, &mapping);
    if (status != OS_EOK) {
        ERROR("SHMDeviceContextRevoke: initial __NextMapping context %p failed: %u", context, status);
        return status;
    }

    while (mapping != NULL) {
        // The recovery group has already blocked new work. Lock the whole buffer
        // range, so neither the CPU nor another device can use it until the
        // hardware is confirmed stopped. This only changes the access entry,
        // under Buffer->Mutex. A caller that is still waiting for a range keeps
        // its operation token until the wait returns or process exit cleanup
        // ends it.
        SHMAccessQuarantine(&mapping->Access);
        
        status = __NextMapping(context, mapping, &next);
        if (status != OS_EOK) {
            ERROR("SHMDeviceContextRevoke: __NextMapping context %p after mapping %p failed: %u",
                  context, mapping, status);
            SHMDeviceMappingRelease(&mapping);
            return status;
        }
        SHMDeviceMappingRelease(&mapping);
        mapping = next;
    }
    return status;
}

// Free the device resources of one mapping after the hardware stop. The caller
// holds a walk reference and runs inside the exclusive stop cleanup pass.
//
// Exactly two references are expected. One is the walk reference. The other is
// held by the mapping's handle, or by the context list if the mapping was
// abandoned. Any other count means someone is still using the mapping, so the
// result is OS_EBUSY. A mapping whose handle is still open keeps its struct
// after this, even though its device resources are freed.
static oserr_t
__CompleteMapping(
    _In_ SHMDeviceMapping_t* mapping)
{
    SHMDeviceContext_t* context = mapping->Context;
    SHMDeviceMapping_t* abandoned = NULL;
    oserr_t             status;

    MutexLock(&context->MappingLock);
    if (mapping->Lifetime.Cleaning ||
        atomic_load(&mapping->Lifetime.References) != 2) {
        MutexUnlock(&context->MappingLock);
        return OS_EBUSY;
    }
    mapping->Lifetime.Cleaning = true;
    MutexUnlock(&context->MappingLock);

    status = SHMDeviceMappingCleanup(mapping, true);
    if (status != OS_EOK) {
        ERROR("__CompleteMapping: SHMDeviceMappingCleanup context %p, mapping %p failed: %u",
              context, mapping, status);
    }

    MutexLock(&context->MappingLock);
    mapping->Lifetime.Cleaning = false;
    if (status == OS_EOK && mapping->Lifetime.Abandoned) {
        mapping->Lifetime.Abandoned = false;
        abandoned = mapping;
    }
    MutexUnlock(&context->MappingLock);

    // Only an abandoned mapping hands its original handle reference to this
    // pass, so only then is that reference dropped. A mapping with an open
    // handle keeps its reference and the handle's pointer stays untouched.
    SHMDeviceMappingRelease(&abandoned);
    return status;
}

oserr_t
SHMDeviceContextCompleteStop(
    _In_ SHMDeviceContext_t* context)
{
    SHMDeviceMapping_t* mapping;
    SHMDeviceMapping_t* next;
    oserr_t             status;
    oserr_t            firstError = OS_EOK;

    // The recovery group has confirmed that the hardware stopped and has kept
    // all other operations out. Visit every mapping, including those whose
    // handle is still open, because their device resources can now be freed.
    status = __NextMapping(
        context,
        NULL,
        &mapping
    );
    if (status != OS_EOK) {
        ERROR("SHMDeviceContextCompleteStop: initial __NextMapping context %p failed: %u", context, status);
        return status;
    }

    while (mapping != NULL) {
        status = __CompleteMapping(mapping);
        if (status != OS_EOK) {
            ERROR("SHMDeviceContextCompleteStop: __CompleteMapping context %p, mapping %p failed: %u",
                  context, mapping, status);
            if (firstError == OS_EOK) {
                firstError = status;
            }
        }

        // Find the next entry before dropping the reference on the current one,
        // so the list position is not lost. If freeing the device addresses
        // failed, the entry stays on the list for a retry that does not repeat
        // the cache syncs.
        status = __NextMapping(
            context,
            mapping,
            &next
        );
        if (status != OS_EOK) {
            ERROR("SHMDeviceContextCompleteStop: __NextMapping context %p after mapping %p failed: %u",
                  context, mapping, status);
            SHMDeviceMappingRelease(&mapping);
            return firstError != OS_EOK ? firstError : status;
        }
        SHMDeviceMappingRelease(&mapping);
        mapping = next;
    }
    return firstError != OS_EOK ? firstError : status;
}

// Start an operation on a context or mapping. The caller must have put the
// record in the Updating state. PendingOperation is saved before any reference
// is taken or the group counter goes up, so process exit cleanup never mistakes
// interrupted work for an idle handle. If a step fails, the steps already done
// are undone.
static oserr_t
__BeginOperation(
    _In_  SHMRecord_t*                record,
    _In_  SHMDeviceContext_t*         context,
    _In_  SHMDeviceMapping_t*         mapping,
    _In_  enum SHMDeviceOperationKind kind,
    _Out_ SHMDeviceOperation_t**      operationOut)
{
    SHMDeviceOperation_t* operation = mapping == NULL ? &context->Operation : &mapping->Operation;
    oserr_t               status;

    // A context has no binding only while a failed preparation is being
    // undone. Such a context must never take a mapping or a device operation.
    if (context->Binding == NULL) {
        WARNING("__BeginOperation: context %p has no binding, kind %u", context, (unsigned int)kind);
        return OS_EINVALPARAMS;
    }

    assert(atomic_load(&record->State) == SHMRecordUpdating);
    assert(record->PendingOperation == NULL && operation->Record == NULL);
    
    *operation = (SHMDeviceOperation_t) {
        .Record = record, .Context = context, .Mapping = mapping,
        .Kind = kind, .Phase = SHMDeviceOperationRunning
    };
    record->PendingOperation = operation;
    
    status = SHMDeviceContextAcquire(context);
    if (status != OS_EOK) {
        ERROR("__BeginOperation: SHMDeviceContextAcquire context %p failed: %u", context, status);
        goto error;
    }

    operation->Setup = SHMDeviceOperationSetupContext;
    if (mapping != NULL) {
        status = SHMDeviceMappingAcquire(mapping);
        if (status != OS_EOK) {
            ERROR("__BeginOperation: SHMDeviceMappingAcquire mapping %p failed: %u", mapping, status);
            goto error;
        }
        operation->Setup = SHMDeviceOperationSetupMapping;
    }

    status = DMABindingBeginOperation(
        context->Binding,
        kind == SHMDeviceOperationRelease,
        &operation->Stopped
    );
    if (status != OS_EOK) {
        // The group refuses new work while it is being stopped, which is expected.
        TRACE("__BeginOperation: DMABindingBeginOperation context %p, kind %u refused: %u",
              context, (unsigned int)kind, status);
        goto error;
    }
    operation->Setup = SHMDeviceOperationSetupCounted;

    *operationOut = operation;
    return OS_EOK;

error:
    SHMDeviceOperationEnd(&operation);
    return status;
}

oserr_t
SHMDeviceContextBeginOperation(
    _In_  SHMRecord_t*                record,
    _In_  SHMDeviceContext_t*         context,
    _In_  enum SHMDeviceOperationKind kind,
    _Out_ SHMDeviceOperation_t**      operationOut)
{
    return __BeginOperation(
        record,
        context,
        NULL,
        kind,
        operationOut
    );
}

oserr_t
SHMDeviceMappingBeginOperation(
    _In_  SHMRecord_t*                record,
    _In_  SHMDeviceMapping_t*         mapping,
    _In_  enum SHMDeviceOperationKind kind,
    _Out_ SHMDeviceOperation_t**      operationOut)
{
    return __BeginOperation(
        record,
        mapping->Context,
        mapping,
        kind,
        operationOut
    );
}

bool
SHMDeviceOperationIsValid(
    _In_Opt_ const SHMDeviceOperation_t* operation,
    _In_     const SHMDeviceContext_t*   context,
    _In_Opt_ const SHMDeviceMapping_t*   mapping,
    _In_     enum SHMDeviceOperationKind kind)
{
    if (operation == NULL) {
        return false;
    }

    if (operation->Record == NULL || operation->Record->PendingOperation != operation ||
        operation->Context != context ||
        operation->Setup != SHMDeviceOperationSetupCounted) {
        return false;
    }
    return operation->Mapping == mapping && operation->Kind == kind;
}

oserr_t
SHMDeviceOperationBeginAccess(
    _In_     SHMDeviceOperation_t*       operation,
    _In_     SHMAccess_t*                access,
    _In_     size_t                      offset,
    _In_     size_t                      length,
    _In_     bool                        write,
    _In_Opt_ const struct SHMAccessWait* wait)
{
    oserr_t status;

    operation->Phase = SHMDeviceOperationRangeAdmission;

    status = SHMAccessBegin(
        access,
        offset,
        length,
        SHMAccessHolderDevice,
        write,
        wait
    );
    if (status == OS_EFORKED) {
        // A worker thread will continue with this same token later, so leave
        // it as it is.
        return status;
    }

    operation->Phase = SHMDeviceOperationRunning;

    if (status != OS_EOK) {
        return status;
    }
    return SHMDeviceOperationCheck(operation);
}

oserr_t
SHMDeviceOperationCheck(
    _In_ const SHMDeviceOperation_t* operation)
{
    oserr_t status;

    assert(operation != NULL && operation->Record != NULL);
    if (operation->Kind != SHMDeviceOperationSubmit) {
        return OS_EOK;
    }

    status = DMABindingCheckOperation(operation->Context->Binding);
    if (status != OS_EOK) {
        WARNING("SHMDeviceOperationCheck: context %p binding no longer accepts work: %u",
                operation->Context, status);
    }
    return status;
}

void
SHMDeviceOperationEnd(
    _InOut_ SHMDeviceOperation_t** operation)
{
    SHMDeviceOperation_t* current = *operation;
    SHMDeviceContext_t*   context;
    SHMDeviceMapping_t*   mapping;
    bool                  counted;

    if (current == NULL) {
        return;
    }

    *operation = NULL;

    context = current->Setup >= SHMDeviceOperationSetupContext ? current->Context : NULL;
    mapping = current->Setup >= SHMDeviceOperationSetupMapping ? current->Mapping : NULL;
    counted = current->Setup == SHMDeviceOperationSetupCounted;
    current->Record->PendingOperation = NULL;
    memset(current, 0, sizeof(SHMDeviceOperation_t));

    // The token can live inside the mapping that is about to be freed. So copy
    // what is needed and clear the record's PendingOperation first, then drop
    // the mapping reference. The context reference goes last. It keeps the
    // context alive until the group count is lowered and any cancel retry is
    // done.
    SHMDeviceMappingRelease(&mapping);
    if (counted) {
        DMABindingEndOperation(context->Binding);
    }
    SHMDeviceContextRelease(&context);
}

oserr_t
SHMDeviceOperationCancel(
    _In_ SHMRecord_t* record)
{
    SHMDeviceOperation_t* operation = record->PendingOperation;

    if (operation == NULL) {
        return OS_EOK;
    }

    DMABindingRevoke(operation->Context->Binding);

    if (operation->Phase != SHMDeviceOperationRangeAdmission) {
        // Process exit may have interrupted this call halfway through
        // allocating, copying, or syncing caches. There is no way to tell what
        // state it left behind, so return OS_EBUSY. The group's operation count
        // and the memory stay in place until reboot.
        WARNING("SHMDeviceOperationCancel: resource %u interrupted in phase %u, kind %u",
                record->Handle, (unsigned int)operation->Phase, (unsigned int)operation->Kind);
        return OS_EBUSY;
    }
    
    // The call was only waiting for a buffer range, so it started no device
    // work. Lock the range and end the operation.
    assert(operation->Mapping != NULL);
    SHMAccessQuarantine(&operation->Mapping->Access);
    SHMDeviceOperationEnd(&operation);
    return OS_EOK;
}
