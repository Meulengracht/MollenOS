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
 * Driver bindings for DMA attachments. Handles creating and publishing binding
 * handles for drivers, attaching shared-memory (SHM) device contexts, and
 * checking whether a recovery group is healthy when DMA operations start.
 */

#include <assert.h>
#include <debug.h>
#include <handles/global.h>
#include <heap.h>
#include <memoryspace.h>
#include <string.h>

#include "attachment_private.h"
#include "../utils.h"

// Updates the binding state under Access.Lock so fast DMA entry paths
// (which check Access.Lock without taking group->Lock) see changes right away.
static void
__SetBindingState(
    _In_ DMABinding_t*        binding,
    _In_ enum DMABindingState state)
{
    SpinlockAcquireIrq(&binding->Group->Access.Lock);
    binding->State = state;
    SpinlockReleaseIrq(&binding->Group->Access.Lock);
}

// Marks a binding closed and blocks its recovery group if a context is attached.
void
DMABindingCloseLocked(
    _In_ DMABinding_t* binding)
{
    // Once closed, a binding stays closed. Keep the last published request ID
    // intact so a late retry cannot reopen DMA after cleanup starts.
    bool hasContext;

    MutexLock(&binding->Group->Lock);
    __SetBindingState(binding, DMABindingClosed);
    hasContext = binding->Context.Pointer != NULL;
    MutexUnlock(&binding->Group->Lock);
    if (hasContext) {
        DMARecoveryBlockLocked(binding->Manager, binding->Group);
    }
}

// Handle close callback invoked when the driver closes its binding handle.
// Blocks the recovery group and cancels any attached SHM context.
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
            binding->Group->Access.State == DMARecoveryLive ? 0 : binding->Group->Stop.Round,
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
    TRACE("__DeleteBinding: binding %u (generation %llu) of manager %u deleted",
          binding->ID, (unsigned long long)binding->Generation, manager->ID);
    DMARecoveryGroupRelease(&binding->Group);
    kfree(binding);
    DMAManagerRelease(&manager);
}

// Hold manager->Lock (group->Lock may be held too). A device in a domain can
// only bind once every device in that domain has a published attachment and
// the domain's hardware ID connection has completed. Every binding in a domain
// must belong to the same process security identity (ownerSecurityID), set by
// the first member that binds.
//
// Returns OS_EBUSY if the domain is Closing, not Connected, or still waiting
// for members. Returns OS_EPERMISSIONS if another process already owns the
// domain, or the unit error from DMAAddressSpaceCheck if any domain in the
// same recovery group sits on a failed IOMMU unit.
static oserr_t
__CheckDomainBindingLocked(
    _In_ DMAAttachment_t* attachment,
    _In_ uint64_t         ownerSecurityID)
{
    DMATranslatedDomain_t* domain = attachment->Domain;
    oserr_t                status;

    if (domain != NULL) {
        if (domain->State != DMATranslatedDomainReady ||
            domain->ConnectionState != DMAConnectionConnected || domain->Members != domain->DeviceCount) {
            return OS_EBUSY;
        }

        if (domain->OwnerSecurityID != 0 && domain->OwnerSecurityID != ownerSecurityID) {
            return OS_EPERMISSIONS;
        }
    }

    // A recovery group that was cleaned up must not reopen through a direct
    // device or a device on a healthy unit that shares its reset key, while the
    // unit of another member is still failed. This is also the last health check
    // before a binding is published, made under the manager and group locks.
    foreach (node, &attachment->RecoveryGroup->Manager->Registry.Attachments) {
        DMAAttachment_t* member = node->value;

        if (member->RecoveryGroup == attachment->RecoveryGroup && member->Domain != NULL) {
            status = DMAAddressSpaceCheck(member->Domain->AddressSpace);
            if (status != OS_EOK) {
                return status;
            }
        }
    }
    return OS_EOK;
}

// Call with no manager or group lock held. Scans the manager's domains and
// blocks and cancels any recovery group tied to a failed IOMMU unit. Errors
// are only logged so the caller still gets the result of its own binding step.
// Blocked groups and queued events stay in place so a later call can retry.
static void
__BlockFailedUnitGroupsForBinding(
    _In_ DMAManager_t* manager)
{
    oserr_t status = DMARecoveryBlockFailedUnits(manager);

    if (status != OS_EOK) {
        ERROR("__BlockFailedUnitGroupsForBinding: failure propagation for manager %u failed: %u", manager->ID, status);
    }
}

