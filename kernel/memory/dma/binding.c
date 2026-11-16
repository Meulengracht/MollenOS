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
#include <heap.h>
#include <limits.h>
#include <memoryspace.h>
#include <string.h>

#include "attachment_private.h"
#include "../utils.h"

void
DMABindingCloseLocked(
    _In_ DMABinding_t* binding)
{
    // Once closed, a binding stays closed. Keep the last published request ID
    // intact so a late retry cannot reopen DMA after cleanup starts.
    bool hasContext;

    MutexLock(&binding->Group->Lock);
    binding->State = DMABindingClosed;
    hasContext = binding->Context.Pointer != NULL;
    MutexUnlock(&binding->Group->Lock);
    if (hasContext) {
        DMARecoveryBlockLocked(binding->Manager, binding->Group);
    }
}

static void
__CloseBinding(
    _In_ void* resource)
{
    DMABinding_t* binding = resource;

    MutexLock(&binding->Manager->Lock);
    DMABindingCloseLocked(binding);
    MutexUnlock(&binding->Manager->Lock);
    DMARecoveryCancel(binding->Group);
}

static void
__DeleteBinding(
    _In_ void* resource)
{
    DMABinding_t* binding = resource;
    DMAManager_t* manager = binding->Manager;
    bool          notifyManager = false;

    // Attachment->Binding.Current does not hold a reference to this binding,
    // so clear it under manager->Lock before freeing the memory.
    MutexLock(&manager->Lock);
    MutexLock(&binding->Group->Lock);
    assert(binding->State == DMABindingClosed);
    assert(binding->Context.Pointer == NULL);
    if (binding->Attachment != NULL) {
        assert(binding->Attachment->Binding.Current == binding);
        binding->Attachment->Binding.Current = NULL;

        // Only published bindings point back to an attachment. Queue the idle
        // event now that the driver's last handle reference has dropped.
        DMAAttachmentQueueEventLocked(
            manager,
            binding->Attachment,
            binding->Generation,
            binding->Group->Gate.State == DMARecoveryLive ? 0 : binding->Group->Stop.Epoch,
            DMA_ATTACHMENT_IDLE
        );
        notifyManager = manager->State == DMAManagerLive;
    }
    MutexUnlock(&binding->Group->Lock);
    MutexUnlock(&manager->Lock);

    if (notifyManager) {
        // This binding holds a reference to manager, so it is safe to signal
        // watchers without Lock. If signaling fails, the event stays queued.
        DMAManagerNotifyEvents(manager, __func__);
    }
    DMARecoveryGroupRelease(&binding->Group);
    kfree(binding);
    DMAManagerRelease(&manager);
}

static bool
__IsBindingLive(
    _In_ DMAAttachment_t* attachment,
    _In_ DMAAttachmentBindingState_t* bindingState)
{
    bool isBindingLive = false;

    MutexLock(&attachment->RecoveryGroup->Lock);
    if (bindingState->Current != NULL) {
        bool bindingLive = bindingState->Current->State == DMABindingLive;
        bool groupLive = attachment->RecoveryGroup->Gate.State == DMARecoveryLive;
        isBindingLive = bindingLive && groupLive;
    }
    MutexUnlock(&attachment->RecoveryGroup->Lock);

    return isBindingLive;
}

