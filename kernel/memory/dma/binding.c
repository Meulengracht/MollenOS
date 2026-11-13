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

#include <assert.h>
#include <debug.h>
#include <handles/global.h>
#include <handles/set.h>
#include <ioset.h>
#include <heap.h>
#include <memoryspace.h>
#include <string.h>

#include "attachment_private.h"

void
DMABindingCloseLocked(
    _In_ DMABinding_t* binding)
{
    // A closed binding stays closed. Keep the published request history so a
    // late retry cannot reopen the grant after the close callback has run.
    binding->State = DMABindingClosed;
}

static void
__CloseBinding(
    _In_ void* resource)
{
    DMABinding_t* binding = resource;

    MutexLock(&binding->Manager->Lock);
    DMABindingCloseLocked(binding);
    MutexUnlock(&binding->Manager->Lock);
}

static void
__DeleteBinding(
    _In_ void* resource)
{
    DMABinding_t* binding = resource;
    DMAManager_t* manager = binding->Manager;
    bool          notifyManager = false;

    // The attachment pointer does not keep the binding alive. Clear it under
    // Lock before freeing the binding so registry updates cannot use freed memory.
    MutexLock(&manager->Lock);
    assert(binding->State == DMABindingClosed);
    if (binding->Attachment != NULL) {
        assert(binding->Attachment->Binding.Current == binding);
        binding->Attachment->Binding.Current = NULL;

        // Only published bindings point to an attachment. Queue the idle event
        // here, after the last binding reference has been released.
        DMAAttachmentQueueEventLocked(
            manager,
            binding->Attachment,
            binding->Generation,
            0,
            DMA_ATTACHMENT_IDLE
        );
        notifyManager = manager->State == DMAManagerLive;
    }
    MutexUnlock(&manager->Lock);

    if (notifyManager) {
        // The binding keeps the manager alive while we notify without Lock.
        // If notification fails, the event stays queued for the next read.
        MarkHandle(manager->ID, IOSETIN);
    }
    kfree(binding);
    DMAManagerRelease(&manager);
}

// Requires Lock, UpdateLock and a temporary reference to the receiver's context.
// Retry checks use the last published request. Failed preparation leaves it unchanged.
// A retry references the current binding; a new request reserves newGenerationOut.
static oserr_t
__PrepareBindingRequestLocked(
    _In_    const OSDmaBindParameters_t* parameters,
    _InOut_ DMABindingRegistration_t*    registration,
    _Out_   uint64_t*                    newGenerationOut)
{
    DMAManager_t*                manager = registration->Manager;
    DMAAttachment_t*             attachment = NULL;
    DMAAttachmentBindingState_t* bindingState;

    if (manager->State != DMAManagerLive) {
        return OS_ECANCELLED;
    }

    foreach (node, &manager->Registry.Attachments) {
        attachment = node->value;
        if (attachment->ID == parameters->Attachment) {
            break;
        }
        attachment = NULL;
    }
    if (attachment == NULL) {
        return OS_ENOENT;
    }

    registration->Attachment = attachment;
    bindingState = &attachment->Binding;

    if (bindingState->Principal != 0 && bindingState->Principal != parameters->Principal) {
        return OS_EPERMISSIONS;
    }

    if (parameters->RequestId < bindingState->RequestId) {
        return OS_ECANCELLED;
    }

    if (parameters->RequestId == bindingState->RequestId) {
        if (bindingState->Current == NULL || bindingState->Current->State != DMABindingLive) {
            return OS_ECANCELLED;
        }
        // Check that the receiver still has the grant and take an operation
        // reference to it. Retrying a lost reply must not add a driver grant.
        return AcquireHandleReferenceInContext(
            registration->Receiver,
            bindingState->Current->ID,
            HandleTypeDMABinding,
            (void**)&registration->Binding
        );
    }

    if (attachment->Event.Exhausted) {
        return OS_EOVERFLOW;
    }

    if (bindingState->Current != NULL || attachment->Event.Queued) {
        return OS_EBUSY;
    }

    if (manager->Registry.NextBindingGeneration == UINT64_MAX) {
        return OS_EOVERFLOW;
    }

    registration->CreatesBinding = true;
    *newGenerationOut = manager->Registry.NextBindingGeneration++;
    return OS_EOK;
}