// Hold manager->Lock. Returns true when the attachment has an active binding
// whose recovery group is also Live.
static bool
__IsBindingLive(
    _In_ DMAAttachment_t* attachment,
    _In_ DMAAttachmentBindingState_t* bindingState)
{
    bool isBindingLive = false;

    MutexLock(&attachment->RecoveryGroup->Lock);
    if (bindingState->Current != NULL) {
        bool bindingLive = bindingState->Current->State == DMABindingLive;
        bool groupLive = attachment->RecoveryGroup->Access.State == DMARecoveryLive;
        isBindingLive = bindingLive && groupLive;
    }
    MutexUnlock(&attachment->RecoveryGroup->Lock);

    return isBindingLive;
}

// Hold manager->Lock. Returns the attachment with the given ID, or NULL.
static DMAAttachment_t*
__FindAttachmentLocked(
    _In_ DMAManager_t* manager,
    _In_ uint64_t      attachmentID)
{
    foreach (node, &manager->Registry.Attachments) {
        DMAAttachment_t* attachment = node->value;

        if (attachment->ID == attachmentID) {
            return attachment;
        }
    }
    return NULL;
}

// Hold manager->Lock. Returns true if any attachment in the recovery group
// still has a binding or a queued event.
static bool
__GroupHasBindingsOrEventsLocked(
    _In_ DMAManager_t*       manager,
    _In_ DMARecoveryGroup_t* group)
{
    foreach (node, &manager->Registry.Attachments) {
        DMAAttachment_t* member = node->value;

        if (member->RecoveryGroup == group &&
            (member->Binding.Current != NULL || member->Event.Queued)) {
            return true;
        }
    }
    return false;
}

// Hold manager->Lock and UpdateLock. Ensures the attachment's recovery group is
// Live so a new binding can start. A stopped and cleaned group can only go
// Live again once every old binding and waiting event in the group is cleared.
// Advancing Stop.Round before switching back to Live ensures old stop tickets
// can never match the new session.
//
// Returns OS_EBUSY if the group is still stopping or not ready for reuse,
// OS_EOVERFLOW if Stop.Round is exhausted, or the error from
// __CheckDomainBindingLocked.
static oserr_t
__MakeGroupLiveLocked(
    _In_ DMAManager_t*    manager,
    _In_ DMAAttachment_t* attachment,
    _In_ uint64_t         ownerSecurityID)
{
    DMARecoveryGroup_t* group = attachment->RecoveryGroup;
    oserr_t             status = OS_EOK;

    MutexLock(&group->Lock);
    if (group->Access.State == DMARecoveryStopCleaned) {
        if (__GroupHasBindingsOrEventsLocked(manager, group) ||
            group->Access.Operations != 0 ||
            group->Stop.Phase != DMARecoveryStopIdle) {
            WARNING("__MakeGroupLiveLocked: recovery group %llu cannot be reused yet, bindings, events or operations remain",
                    (unsigned long long)group->ID);
            status = OS_EBUSY;
            goto unlock;
        }

        if (group->Stop.Round >= UINT64_MAX - 1) {
            ERROR("__MakeGroupLiveLocked: recovery group %llu has no stop rounds left",
                  (unsigned long long)group->ID);
            status = OS_EOVERFLOW;
            goto unlock;
        }

        status = __CheckDomainBindingLocked(attachment, ownerSecurityID);
        if (status != OS_EOK) {
            WARNING("__MakeGroupLiveLocked: recovery group %llu cannot be reused, domain check failed: %u",
                    (unsigned long long)group->ID, status);
            goto unlock;
        }

        group->Stop.Round++;
        SpinlockAcquireIrq(&group->Access.Lock);
        group->Access.State = DMARecoveryLive;
        SpinlockReleaseIrq(&group->Access.Lock);
        TRACE("__MakeGroupLiveLocked: recovery group %llu is live again, stop round %llu",
              (unsigned long long)group->ID, (unsigned long long)group->Stop.Round);
    }

    if (group->Access.State != DMARecoveryLive) {
        WARNING("__MakeGroupLiveLocked: recovery group %llu is not live, access state %u",
                (unsigned long long)group->ID, (unsigned int)group->Access.State);
        status = OS_EBUSY;
    }

unlock:
    MutexUnlock(&group->Lock);
    return status;
}