// Requires manager->Lock, UpdateLock, and a reference to the driver's security
// context. Retries check against the last published request, and a failed setup
// leaves that history unchanged. On success, either takes a reference to the
// active binding (retry) or reserves a new generation in newGenerationOut.
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

    MutexLock(&attachment->RecoveryGroup->Lock);
    if (attachment->RecoveryGroup->Gate.State == DMARecoveryBlocked ||
        attachment->RecoveryGroup->Gate.State == DMARecoveryConfirmed) {
        MutexUnlock(&attachment->RecoveryGroup->Lock);
        return OS_ECANCELLED;
    }
    MutexUnlock(&attachment->RecoveryGroup->Lock);

    if (parameters->RequestId < bindingState->RequestId) {
        return OS_ECANCELLED;
    }

    if (parameters->RequestId == bindingState->RequestId) {
        bool isBindingLive = __IsBindingLive(attachment, bindingState);
        if (!isBindingLive) {
            return OS_ECANCELLED;
        }

        // Check that the driver still holds the handle and take a reference to
        // keep it alive, so retrying a lost reply returns the same handle.
        return AcquireHandleReferenceInContext(
            registration->Receiver,
            bindingState->Current->ID,
            HandleTypeDMABinding,
            (void**)&registration->Binding
        );
    }

    if (attachment->Event.Sequence == UINT64_MAX) {
        return OS_EOVERFLOW;
    }

    if (bindingState->Current != NULL || attachment->Event.Queued) {
        return OS_EBUSY;
    }

    if (manager->Registry.NextBindingGeneration == UINT64_MAX) {
        return OS_EOVERFLOW;
    }

    // A cleaned recovery group can only be reused after every old binding and
    // waiting event in the group is gone. Advance the stop round counter before
    // switching back to Live so old stop tickets can never match the new session.
    MutexLock(&attachment->RecoveryGroup->Lock);
    if (attachment->RecoveryGroup->Gate.State == DMARecoveryCleaned) {
        foreach (node, &manager->Registry.Attachments) {
            DMAAttachment_t* member = node->value;

            if (member->RecoveryGroup == attachment->RecoveryGroup &&
                (member->Binding.Current != NULL || member->Event.Queued)) {
                MutexUnlock(&attachment->RecoveryGroup->Lock);
                return OS_EBUSY;
            }
        }

        if (attachment->RecoveryGroup->Gate.Operations != 0 ||
            attachment->RecoveryGroup->Stop.Phase != DMARecoveryStopIdle) {
            MutexUnlock(&attachment->RecoveryGroup->Lock);
            return OS_EBUSY;
        }

        if (attachment->RecoveryGroup->Stop.Epoch >= UINT64_MAX - 1) {
            MutexUnlock(&attachment->RecoveryGroup->Lock);
            return OS_EOVERFLOW;
        }

        attachment->RecoveryGroup->Stop.Epoch++;
        attachment->RecoveryGroup->Gate.State = DMARecoveryLive;
    }
    if (attachment->RecoveryGroup->Gate.State != DMARecoveryLive) {
        MutexUnlock(&attachment->RecoveryGroup->Lock);
        return OS_EBUSY;
    }
    MutexUnlock(&attachment->RecoveryGroup->Lock);

    registration->CreatesBinding = true;
    *newGenerationOut = manager->Registry.NextBindingGeneration++;
    return OS_EOK;
}