// Requires UpdateLock and the caller's manager operation reference.
// Release UpdateLock before dropping references, which may run registry cleanup.
static void
__FinishBindingRegistration(
    _InOut_ DMABindingRegistration_t** registration,
    _In_    bool                       cancel)
{
    DMABindingRegistration_t* prepared = *registration;
    uuid_t                    bindingHandle = prepared->Binding->ID;

    *registration = NULL;

    MutexUnlock(&prepared->Manager->UpdateLock);
    if (cancel && prepared->CreatesBinding) {
        DestroyHandleInContext(prepared->Receiver, bindingHandle);
    }
    ReleaseGlobalHandle(bindingHandle);
    ReleaseGlobalHandle(prepared->Receiver->GlobalHandleID);
    kfree(prepared);
}

oserr_t
DMAManagerPrepareBinding(
    _In_  DMAManager_t*                manager,
    _In_  const OSDmaBindParameters_t* parameters,
    _Out_ DMABindingRegistration_t**   registrationOut,
    _Out_ OSDmaBindingResult_t*        bindingOut)
{
    MSContextSecurity_t*      receiver;
    DMABindingRegistration_t* registration;
    DMABinding_t*             binding = NULL;
    uuid_t                    bindingHandle = UUID_INVALID;
    uint64_t                  generation = 0;
    bool                      hasManagerReference = false;
    oserr_t                   status;

    if (parameters->Attachment == 0 || parameters->RequestId == 0) {
        return OS_EINVALPARAMS;
    }

    if (parameters->RequestId == UINT64_MAX) {
        return OS_EOVERFLOW;
    }

    // Resolve the full principal before casting it or reserving a generation.
    // Keep the receiver's handle table alive until we publish or cancel.
    status = MSContextAcquireSecurity(parameters->Principal, &receiver);
    if (status != OS_EOK) {
        ERROR("DMAManagerPrepareBinding: MSContextAcquireSecurity principal %llu failed: %u",
              (unsigned long long)parameters->Principal, status);
        return status;
    }

    registration = kmalloc(sizeof(DMABindingRegistration_t));
    if (registration == NULL) {
        ReleaseGlobalHandle(receiver->GlobalHandleID);
        return OS_EOOM;
    }

    memset(registration, 0, sizeof(DMABindingRegistration_t));
    registration->Manager = manager;
    registration->Receiver = receiver;

    MutexLock(&manager->UpdateLock);
    MutexLock(&manager->Lock);
    status = __PrepareBindingRequestLocked(parameters, registration, &generation);
    MutexUnlock(&manager->Lock);
    if (status != OS_EOK) {
        ERROR("DMAManagerPrepareBinding: __PrepareBindingRequestLocked attachment %llu "
              "for manager %u failed: %u",
              (unsigned long long)parameters->Attachment, manager->ID, status);
        goto error;
    }

    if (!registration->CreatesBinding) {
        goto ready;
    }

    binding = kmalloc(sizeof(DMABinding_t));
    if (binding == NULL) {
        status = OS_EOOM;
        goto error;
    }

    memset(binding, 0, sizeof(DMABinding_t));
    binding->ID = UUID_INVALID;
    binding->Manager = manager;
    binding->Principal = parameters->Principal;
    binding->RequestId = parameters->RequestId;
    binding->Generation = generation;
    binding->State = DMABindingPreparing;

    status = AcquireGlobalHandle(manager->ID, HandleTypeDMAManager, NULL);
    if (status != OS_EOK) {
        ERROR("DMAManagerPrepareBinding: AcquireGlobalHandle manager %u failed: %u",
              manager->ID, status);
        goto error;
    }
    hasManagerReference = true;

    MutexLock(&manager->Lock);
    if (manager->State != DMAManagerLive) {
        ERROR("DMAManagerPrepareBinding: manager %u is no longer live", manager->ID);
        status = OS_ECANCELLED;
        goto close_binding;
    }

    // The receiver may guess the handle and close it as soon as it exists.
    // Hold Lock so the callback waits until we have stored the ID and taken
    // the reference needed for publication.
    bindingHandle = CreateHandleInContext(
        HandleTypeDMABinding,
        __CloseBinding,
        __DeleteBinding,
        binding,
        receiver
    );
    if (bindingHandle == UUID_INVALID) {
        ERROR("DMAManagerPrepareBinding: CreateHandleInContext for manager %u failed", manager->ID);
        status = OS_EOOM;
        goto close_binding;
    }
    binding->ID = bindingHandle;

    status = AcquireGlobalHandle(bindingHandle, HandleTypeDMABinding, NULL);
    if (status != OS_EOK) {
        ERROR("DMAManagerPrepareBinding: AcquireGlobalHandle binding %u failed: %u",
              bindingHandle, status);
        goto close_binding;
    }
    MutexUnlock(&manager->Lock);
    registration->Binding = binding;

ready:
    // Both new bindings and retries reach here with an operation reference.
    // Publish checks for a close that happens later. Returning the handle and
    // generation does not enable DMA.
    *bindingOut = (OSDmaBindingResult_t) {
        .Handle = registration->Binding->ID,
        .Generation = registration->Binding->Generation
    };
    *registrationOut = registration;
    return OS_EOK;

close_binding:
    DMABindingCloseLocked(binding);
    MutexUnlock(&manager->Lock);

error:
    MutexUnlock(&manager->UpdateLock);
    if (bindingHandle != UUID_INVALID) {
        // The grant was created, but taking the publication reference failed.
        // The receiver may already have closed and freed the binding. Use the
        // saved handle and the receiver reference without touching the binding.
        DestroyHandleInContext(receiver, bindingHandle);
    } else {
        kfree(binding);
        if (hasManagerReference) {
            ReleaseGlobalHandle(manager->ID);
        }
    }
    ReleaseGlobalHandle(receiver->GlobalHandleID);
    kfree(registration);
    return status;
}