// Requires manager->Lock, UpdateLock, and a reference to the driver's security
// context. Handles a request with the same request ID as the last published
// one, which is a retry of a lost reply. If the binding is still live and the
// driver still holds the handle, a reference to it is taken so the same handle
// is returned again.
static oserr_t
__RetryBindingRequestLocked(
    _In_    DMAAttachment_t*          attachment,
    _InOut_ DMABindingRegistration_t* registration)
{
    DMAAttachmentBindingState_t* bindingState = &attachment->Binding;

    if (!__IsBindingLive(attachment, bindingState)) {
        return OS_ECANCELLED;
    }

    // Check that the driver still holds the handle and take a reference to
    // keep it alive, so retrying a lost reply returns the same handle.
    return AcquireHandleReferenceInContext(
        registration->DriverSecurity,
        bindingState->Current->ID,
        HandleTypeDMABinding,
        (void**)&registration->Binding
    );
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
    DMAAttachment_t*             attachment;
    DMAAttachmentBindingState_t* bindingState;
    oserr_t                      status;

    if (manager->State != DMAManagerLive) {
        return OS_ECANCELLED;
    }

    attachment = __FindAttachmentLocked(manager, parameters->Attachment);
    if (attachment == NULL) {
        return OS_ENOENT;
    }

    status = __CheckDomainBindingLocked(
        attachment,
        parameters->Principal
    );
    if (status != OS_EOK) {
        return status;
    }

    registration->Attachment = attachment;
    bindingState = &attachment->Binding;

    if (bindingState->OwnerSecurityID != 0 && bindingState->OwnerSecurityID != parameters->Principal) {
        return OS_EPERMISSIONS;
    }

    // A group that is blocked or confirmed is stopping. New requests must wait
    // until the stop has finished.
    MutexLock(&attachment->RecoveryGroup->Lock);
    if (attachment->RecoveryGroup->Access.State == DMARecoveryBlocked ||
        attachment->RecoveryGroup->Access.State == DMARecoveryStopConfirmed) {
        MutexUnlock(&attachment->RecoveryGroup->Lock);
        return OS_ECANCELLED;
    }
    MutexUnlock(&attachment->RecoveryGroup->Lock);

    // Request IDs only go up. An older ID is a late duplicate, and the same
    // ID is a retry of the last request.
    if (parameters->RequestId < bindingState->RequestId) {
        return OS_ECANCELLED;
    }

    if (parameters->RequestId == bindingState->RequestId) {
        return __RetryBindingRequestLocked(attachment, registration);
    }

    if (attachment->Event.Sequence == UINT64_MAX) {
        return OS_EOVERFLOW;
    }

    if (bindingState->Current != NULL || attachment->Event.Queued) {
        return OS_EBUSY;
    }

    if (manager->Registry.NextBindingGeneration == UINT64_MAX) {
        ERROR("__PrepareBindingRequestLocked: manager %u has no binding generations left", manager->ID);
        return OS_EOVERFLOW;
    }

    status = __MakeGroupLiveLocked(manager, attachment, parameters->Principal);
    if (status != OS_EOK) {
        return status;
    }

    registration->CreatesBinding = true;
    *newGenerationOut = manager->Registry.NextBindingGeneration++;
    return OS_EOK;
}

// Destroy the handle of a binding that was never published. Another thread may
// have already closed it, which is not an error.
static void
__DestroyUnpublishedHandle(
    _In_ MSContextSecurity_t* driverSecurity,
    _In_ uuid_t               handle)
{
    oserr_t status = DestroyHandleInContext(driverSecurity, handle);

    if (status != OS_EOK && status != OS_EINCOMPLETE && status != OS_ENOENT) {
        ERROR("__DestroyUnpublishedHandle: DestroyHandleInContext %u failed: %u", handle, status);
    }
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
        __DestroyUnpublishedHandle(prepared->DriverSecurity, bindingHandle);
    }
    __ReleaseMemoryHandleReference(bindingHandle, __func__);
    __ReleaseMemoryHandleReference(prepared->DriverSecurity->GlobalHandleID, __func__);
    kfree(prepared);
}