// Requires UpdateLock and the caller's manager reference. Unlock UpdateLock
// before dropping references so handle cleanup cannot deadlock on registry locks.
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
        uuid_t  releasedHandle = bindingHandle;
        oserr_t releaseStatus = DestroyHandleInContext(prepared->Receiver, releasedHandle);

        // Another thread may have already closed this unpublished handle.
        if (releaseStatus != OS_EOK && releaseStatus != OS_EINCOMPLETE && releaseStatus != OS_ENOENT) {
            ERROR("%s: DestroyHandleInContext %u failed: %u", __func__, releasedHandle, releaseStatus);
        }
    }
    __ReleaseMemoryHandleReference(bindingHandle, __func__);
    __ReleaseMemoryHandleReference(prepared->Receiver->GlobalHandleID, __func__);
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

    // Look up the driver's security context before using its ID or reserving a
    // generation. Keep its handle table alive until publish or cancel finishes.
    status = MSContextAcquireSecurity(parameters->Principal, &receiver);
    if (status != OS_EOK) {
        ERROR("DMAManagerPrepareBinding: MSContextAcquireSecurity principal %llu failed: %u",
              (unsigned long long)parameters->Principal, status);
        return status;
    }

    registration = kmalloc(sizeof(DMABindingRegistration_t));
    if (registration == NULL) {
        __ReleaseMemoryHandleReference(receiver->GlobalHandleID, __func__);
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

    // UpdateLock keeps this group registered until publish or cancel finishes.
    // Acquire a reference before creating the handle in case the driver guesses
    // the handle ID and closes it right away.
    status = DMARecoveryGroupAcquire(registration->Attachment->RecoveryGroup);
    if (status != OS_EOK) {
        goto error;
    }
    binding->Group = registration->Attachment->RecoveryGroup;

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

    // The driver could guess the handle ID and close it right after it is added.
    // Hold manager->Lock so the close callback waits until we save the ID and
    // take our publish reference.
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
    // Both new bindings and retries reach here with a handle reference.
    // Publish still checks if the handle closed later. Returning the handle ID
    // and generation alone does not enable DMA yet.
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
        // The handle was created, but taking our publish reference failed. The
        // driver might already have closed and freed the binding, so clean up
        // via the saved handle ID without touching the binding pointer.
        uuid_t releasedHandle = bindingHandle;
        oserr_t releaseStatus = DestroyHandleInContext(receiver, releasedHandle);

        // Another thread may have already closed this unpublished handle.
        if (releaseStatus != OS_EOK && releaseStatus != OS_EINCOMPLETE && releaseStatus != OS_ENOENT) {
            ERROR("%s: DestroyHandleInContext %u failed: %u", __func__, releasedHandle, releaseStatus);
        }
    } else {
        if (binding != NULL) {
            DMARecoveryGroupRelease(&binding->Group);
        }
        kfree(binding);
        if (hasManagerReference) {
            __ReleaseMemoryHandleReference(manager->ID, __func__);
        }
    }
    __ReleaseMemoryHandleReference(receiver->GlobalHandleID, __func__);
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
    MutexLock(&binding->Group->Lock);
    if (manager->State != DMAManagerLive || binding->State == DMABindingClosed ||
        binding->Group->Gate.State != DMARecoveryLive) {
        status = OS_ECANCELLED;
    } else if (prepared->CreatesBinding) {
        // UpdateLock blocks attachment removal and other bind calls. Switch
        // the binding to Live and save it on the attachment together under Lock.
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
    MutexUnlock(&binding->Group->Lock);
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
        // Extra handle references may delay the close callback. Mark this
        // unpublished binding closed right away while keeping earlier request
        // history intact.
        MutexLock(&prepared->Manager->Lock);
        DMABindingCloseLocked(prepared->Binding);
        MutexUnlock(&prepared->Manager->Lock);
    }
    __FinishBindingRegistration(registration, true);
}

// Requires group->Lock and a binding reference. Sharing this helper ensures
// context publish and operation checks follow the exact same rules.
static oserr_t
__CheckBindingLiveLocked(
    _In_ DMABinding_t* binding)
{
    if (binding->State != DMABindingLive || binding->Group->Gate.State != DMARecoveryLive) {
        return OS_ECANCELLED;
    }
    return OS_EOK;
}

oserr_t
DMABindingAttachContext(
    _In_  uuid_t                            handle,
    _In_  const OSDmaBoundOpenParameters_t* parameters,
    _In_  SHMDeviceContext_t*               context,
    _Out_ DMABinding_t**                    bindingOut,
    _Out_ OSDmaAttachmentParameters_t*      configurationOut)
{
    MSContextSecurity_t* caller = MemorySpaceGetSecurityContext();
    DMABinding_t*        binding;
    oserr_t              status;

    if (parameters->Generation == 0) {
        return OS_EINVALPARAMS;
    }

    if (caller == NULL) {
        return OS_EPERMISSIONS;
    }

    status = AcquireHandleReferenceInContext(caller, handle, HandleTypeDMABinding, (void**)&binding);
    if (status != OS_EOK) {
        return status;
    }

    MutexLock(&binding->Group->Lock);
    if (binding->Principal != (uint64_t)caller->GlobalHandleID) {
        status = OS_EPERMISSIONS;
    } else if (binding->Generation != parameters->Generation) {
        status = OS_ECANCELLED;
    } else {
        status = __CheckBindingLiveLocked(binding);
    }

    // If the context is already attached, we cannot attach another one.
    if (status == OS_EOK && binding->Context.Pointer != NULL) {
        status = OS_EBUSY;
    } else if (status == OS_EOK && binding->Group->Gate.Operations == UINT_MAX) {
        // If the number of active operations has reached the maximum, 
        // we cannot attach a new context.
        status = OS_EOVERFLOW;
    } else if (status == OS_EOK) {
        // Store the attachment configuration and update the address limit if necessary.
        *configurationOut = binding->Attachment->Configuration;
        if (parameters->AddressLimit < configurationOut->AddressLimit) {
            configurationOut->AddressLimit = parameters->AddressLimit;
        }

        binding->Context.Pointer = context;
        binding->Context.Phase = DMABindingContextPreparing;
        binding->Group->Gate.Operations++;
        *bindingOut = binding;
    }
    MutexUnlock(&binding->Group->Lock);
    if (status != OS_EOK) {
        __ReleaseMemoryHandleReference(binding->ID, __func__);
    }
    return status;
}

