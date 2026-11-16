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

#include "device_private.h"
#include <heap.h>
#include <assert.h>
#include <debug.h>
#include <shm/record.h>
#include <string.h>

oserr_t
SHMDeviceContextPrepareBound(
    _In_  uuid_t                            binding,
    _In_  const OSDmaBoundOpenParameters_t* parameters,
    _Out_ SHMDeviceContext_t**              contextOut)
{
    SHMDeviceContext_t*         context;
    OSDmaAttachmentParameters_t configuration;
    oserr_t                     status;

    context = kmalloc(sizeof(SHMDeviceContext_t));
    if (context == NULL) {
        return OS_EOOM;
    }

    SHMDeviceContextInitialize(context);

    // A cancel pass can visit this context as soon as the binding points to it.
    // Set up the reference count and mapping list first. No mappings can be
    // added until building the address rules and publishing the context finish.
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
    
    status = DMAAddressSpaceCreateDirect(
        configuration.Ranges,
        configuration.RangeCount,
        &context->AddressSpace
    );
    if (status != OS_EOK) {
        ERROR("SHMDeviceContextPrepareBound: DMAAddressSpaceCreateDirect binding %u failed: %u", binding, status);
        SHMDeviceContextRelease(&context);
        return status;
    }

    *contextOut = context;
    return OS_EOK;
}

oserr_t
SHMDeviceContextPublishBound(
    _In_ SHMDeviceContext_t* context)
{
    return DMABindingPublishContext(context->Binding);
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
    if (current->Binding != NULL && hasMappings) {
        // Whether the context handle or a mapping handle closes first, both
        // paths must block the same recovery group. Drop MappingLock before
        // shutting down the group so we do not hold it while taking manager locks.
        DMABindingRevoke(current->Binding);
    }
    SHMDeviceContextRelease(context);
}