// Hold UpdateLock. Allocate a binding in the Preparing state. It takes a
// reference on the attachment's recovery group and on the manager, so both stay
// alive for as long as the binding does. On failure nothing is held. Free a
// binding that never got a handle with __FreeUnpublishedBinding.
static oserr_t
__CreateBinding(
    _In_  DMAManager_t*                manager,
    _In_  const OSDmaBindParameters_t* parameters,
    _In_  DMAAttachment_t*             attachment,
    _In_  uint64_t                     generation,
    _Out_ DMABinding_t**               bindingOut)
{
    DMABinding_t* binding;
    oserr_t       status;

    binding = kmalloc(sizeof(DMABinding_t));
    if (binding == NULL) {
        ERROR("__CreateBinding: binding allocation for manager %u failed", manager->ID);
        return OS_EOOM;
    }

    memset(binding, 0, sizeof(DMABinding_t));
    binding->ID = UUID_INVALID;
    binding->Manager = manager;
    binding->OwnerSecurityID = parameters->Principal;
    binding->RequestId = parameters->RequestId;
    binding->Generation = generation;
    atomic_init(&binding->State, DMABindingPreparing);
    atomic_init(&binding->Context.Phase, DMABindingContextNone);

    // UpdateLock keeps this group registered until publish or cancel finishes.
    // Acquire a reference before creating the handle in case the driver guesses
    // the handle ID and closes it right away.
    status = DMARecoveryGroupAcquire(attachment->RecoveryGroup);
    if (status != OS_EOK) {
        ERROR("__CreateBinding: recovery group %llu reference for manager %u failed: %u",
              (unsigned long long)attachment->RecoveryGroup->ID, manager->ID, status);
        kfree(binding);
        return status;
    }
    binding->Group = attachment->RecoveryGroup;

    status = AcquireGlobalHandle(manager->ID, HandleTypeDMAManager, NULL);
    if (status != OS_EOK) {
        ERROR("__CreateBinding: AcquireGlobalHandle manager %u failed: %u",
              manager->ID, status);
        DMARecoveryGroupRelease(&binding->Group);
        kfree(binding);
        return status;
    }

    *bindingOut = binding;
    return OS_EOK;
}

// Release a binding made by __CreateBinding that never got a handle. Once a
// handle exists, the handle's delete callback frees the binding instead.
static void
__FreeUnpublishedBinding(
    _In_ DMABinding_t* binding)
{
    DMAManager_t* manager = binding->Manager;

    DMARecoveryGroupRelease(&binding->Group);
    kfree(binding);
    __ReleaseMemoryHandleReference(manager->ID, __func__);
}

// Hold UpdateLock. Create the driver's handle for the binding and take the
// reference that publish uses. handleOut is set as soon as the handle exists,
// even if a later step fails. The caller must then clean up through that ID
// and not touch the binding pointer, because the driver could have closed the
// handle and freed the binding already. On failure the binding is marked closed.
static oserr_t
__CreateBindingHandle(
    _In_  DMAManager_t*        manager,
    _In_  MSContextSecurity_t* driverSecurity,
    _In_  DMABinding_t*        binding,
    _Out_ uuid_t*              handleOut)
{
    uuid_t  handle;
    oserr_t status;

    MutexLock(&manager->Lock);
    if (manager->State != DMAManagerLive) {
        WARNING("__CreateBindingHandle: manager %u is no longer live", manager->ID);
        status = OS_ECANCELLED;
        goto close_binding;
    }

    // The driver could guess the handle ID and close it right after it is added.
    // Hold manager->Lock so the close callback waits until we save the ID and
    // take our publish reference.
    handle = CreateHandleInContext(
        HandleTypeDMABinding,
        __CloseBinding,
        __DeleteBinding,
        binding,
        driverSecurity
    );
    if (handle == UUID_INVALID) {
        ERROR("__CreateBindingHandle: CreateHandleInContext for manager %u failed", manager->ID);
        status = OS_EOOM;
        goto close_binding;
    }
    binding->ID = handle;
    *handleOut = handle;

    status = AcquireGlobalHandle(handle, HandleTypeDMABinding, NULL);
    if (status != OS_EOK) {
        ERROR("__CreateBindingHandle: AcquireGlobalHandle binding %u failed: %u",
              handle, status);
        goto close_binding;
    }
    MutexUnlock(&manager->Lock);
    return OS_EOK;

close_binding:
    DMABindingCloseLocked(binding);
    MutexUnlock(&manager->Lock);
    return status;
}