oserr_t
DMAManagerPublishBinding(
    _InOut_ DMABindingRegistration_t** registration)
{
    DMABindingRegistration_t*    prepared = *registration;
    DMAManager_t*                manager = prepared->Manager;
    DMABinding_t*                binding = prepared->Binding;
    DMAAttachmentBindingState_t* bindingState;
    oserr_t                      status = OS_EOK;

    MutexLock(&manager->Lock);
    if (manager->State != DMAManagerLive || binding->State == DMABindingClosed) {
        status = OS_ECANCELLED;
    } else if (prepared->CreatesBinding) {
        // UpdateLock prevents attachment removal and another publication.
        // Make the grant live under Lock, together with the attachment pointer
        // and request history.
        assert(binding->State == DMABindingPreparing);
        bindingState = &prepared->Attachment->Binding;
        assert(bindingState->Current == NULL);
        binding->Attachment = prepared->Attachment;
        bindingState->Current = binding;
        bindingState->Principal = binding->Principal;
        bindingState->RequestId = binding->RequestId;
        bindingState->Generation = binding->Generation;
        binding->State = DMABindingLive;
    }
    MutexUnlock(&manager->Lock);
    if (status != OS_EOK) {
        ERROR("DMAManagerPublishBinding: manager %u or binding %u closed before publication",
              manager->ID, binding->ID);
        return status;
    }

    __FinishBindingRegistration(registration, false);
    return OS_EOK;
}

void
DMAManagerCancelBinding(
    _InOut_ DMABindingRegistration_t** registration)
{
    DMABindingRegistration_t* prepared = *registration;

    if (prepared == NULL) {
        return;
    }

    if (prepared->CreatesBinding) {
        // Temporary grant references may delay the close callback. Mark the
        // unpublished binding closed now and leave the published history intact.
        MutexLock(&prepared->Manager->Lock);
        DMABindingCloseLocked(prepared->Binding);
        MutexUnlock(&prepared->Manager->Lock);
    }
    __FinishBindingRegistration(registration, true);
}