// Caller must hold a context reference. Returning a mapping reference keeps the
// list entry readable under MappingLock even after its device resources are freed.
static oserr_t
__NextMapping(
    _In_     SHMDeviceContext_t*  context,
    _In_Opt_ SHMDeviceMapping_t*  current,
    _In_     bool                 onlyAbandoned,
    _Out_    SHMDeviceMapping_t** mappingOut)
{
    SHMDeviceMapping_t* mapping;
    oserr_t             status = OS_EOK;

    *mappingOut = NULL;
    
    MutexLock(&context->MappingLock);
    mapping = current == NULL ? context->Mappings : current->Lifetime.Next;
    for (; mapping != NULL; mapping = mapping->Lifetime.Next) {
        if (onlyAbandoned && !mapping->Lifetime.Abandoned) {
            continue;
        }

        if (mapping->Lifetime.Cleaning) {
            status = OS_EBUSY;
            break;
        }

        if (mapping->State == SHMDeviceMappingStopped) {
            continue;
        }

        status = SHMDeviceMappingAcquire(mapping);
        if (status == OS_ECANCELLED) {
            // A final release already hit zero and is waiting to unlink this
            // entry. It cannot free the struct until we drop MappingLock.
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

    status = __NextMapping(context, NULL, false, &mapping);
    if (status != OS_EOK) {
        ERROR("SHMDeviceContextRevoke: initial __NextMapping context %p failed: %u", context, status);
        return status;
    }

    while (mapping != NULL) {
        // The recovery group already blocked new work. Locking the range updates
        // only the access entry under Buffer->Mutex. Any caller still waiting
        // on a range holds onto its operation token until it returns or exit
        // cleanup retires it.
        SHMAccessQuarantine(&mapping->Access);
        
        status = __NextMapping(context, mapping, false, &next);
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

// Caller holds a walk reference and runs during an exclusive stop-cleanup
// pass. A live mapping handle keeps the second reference even after its device
// resources are freed.
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

    // Only abandoned mappings hand their original handle reference over to this
    // pass. Never drop a live handle's reference or clear its object pointer.
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

    // Unbound contexts rely on the caller's own proof that hardware stopped
    // and no new mappings are being added. They only clean abandoned mappings.
    status = __NextMapping(
        context,
        NULL,
        context->Binding == NULL,
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

        // Advance before dropping the reference on the current entry. If freeing
        // device addresses failed, the entry stays listed for a retry without
        // repeating cache syncs.
        status = __NextMapping(
            context,
            mapping,
            context->Binding == NULL,
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

// Caller must hold the record in Updating. Save PendingOperation before taking
// references or incrementing the group counter, so exit cleanup never mistakes
// interrupted work for an idle handle.
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

    assert(atomic_load(&record->State) == SHMRecordUpdating);
    assert(record->PendingOperation == NULL && operation->Record == NULL);
    
    *operation = (SHMDeviceOperation_t) {
        .Record = record, .Context = context, .Mapping = mapping,
        .Kind = kind, .Phase = SHMDeviceOperationRunning
    };
    record->PendingOperation = operation;
    
    status = SHMDeviceContextAcquire(context);
    if (status != OS_EOK) {
        goto error;
    }

    operation->Setup = SHMDeviceOperationSetupContext;
    if (mapping != NULL) {
        status = SHMDeviceMappingAcquire(mapping);
        if (status != OS_EOK) {
            goto error;
        }
        operation->Setup = SHMDeviceOperationSetupMapping;
    }

    if (context->Binding != NULL) {
        status = DMABindingBeginOperation(
            context->Binding, kind == SHMDeviceOperationRelease, &operation->Stopped
        );
        if (status != OS_EOK) {
            goto error;
        }
        operation->Setup = SHMDeviceOperationSetupCounted;
    }

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
        return context->Binding == NULL;
    }

    if (operation->Record == NULL || operation->Record->PendingOperation != operation ||
        operation->Context != context ||
        (context->Binding != NULL && operation->Setup != SHMDeviceOperationSetupCounted)) {
        return false;
    }
    return operation->Mapping == mapping && operation->Kind == kind;
}

oserr_t
SHMDeviceOperationBeginAccess(
    _In_Opt_ SHMDeviceOperation_t*       operation,
    _In_     SHMAccess_t*                access,
    _In_     size_t                      offset,
    _In_     size_t                      length,
    _In_     bool                        write,
    _In_Opt_ const struct SHMAccessWait* wait)
{
    oserr_t status;

    if (operation != NULL) {
        operation->Phase = SHMDeviceOperationRangeAdmission;
    }

    status = SHMAccessBegin(
        access,
        offset,
        length,
        SHMAccessHolderDevice,
        write,
        wait
    );
    if (status == OS_EFORKED) {
        // The background thread resumes with this same token, so leave it intact.
        return status;
    }

    if (operation != NULL) {
        operation->Phase = SHMDeviceOperationRunning;
    }

    if (status != OS_EOK) {
        return status;
    }
    return operation == NULL ? OS_EOK : SHMDeviceOperationCheck(operation);
}

oserr_t
SHMDeviceOperationCheck(
    _In_ const SHMDeviceOperation_t* operation)
{
    assert(operation != NULL && operation->Record != NULL);
    if (operation->Context->Binding != NULL && operation->Kind == SHMDeviceOperationSubmit) {
        return DMABindingCheckOperation(operation->Context->Binding);
    }
    return OS_EOK;
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

    // The token struct can live inside the mapping being freed. Copy what we
    // need and clear PendingOperation before dropping the mapping reference.
    // Keep context alive until both the group count and any cancel retry finish.
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

    if (operation->Context->Binding != NULL) {
        DMABindingRevoke(operation->Context->Binding);
    }

    if (operation->Phase != SHMDeviceOperationRangeAdmission) {
        // A process exit may have interrupted allocation, copying, or cache
        // syncs mid-way. Since we cannot tell what state was left behind,
        // return OS_EBUSY so the group count and storage stay pinned until reboot.
        return OS_EBUSY;
    }
    
    assert(operation->Mapping != NULL);
    SHMAccessQuarantine(&operation->Mapping->Access);
    SHMDeviceOperationEnd(&operation);
    return OS_EOK;
}