oserr_t
DMAManagerPrepareBinding(
    _In_  DMAManager_t*                manager,
    _In_  const OSDmaBindParameters_t* parameters,
    _Out_ DMABindingRegistration_t**   registrationOut,
    _Out_ OSDmaBindingResult_t*        bindingOut)
{
    MSContextSecurity_t*      driverSecurity;
    DMABindingRegistration_t* registration;
    DMABinding_t*             binding = NULL;
    uuid_t                    bindingHandle = UUID_INVALID;
    uint64_t                  generation = 0;
    oserr_t                   status;

    if (parameters->Attachment == 0 || parameters->RequestId == 0) {
        WARNING("DMAManagerPrepareBinding: bind request for manager %u rejected, attachment %llu and request %llu must be non-zero",
                manager->ID, (unsigned long long)parameters->Attachment,
                (unsigned long long)parameters->RequestId);
        return OS_EINVALPARAMS;
    }

    if (parameters->RequestId == UINT64_MAX) {
        ERROR("DMAManagerPrepareBinding: bind request for manager %u rejected, request IDs are exhausted", manager->ID);
        return OS_EOVERFLOW;
    }

    // Look up the driver's security context before using its ID or reserving a
    // generation. Keep its handle table alive until publish or cancel finishes.
    status = MSContextAcquireSecurity(parameters->Principal, &driverSecurity);
    if (status != OS_EOK) {
        ERROR("DMAManagerPrepareBinding: MSContextAcquireSecurity ownerSecurityID %llu failed: %u",
              (unsigned long long)parameters->Principal, status);
        return status;
    }

    registration = kmalloc(sizeof(DMABindingRegistration_t));
    if (registration == NULL) {
        ERROR("DMAManagerPrepareBinding: binding registration allocation for manager %u failed", manager->ID);
        __ReleaseMemoryHandleReference(driverSecurity->GlobalHandleID, __func__);
        return OS_EOOM;
    }

    memset(registration, 0, sizeof(DMABindingRegistration_t));
    registration->Manager = manager;
    registration->DriverSecurity = driverSecurity;

    MutexLock(&manager->UpdateLock);
    MutexLock(&manager->Lock);
    status = __PrepareBindingRequestLocked(parameters, registration, &generation);
    MutexUnlock(&manager->Lock);
    if (status != OS_EOK) {
        WARNING("DMAManagerPrepareBinding: binding request for attachment %llu "
                "on manager %u refused: %u",
                (unsigned long long)parameters->Attachment, manager->ID, status);
        goto error;
    }

    // A retry already holds a reference to the existing binding. Only a new
    // request creates a binding and its handle.
    if (registration->CreatesBinding) {
        status = __CreateBinding(
            manager,
            parameters,
            registration->Attachment,
            generation,
            &binding
        );
        if (status != OS_EOK) {
            goto error;
        }

        status = __CreateBindingHandle(
            manager,
            driverSecurity,
            binding,
            &bindingHandle
        );
        if (status != OS_EOK) {
            goto error;
        }
        registration->Binding = binding;
    }

    // Both new bindings and retries reach here with a handle reference.
    // Publish still checks if the handle closed later. Returning the handle ID
    // and generation alone does not enable DMA yet.
    *bindingOut = (OSDmaBindingResult_t) {
        .Handle = registration->Binding->ID,
        .Generation = registration->Binding->Generation
    };
    *registrationOut = registration;
    return OS_EOK;

error:
    MutexUnlock(&manager->UpdateLock);
    if (bindingHandle != UUID_INVALID) {
        // The handle was created, but a later step failed. The driver might
        // already have closed and freed the binding, so clean up via the saved
        // handle ID without touching the binding pointer.
        __DestroyUnpublishedHandle(driverSecurity, bindingHandle);
    } else if (binding != NULL) {
        __FreeUnpublishedBinding(binding);
    }

    __ReleaseMemoryHandleReference(driverSecurity->GlobalHandleID, __func__);
    kfree(registration);
    __BlockFailedUnitGroupsForBinding(manager);
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
        binding->Group->Access.State != DMARecoveryLive) {
        status = OS_ECANCELLED;
        goto unlock;
    }

    // Check domain membership and owner security ID first, then the health
    // of the unit, all under the manager and group locks. The failure pass takes
    // the same locks, so it always sees a binding that was just made live.
    status = __CheckDomainBindingLocked(prepared->Attachment, binding->OwnerSecurityID);
    if (status != OS_EOK) {
        goto unlock;
    }

    if (prepared->CreatesBinding) {
        assert(binding->State == DMABindingPreparing);
        bindingState = &prepared->Attachment->Binding;
        assert(bindingState->Current == NULL);

        binding->Attachment = prepared->Attachment;
        bindingState->Current = binding;
        bindingState->OwnerSecurityID = binding->OwnerSecurityID;
        bindingState->RequestId = binding->RequestId;
        bindingState->Generation = binding->Generation;

        // The first binding in a domain fixes the owner security ID for the rest.
        if (prepared->Attachment->Domain != NULL) {
            prepared->Attachment->Domain->OwnerSecurityID = binding->OwnerSecurityID;
        }

        __SetBindingState(binding, DMABindingLive);
    }