oserr_t
DMABindingPublishContext(
    _In_ DMABinding_t* binding)
{
    oserr_t status;

    MutexLock(&binding->Group->Lock);
    assert(binding->Context.Phase == DMABindingContextPreparing);
    status = __CheckBindingLiveLocked(binding);
    if (status == OS_EOK) {
        binding->Context.Phase = DMABindingContextLive;
        assert(binding->Group->Gate.Operations != 0);
        binding->Group->Gate.Operations--;
    }
    MutexUnlock(&binding->Group->Lock);
    return status;
}

void
DMABindingDetachContext(
    _InOut_ DMABinding_t**      binding,
    _In_    SHMDeviceContext_t* context)
{
    DMABinding_t* current = *binding;
    bool          preparing;

    if (current == NULL) {
        return;
    }
    
    *binding = NULL;
    
    MutexLock(&current->Group->Lock);
    assert(current->Context.Pointer == context);
    preparing = current->Context.Phase == DMABindingContextPreparing;
    memset(&current->Context, 0, sizeof(DMABindingContextState_t));
    MutexUnlock(&current->Group->Lock);

    if (preparing) {
        // Cancelling an open before it finishes publishing may be the last
        // active operation in the group. Use DMABindingEndOperation so any
        // service waiting to retry stop gets notified.
        DMABindingEndOperation(current);
    }
    __ReleaseMemoryHandleReference(current->ID, __func__);
}

oserr_t
DMABindingBeginOperation(
    _In_  DMABinding_t* binding,
    _In_  bool          release,
    _Out_ bool*         stoppedOut)
{
    DMARecoveryGroup_t* group = binding->Group;
    oserr_t             status;

    MutexLock(&group->Lock);
    status = __CheckBindingLiveLocked(binding);
    if (release && group->Gate.State != DMARecoveryLive) {
        status = group->Gate.State == DMARecoveryBlocked ||
            group->Stop.Phase == DMARecoveryStopCleaning ? OS_EBUSY : OS_EOK;
    }

    if (group->Gate.ManagerClosed) {
        status = OS_ECANCELLED;
    }

    if (status == OS_EOK && binding->Context.Phase != DMABindingContextLive) {
        status = OS_ECANCELLED;
    }

    if (status == OS_EOK) {
        if (group->Gate.Operations == UINT_MAX) {
            status = OS_EOVERFLOW;
        } else {
            group->Gate.Operations++;
            *stoppedOut = release && (group->Gate.State == DMARecoveryConfirmed ||
                                     group->Gate.State == DMARecoveryCleaned);
        }
    }
    MutexUnlock(&group->Lock);
    return status;
}

oserr_t
DMABindingCheckOperation(
    _In_ DMABinding_t* binding)
{
    oserr_t status;

    MutexLock(&binding->Group->Lock);
    status = __CheckBindingLiveLocked(binding);
    MutexUnlock(&binding->Group->Lock);
    return status;
}

void
DMABindingRevoke(
    _In_ DMABinding_t* binding)
{
    MutexLock(&binding->Manager->Lock);
    DMARecoveryBlockLocked(binding->Manager, binding->Group);
    MutexUnlock(&binding->Manager->Lock);
    DMARecoveryCancel(binding->Group);
}