unlock:
    MutexUnlock(&binding->Group->Lock);
    MutexUnlock(&manager->Lock);
    if (status != OS_EOK) {
        WARNING("DMAManagerPublishBinding: manager %u, binding %u publication refused: %u",
                manager->ID, binding->ID, status);
        __BlockFailedUnitGroupsForBinding(manager);
        return status;
    }

    TRACE("DMAManagerPublishBinding: binding %u (generation %llu) published on manager %u",
          binding->ID, (unsigned long long)binding->Generation, manager->ID);
    __FinishBindingRegistration(registration, false);
    __BlockFailedUnitGroupsForBinding(manager);
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
        TRACE("DMAManagerCancelBinding: binding %u on manager %u cancelled before publication",
              prepared->Binding->ID, prepared->Manager->ID);
    }
    __FinishBindingRegistration(registration, true);
}

// Requires group->Lock or group->Access.Lock, and a binding reference. The
// binding and its recovery group must be live. For a domain member, the unit must not
// have failed. Returns OS_ECANCELLED, or the unit failure from
// DMAIOMMUUnitCheck. Sharing this helper ensures context publish and
// operation checks follow the exact same rules.
oserr_t
DMABindingCheckLiveLocked(
    _In_ DMABinding_t* binding)
{
    if (binding->State != DMABindingLive || binding->Group->Access.State != DMARecoveryLive) {
        return OS_ECANCELLED;
    }

    if (binding->Attachment->Domain != NULL) {
        // A domain always has a referenced unit. Its failure latch is safe to
        // read under Access.Lock and does not log or take the unit mutex.
        return DMAIOMMUUnitCheck(binding->Attachment->Domain->Unit);
    }
    return OS_EOK;
}

// Requires group->Lock. Checks that the caller may open a context on the
// binding, and if so reserves the binding for it. Only one context can be
// attached to a binding, and the count of running operations must have room
// for this open. From the moment the context pointer is set, a cancel pass can
// reach the new SHM context, which is set up but has no address space yet. So
// this open is counted as a running operation before the lock is dropped, and
// stop cleanup waits for it while the address space is created or referenced.
// The address limit is the lower of the attachment limit and the one the
// caller asked for. Returns OS_EPERMISSIONS for a binding owned by another
// caller, OS_ECANCELLED for an old generation or a stopped binding, OS_EBUSY if
// a context is already attached, or OS_EOVERFLOW.
static oserr_t
__ReserveContextLocked(
    _In_  DMABinding_t*                     binding,
    _In_  const MSContextSecurity_t*        caller,
    _In_  const OSDmaBoundOpenParameters_t* parameters,
    _In_  SHMDeviceContext_t*               context,
    _Out_ DMABindingContextConfig_t*        configurationOut)
{
    oserr_t status;

    if (binding->OwnerSecurityID != (uint64_t)caller->GlobalHandleID) {
        return OS_EPERMISSIONS;
    }

    if (binding->Generation != parameters->Generation) {
        return OS_ECANCELLED;
    }

    status = DMABindingCheckLiveLocked(binding);
    if (status != OS_EOK) {
        return status;
    }

    if (binding->Context.Pointer != NULL) {
        return OS_EBUSY;
    }

    configurationOut->CachePolicy = binding->Attachment->Configuration.CachePolicy;
    configurationOut->AddressLimit = binding->Attachment->Configuration.AddressLimit;
    if (parameters->AddressLimit < configurationOut->AddressLimit) {
        configurationOut->AddressLimit = parameters->AddressLimit;
    }

    SpinlockAcquireIrq(&binding->Group->Access.Lock);
    status = DMARecoveryAddOperationLocked(&binding->Group->Access);
    if (status == OS_EOK) {
        binding->Context.Pointer = context;
        binding->Context.Phase = DMABindingContextPreparing;
    }
    SpinlockReleaseIrq(&binding->Group->Access.Lock);
    return status;
}

// Get the address space the binding's context will use. A device in a domain
// shares the domain's address space, so that one is referenced. Any other
// device gets a new direct address space built from its ranges. On success the
// caller holds one reference to configuration->AddressSpace.
static oserr_t
__AcquireAddressSpace(
    _In_    DMABinding_t*              binding,
    _InOut_ DMABindingContextConfig_t* configuration)
{
    oserr_t status;

    if (binding->Attachment->Domain != NULL) {
        // The domain cannot go away while its attachments exist, and this
        // binding keeps its attachment alive. So the domain's address space is
        // safe to reference here. All members of the domain share this one
        // space and its IOVA allocator.
        configuration->AddressSpace = binding->Attachment->Domain->AddressSpace;
        status = DMAAddressSpaceAcquire(configuration->AddressSpace);
        if (status != OS_EOK) {
            configuration->AddressSpace = NULL;
        }
        return status;
    }

    return DMAAddressSpaceCreateDirect(
        binding->Attachment->Configuration.Ranges,
        binding->Attachment->Configuration.RangeCount,
        &configuration->AddressSpace
    );
}

oserr_t
DMABindingAttachContext(
    _In_  uuid_t                            handle,
    _In_  const OSDmaBoundOpenParameters_t* parameters,
    _In_  SHMDeviceContext_t*               context,
    _Out_ DMABinding_t**                    bindingOut,
    _Out_ DMABindingContextConfig_t*        configurationOut)
{
    MSContextSecurity_t*      caller = MemorySpaceGetSecurityContext();
    DMABinding_t*             binding;
    DMABindingContextConfig_t configuration = { 0 };
    oserr_t                   status;

    if (parameters->Generation == 0) {
        WARNING("DMABindingAttachContext: context open on binding %u rejected, generation must be non-zero", handle);
        return OS_EINVALPARAMS;
    }

    if (caller == NULL) {
        WARNING("DMABindingAttachContext: context open on binding %u denied, the caller has no security context", handle);
        return OS_EPERMISSIONS;
    }

    status = AcquireHandleReferenceInContext(
        caller,
        handle,
        HandleTypeDMABinding,
        (void**)&binding
    );
    if (status != OS_EOK) {
        WARNING("DMABindingAttachContext: context open failed, binding reference %u unavailable: %u", handle, status);
        return status;
    }

    MutexLock(&binding->Group->Lock);
    status = __ReserveContextLocked(binding, caller, parameters, context, &configuration);
    MutexUnlock(&binding->Group->Lock);
    if (status != OS_EOK) {
        if (status == OS_EOVERFLOW) {
            ERROR("DMABindingAttachContext: binding %u cannot open a context, the operation count is exhausted",
                  binding->ID);
        } else {
            WARNING("DMABindingAttachContext: context open on binding %u refused: %u", binding->ID, status);
        }
        __BlockFailedUnitGroupsForBinding(binding->Manager);
        __ReleaseMemoryHandleReference(binding->ID, __func__);
        return status;
    }

    status = __AcquireAddressSpace(binding, &configuration);
    if (status != OS_EOK) {
        ERROR("DMABindingAttachContext: address-space setup for binding %u failed: %u", binding->ID, status);
        goto cancel;
    }

    // Check once more that the binding and the unit are still healthy before
    // handing the address space to the caller.
    status = DMABindingCheckOperation(binding);
    if (status != OS_EOK) {
        WARNING("DMABindingAttachContext: context open on binding %u cancelled, binding is no longer usable: %u",
                binding->ID, status);
        DMAAddressSpaceRelease(&configuration.AddressSpace);
        goto cancel;
    }

    // Only fill in the outputs after setup succeeded. The context takes over
    // the address space reference. The caller finishes the open later, by
    // publishing the context or cancelling it.
    *configurationOut = configuration;
    *bindingOut = binding;
    return OS_EOK;

cancel:
    // Detaching also removes the running operation counted above.
    __BlockFailedUnitGroupsForBinding(binding->Manager);
    DMABindingDetachContext(&binding, context);
    return status;
}

oserr_t
DMABindingPublishContext(
    _In_ DMABinding_t* binding)
{
    oserr_t status;

    MutexLock(&binding->Group->Lock);
    SpinlockAcquireIrq(&binding->Group->Access.Lock);
    assert(binding->Context.Phase == DMABindingContextPreparing);

    status = DMABindingCheckLiveLocked(binding);
    if (status == OS_EOK) {
        binding->Context.Phase = DMABindingContextLive;
        DMARecoveryRemoveOperationLocked(&binding->Group->Access);
    }
    SpinlockReleaseIrq(&binding->Group->Access.Lock);
    MutexUnlock(&binding->Group->Lock);

    if (status != OS_EOK) {
        WARNING("DMABindingPublishContext: context publish on binding %u refused: %u", binding->ID, status);
    }

    __BlockFailedUnitGroupsForBinding(binding->Manager);
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

    SpinlockAcquireIrq(&current->Group->Access.Lock);
    current->Context.Pointer = NULL;
    current->Context.Phase = DMABindingContextNone;
    SpinlockReleaseIrq(&current->Group->Access.Lock);
    MutexUnlock(&current->Group->Lock);

    if (preparing) {
        // Cancelling an open before it finishes publishing may be the last
        // active operation in the group. Use DMABindingEndOperation so any
        // service waiting to retry stop gets notified.
        DMABindingEndOperation(current);
    }
    __ReleaseMemoryHandleReference(current->ID, __func__);
}

// Hold group->Access.Lock. Checks whether a binding can start a DMA operation.
// A release operation frees mappings, so it is still allowed after the group
// stops. It waits with OS_EBUSY only while the group is Blocked (stop not yet
// confirmed) or while stop cleanup is already freeing those same mappings.
// Returns OS_ECANCELLED if the binding, context, or manager closed, OS_EBUSY
// when a release must wait, or the unit error from DMABindingCheckLiveLocked.
static oserr_t
__CheckOperationAllowedLocked(
    _In_ DMABinding_t* binding,
    _In_ bool          release)
{
    DMARecoveryGroup_t* group = binding->Group;
    oserr_t             status = DMABindingCheckLiveLocked(binding);

    if (release && group->Access.State != DMARecoveryLive) {
        status = group->Access.State == DMARecoveryBlocked ||
            group->Access.Cleaning ? OS_EBUSY : OS_EOK;
    }

    if (group->Access.ManagerClosed) {
        status = OS_ECANCELLED;
    }

    if (status == OS_EOK && binding->Context.Phase != DMABindingContextLive) {
        status = OS_ECANCELLED;
    }
    return status;
}

oserr_t
DMABindingBeginOperation(
    _In_  DMABinding_t* binding,
    _In_  bool          release,
    _Out_ bool*         stoppedOut)
{
    DMARecoveryGroup_t* group = binding->Group;
    oserr_t             status;

    // Propagate any new IOMMU unit failures before taking Access.Lock because
    // blocking failed-unit groups takes mutexes and walks domains.
    status = DMARecoveryCheckFailures(binding->Manager);
    if (status != OS_EOK) {
        ERROR("DMABindingBeginOperation: failure propagation for binding %u failed: %u", binding->ID, status);
    }

    SpinlockAcquireIrq(&group->Access.Lock);
    status = __CheckOperationAllowedLocked(binding, release);

    // Count the operation, so stop cleanup waits for it to end. For a release
    // on a stopped group, stoppedOut tells the caller the device is already
    // known to be stopped.
    if (status == OS_EOK) {
        status = DMARecoveryAddOperationLocked(&group->Access);
    }
    if (status == OS_EOK) {
        *stoppedOut = release && (group->Access.State == DMARecoveryStopConfirmed ||
                                 group->Access.State == DMARecoveryStopCleaned);
    }
    SpinlockReleaseIrq(&group->Access.Lock);

    if (status == OS_EOVERFLOW) {
        ERROR("DMABindingBeginOperation: binding %u cannot start an operation, the operation count is exhausted",
              binding->ID);
    }
    return status;
}

oserr_t
DMABindingCheckOperation(
    _In_ DMABinding_t* binding)
{
    oserr_t status;

    // Check for recent IOMMU failures before reading the fast spinlock state.
    status = DMARecoveryCheckFailures(binding->Manager);
    if (status != OS_EOK) {
        ERROR("DMABindingCheckOperation: failure propagation for binding %u failed: %u", binding->ID, status);
    }

    SpinlockAcquireIrq(&binding->Group->Access.Lock);
    status = DMABindingCheckLiveLocked(binding);
    SpinlockReleaseIrq(&binding->Group->Access.Lock);
    return status;
}

// Blocks the binding's recovery group and cancels all open SHM contexts in it.
void
DMABindingRevoke(
    _In_ DMABinding_t* binding)
{
    MutexLock(&binding->Manager->Lock);
    DMARecoveryBlockLocked(binding->Manager, binding->Group);
    MutexUnlock(&binding->Manager->Lock);
    DMARecoveryCancel(binding->Group);
    __BlockFailedUnitGroupsForBinding(binding->Manager);
}
