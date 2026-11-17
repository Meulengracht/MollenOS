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
#include <shm/device.h>

#include "attachment_private.h"

// Hold manager->Lock and group->Lock. Queues the same event for every
// attachment in the group, using the group's current stop round (Round).
static void
__QueueGroupEventLocked(
    _In_ DMAManager_t*       manager,
    _In_ DMARecoveryGroup_t* group,
    _In_ uint32_t            reasons)
{
    foreach (node, &manager->Registry.Attachments) {
        DMAAttachment_t* attachment = node->value;

        if (attachment->RecoveryGroup == group) {
            DMAAttachmentQueueEventLocked(
                manager,
                attachment,
                attachment->Binding.Generation,
                group->Stop.Round,
                reasons
            );
        }
    }
}

void
DMARecoveryBlockLocked(
    _In_ DMAManager_t*       manager,
    _In_ DMARecoveryGroup_t* group)
{
    MutexLock(&group->Lock);
    if (manager->State == DMAManagerClosed) {
        // Record that the manager closed even if recovery is already under way.
        // A stop pass that is allowed to run can still finish, but a driver
        // cannot retry cleanup later.
        group->Access.ManagerClosed = true;
    }

    if (group->Access.State != DMARecoveryLive) {
        MutexUnlock(&group->Lock);
        return;
    }

    // The stop round (Round) was reserved before operations could start, so
    // blocking never needs memory or new IDs, and a second close cannot undo a
    // confirmed stop.
    group->Access.State = DMARecoveryBlocked;
    group->Stop.CancellationError = OS_EBUSY;
    TRACE("DMARecoveryBlockLocked: recovery group %llu blocked, stop round %llu",
          (unsigned long long)group->ID, (unsigned long long)group->Stop.Round);

    foreach (node, &manager->Registry.Attachments) {
        DMAAttachment_t* attachment = node->value;

        if (attachment->RecoveryGroup != group) {
            continue;
        }

        if (attachment->Binding.Current != NULL) {
            attachment->Binding.Current->State = DMABindingClosed;
        }

        DMAAttachmentQueueEventLocked(
            manager,
            attachment,
            attachment->Binding.Generation,
            group->Stop.Round,
            DMA_ATTACHMENT_STOP_REQUIRED
        );
    }
    MutexUnlock(&group->Lock);
}

// Hold manager->Lock and group->Lock. Taking a reference on each context keeps
// it alive after dropping the locks. If a context's reference count already hit
// zero, it is already detaching with no mappings left and must not be reused.
static oserr_t
__CollectContextsLocked(
    _In_  DMARecoveryGroup_t*  group,
    _Out_ SHMDeviceContext_t** contexts,
    _Out_ uint32_t*            countOut)
{
    oserr_t firstError = OS_EOK;

    *countOut = 0;

    foreach (node, &group->Manager->Registry.Attachments) {
        DMAAttachment_t* attachment = node->value;
        DMABinding_t*    binding = attachment->Binding.Current;
        oserr_t          status;

        if (attachment->RecoveryGroup != group || binding == NULL || binding->Context.Pointer == NULL) {
            continue;
        }
        status = SHMDeviceContextAcquire(binding->Context.Pointer);
        if (status == OS_EOK) {
            contexts[(*countOut)++] = binding->Context.Pointer;
        } else if (status != OS_ECANCELLED) {
            ERROR("__CollectContextsLocked: SHMDeviceContextAcquire group %llu, context %p failed: %u",
                  (unsigned long long)group->ID, binding->Context.Pointer, status);
            if (firstError == OS_EOK) {
                firstError = status;
            }
        }
    }
    return firstError;
}

void
DMARecoveryCancel(
    _In_ DMARecoveryGroup_t* group)
{
    DMAManager_t*       manager = group->Manager;
    SHMDeviceContext_t* contexts[DMA_MANAGER_MAX_ATTACHMENTS];
    uint32_t            count;
    oserr_t             firstError;
    bool                notifyManager;

    MutexLock(&manager->Lock);
    MutexLock(&group->Lock);
    if (group->Access.State != DMARecoveryBlocked) {
        MutexUnlock(&group->Lock);
        MutexUnlock(&manager->Lock);
        return;
    }

    if (group->Stop.Phase != DMARecoveryStopIdle) {
        // A cancel pass already in progress may have skipped a releasing mapping
        // or missed one linking in. Request another pass before returning to Idle.
        group->Stop.Phase = DMARecoveryStopCancelAgain;
        MutexUnlock(&group->Lock);
        MutexUnlock(&manager->Lock);
        return;
    }

    do {
        group->Stop.Phase = DMARecoveryStopCancelling;
        firstError = __CollectContextsLocked(group, contexts, &count);
        if (firstError != OS_EOK) {
            ERROR("DMARecoveryCancel: __CollectContextsLocked group %llu failed: %u",
                  (unsigned long long)group->ID, firstError);
        }
        MutexUnlock(&group->Lock);
        MutexUnlock(&manager->Lock);

        for (uint32_t i = 0; i < count; i++) {
            oserr_t status = SHMDeviceContextRevoke(contexts[i]);

            if (status != OS_EOK) {
                ERROR("DMARecoveryCancel: SHMDeviceContextRevoke group %llu, context %p failed: %u",
                      (unsigned long long)group->ID, contexts[i], status);
                if (firstError == OS_EOK) {
                    firstError = status;
                }
            }
            SHMDeviceContextRelease(&contexts[i]);
        }

        MutexLock(&manager->Lock);
        MutexLock(&group->Lock);
        group->Stop.CancellationError = firstError;
        // Keep cancellation active across retries. CompleteStop must wait for a pass
        // that saw every cancel request made before taking the lock.
    } while (group->Stop.Phase == DMARecoveryStopCancelAgain);

    group->Stop.Phase = DMARecoveryStopIdle;
    notifyManager = manager->State == DMAManagerLive;

    MutexUnlock(&group->Lock);
    MutexUnlock(&manager->Lock);
    if (notifyManager) {
        // If signaling watchers fails, the events stay queued for polling.
        DMAManagerNotifyEvents(manager, __func__);
    }
}

// Hold manager->Lock. A unit failed. Blocks the recovery group of every
// attached device on that unit and adds each group to groups, skipping groups
// that are already in the list. This covers all attachments and not only the
// ones in the domain that reported the failure. The unit may fail between the
// health checks of two domains in the caller's loop, so going through all
// attachments here avoids missing one.
static void
__BlockGroupsOfUnitLocked(
    _In_    DMAManager_t*        manager,
    _In_    DMAIOMMUUnit_t*      unit,
    _InOut_ DMARecoveryGroup_t** groups,
    _InOut_ uint32_t*            count)
{
    foreach (member, &manager->Registry.Attachments) {
        DMAAttachment_t*    attachment = member->value;
        DMARecoveryGroup_t* group;
        uint32_t            index;

        if (attachment->Domain == NULL || attachment->Domain->Unit != unit) {
            continue;
        }

        // Skip a group that is already in the list.
        group = attachment->RecoveryGroup;
        for (index = 0; index < *count && groups[index] != group; index++) {
        }
        if (index != *count) {
            continue;
        }

        assert(*count < DMA_MANAGER_MAX_ATTACHMENTS);
        DMARecoveryBlockLocked(manager, group);
        groups[(*count)++] = group;
    }
}

// Cancel the waiting work of a blocked group, then report whether the group is
// done. A blocked group is only done when no cancel pass is running and the
// last pass found no error. Otherwise the result says why it is not done.
static oserr_t
__CancelBlockedGroup(
    _In_ DMARecoveryGroup_t* group)
{
    oserr_t status = OS_EOK;

    DMARecoveryCancel(group);

    MutexLock(&group->Lock);
    if (group->Access.State == DMARecoveryBlocked) {
        status = group->Stop.Phase == DMARecoveryStopIdle ? group->Stop.CancellationError : OS_EBUSY;
    }
    MutexUnlock(&group->Lock);
    return status;
}

oserr_t
DMARecoveryBlockFailedUnits(
    _In_ DMAManager_t* manager)
{
    DMARecoveryGroup_t* groups[DMA_MANAGER_MAX_ATTACHMENTS];
    uint32_t            count = 0;
    oserr_t             firstError = OS_EOK;
    oserr_t             status;

    MutexLock(&manager->Lock);
    foreach (node, &manager->Domains) {
        DMATranslatedDomain_t* domain = node->value;

        // A Closing domain has no attachments. Its address space may be freed
        // while this lock is not held, so do not read the pointer, not even to
        // check the health of the unit.
        if (domain->State != DMATranslatedDomainReady) {
            continue;
        }

        status = DMAAddressSpaceCheck(domain->AddressSpace);
        if (status != OS_EOK) {
            __BlockGroupsOfUnitLocked(manager, domain->Unit, groups, &count);
        }
    }

    // Every affected group is already blocked and has its stop event queued, so
    // a failed reference below cannot leave a group half handled. The group
    // pointers are only safe while manager->Lock is held, so take a reference
    // on each group now. A group that cannot be referenced is dropped from the
    // list. Only referenced groups are used after the unlock.
    for (uint32_t i = 0; i < count; i++) {
        status = DMARecoveryGroupAcquire(groups[i]);
        if (status != OS_EOK) {
            ERROR("DMARecoveryBlockFailedUnits: group %llu reference failed: %u",
                  (unsigned long long)groups[i]->ID, status);
            groups[i] = NULL;
            if (firstError == OS_EOK) {
                firstError = status;
            }
        }
    }
    MutexUnlock(&manager->Lock);

    for (uint32_t i = 0; i < count; i++) {
        if (groups[i] == NULL) {
            continue;
        }

        status = __CancelBlockedGroup(groups[i]);
        if (status != OS_EOK) {
            ERROR("DMARecoveryBlockFailedUnits: group %llu cancellation incomplete: %u",
                  (unsigned long long)groups[i]->ID, status);
            if (firstError == OS_EOK) {
                firstError = status;
            }
        }
        DMARecoveryGroupRelease(&groups[i]);
    }

    if (count != 0) {
        // Notify even if every reference or cancellation failed. The stop events
        // are already queued, so polling still sees them, and the next event
        // read retries the cancellation.
        DMAManagerNotifyEvents(manager, __func__);
    }
    return firstError;
}

void
DMABindingEndOperation(
    _In_ DMABinding_t* binding)
{
    bool    retryCancellation;
    bool    notifyManager = false;
    bool    drained;
    oserr_t status;

    // A unit can fail while this operation runs, before any check notices.
    // Run the failure pass first, while the operation is still counted. Stop
    // cleanup keeps waiting for it, so cleanup cannot finish before the failure
    // reaches the recovery groups.
    status = DMARecoveryBlockFailedUnits(binding->Manager);
    if (status != OS_EOK) {
        ERROR("DMABindingEndOperation: failure propagation for manager %u, binding %u failed: %u",
              binding->Manager->ID, binding->ID, status);
    }

    MutexLock(&binding->Group->Lock);
    assert(binding->Group->Access.Operations != 0);
    binding->Group->Access.Operations--;
    retryCancellation = binding->Group->Access.State == DMARecoveryBlocked &&
        (binding->Group->Stop.Phase != DMARecoveryStopIdle ||
         binding->Group->Stop.CancellationError != OS_EOK);
    drained = binding->Group->Access.State == DMARecoveryBlocked && binding->Group->Access.Operations == 0;
    MutexUnlock(&binding->Group->Lock);

    // A release operation that overlapped the first cancel pass just finished.
    // Run another cancel pass outside the locks so any waiting contexts get
    // cancelled even if the manager has closed.
    if (retryCancellation) {
        DMARecoveryCancel(binding->Group);
    }

    if (!drained) {
        return;
    }

    MutexLock(&binding->Manager->Lock);
    MutexLock(&binding->Group->Lock);
    if (binding->Manager->State == DMAManagerLive && binding->Group->Access.State == DMARecoveryBlocked &&
        binding->Group->Access.Operations == 0) {
        __QueueGroupEventLocked(
            binding->Manager,
            binding->Group,
            DMA_ATTACHMENT_STOP_REQUIRED
        );
        notifyManager = true;
    }
    MutexUnlock(&binding->Group->Lock);
    MutexUnlock(&binding->Manager->Lock);

    if (notifyManager) {
        DMAManagerNotifyEvents(binding->Manager, __func__);
    }
}

oserr_t
DMAManagerPrepareStop(
    _In_  DMAManager_t*       manager,
    _In_  uint64_t            attachmentID,
    _Out_ OSDmaStopTicket_t* ticketOut)
{
    DMARecoveryGroup_t* group = NULL;
    DMAAttachment_t*    attachment = NULL;
    oserr_t             status;

    if (attachmentID == 0) {
        WARNING("DMAManagerPrepareStop: stop request for manager %u rejected, attachment ID must be non-zero", manager->ID);
        return OS_EINVALPARAMS;
    }

    MutexLock(&manager->Lock);
    if (manager->State != DMAManagerLive) {
        WARNING("DMAManagerPrepareStop: stop request for attachment %llu refused, manager %u is not live",
                (unsigned long long)attachmentID, manager->ID);
        status = OS_ECANCELLED;
        goto unlock;
    }

    foreach (node, &manager->Registry.Attachments) {
        DMAAttachment_t* candidate = node->value;

        if (candidate->ID == attachmentID) {
            attachment = candidate;
            break;
        }
    }

    if (attachment == NULL) {
        WARNING("DMAManagerPrepareStop: stop request for attachment %llu on manager %u failed, no such attachment",
                (unsigned long long)attachmentID, manager->ID);
        status = OS_ENOENT;
        goto unlock;
    }

    status = DMARecoveryGroupAcquire(attachment->RecoveryGroup);
    if (status != OS_EOK) {
        ERROR("DMAManagerPrepareStop: group %llu reference for attachment %llu failed: %u",
              (unsigned long long)attachment->RecoveryGroup->ID,
              (unsigned long long)attachmentID, status);
        goto unlock;
    }
    group = attachment->RecoveryGroup;

    DMARecoveryBlockLocked(manager, group);
    MutexLock(&group->Lock);
    *ticketOut = (OSDmaStopTicket_t) { group->ID, group->Stop.Round };
    MutexUnlock(&group->Lock);

unlock:
    MutexUnlock(&manager->Lock);
    if (group != NULL) {
        DMARecoveryCancel(group);
        DMARecoveryGroupRelease(&group);
    }
    return status;
}

// Look up the group named by a stop ticket and take a reference on it. The
// group is looked up in the manager's current list, so an old ticket from
// before a group rebuild cannot match a detached group. Returns OS_ECANCELLED
// if the manager is not live, OS_ENOENT if no group has this ID, or the error
// from taking the reference.
static oserr_t
__AcquireGroupByID(
    _In_  DMAManager_t*        manager,
    _In_  uint64_t             groupID,
    _Out_ DMARecoveryGroup_t** groupOut)
{
    DMARecoveryGroup_t* group = NULL;
    oserr_t             status;

    MutexLock(&manager->Lock);
    if (manager->State != DMAManagerLive) {
        WARNING("__AcquireGroupByID: stop ticket for group %llu refused, manager %u is not live",
                (unsigned long long)groupID, manager->ID);
        status = OS_ECANCELLED;
        goto unlock;
    }

    foreach (node, &manager->Registry.Groups) {
        DMARecoveryGroup_t* candidate = node->value;

        if (candidate->ID == groupID) {
            group = candidate;
            break;
        }
    }

    if (group == NULL) {
        WARNING("__AcquireGroupByID: stop ticket for group %llu on manager %u refused, no such group",
                (unsigned long long)groupID, manager->ID);
        status = OS_ENOENT;
        goto unlock;
    }

    status = DMARecoveryGroupAcquire(group);
    if (status != OS_EOK) {
        ERROR("__AcquireGroupByID: group %llu reference for a stop ticket failed: %u",
              (unsigned long long)groupID, status);
        goto unlock;
    }
    *groupOut = group;

unlock:
    MutexUnlock(&manager->Lock);
    return status;
}

// Hold manager->Lock and group->Lock. Decides whether cleanup can start for
// the stop round named by the ticket. The checks run again here because the
// locks were dropped while the cancel pass ran.
//
// Returns OS_EOK and sets *cleanupOut when the group moved to Confirmed and
// Cleaning. Then contexts holds one reference for each context to clean up,
// and the caller must release them. Returns OS_EOK without setting
// *cleanupOut if the group is already cleaned. Returns OS_ECANCELLED for an
// outdated ticket or a closed manager, OS_EBUSY while operations or a cancel
// pass are still running, or the last cancellation error.
static oserr_t
__BeginStopCleanupLocked(
    _In_  DMAManager_t*             manager,
    _In_  DMARecoveryGroup_t*       group,
    _In_  const OSDmaStopTicket_t*  ticket,
    _Out_ SHMDeviceContext_t**      contexts,
    _Out_ uint32_t*                 countOut,
    _Out_ bool*                     cleanupOut)
{
    oserr_t status;

    if (manager->State != DMAManagerLive || group->Stop.Round != ticket->Epoch ||
        group->Access.State == DMARecoveryLive) {
        return OS_ECANCELLED;
    }

    if (group->Access.State == DMARecoveryStopCleaned) {
        return OS_EOK;
    }

    if (group->Access.Operations != 0 || group->Stop.Phase != DMARecoveryStopIdle) {
        return OS_EBUSY;
    }

    if (group->Stop.CancellationError != OS_EOK) {
        return group->Stop.CancellationError;
    }

    status = __CollectContextsLocked(group, contexts, countOut);
    if (status != OS_EOK) {
        ERROR("__BeginStopCleanupLocked: __CollectContextsLocked group %llu, epoch %llu failed: %u",
              (unsigned long long)ticket->RecoveryGroup, (unsigned long long)ticket->Epoch, status);
        return status;
    }

    group->Access.State = DMARecoveryStopConfirmed;
    group->Stop.Phase = DMARecoveryStopCleaning;
    *cleanupOut = true;
    return OS_EOK;
}

// Release the context references taken by __BeginStopCleanupLocked without
// touching the contexts' mappings.
static void
__ReleaseContexts(
    _InOut_ SHMDeviceContext_t** contexts,
    _In_    uint32_t             count)
{
    for (uint32_t i = 0; i < count; i++) {
        SHMDeviceContextRelease(&contexts[i]);
    }
}

// Tell each context to free its mappings, then release the references taken by
// __BeginStopCleanupLocked. Every context is attempted, even after a failure.
// Returns the first cleanup error, or OS_EOK.
static oserr_t
__CleanContexts(
    _In_    const OSDmaStopTicket_t* ticket,
    _InOut_ SHMDeviceContext_t**     contexts,
    _In_    uint32_t                 count)
{
    oserr_t firstError = OS_EOK;

    for (uint32_t i = 0; i < count; i++) {
        oserr_t status = SHMDeviceContextCompleteStop(contexts[i]);

        if (status != OS_EOK) {
            ERROR("__CleanContexts: SHMDeviceContextCompleteStop group %llu, epoch %llu, context %p failed: %u",
                  (unsigned long long)ticket->RecoveryGroup, (unsigned long long)ticket->Epoch,
                  contexts[i], status);
            if (firstError == OS_EOK) {
                firstError = status;
            }
        }
        SHMDeviceContextRelease(&contexts[i]);
    }
    return firstError;
}

// Record the result of a cleanup pass. On success the group becomes Cleaned. On
// failure the group stays Confirmed, so the service can retry with the same
// ticket, and a cleanup failed event is queued for every attachment.
static void
__FinishStopCleanup(
    _In_ DMAManager_t*       manager,
    _In_ DMARecoveryGroup_t* group,
    _In_ oserr_t             cleanupStatus)
{
    bool notifyManager = false;

    MutexLock(&manager->Lock);
    MutexLock(&group->Lock);
    group->Stop.Phase = DMARecoveryStopIdle;
    if (cleanupStatus == OS_EOK) {
        group->Access.State = DMARecoveryStopCleaned;
        TRACE("__FinishStopCleanup: recovery group %llu cleaned, stop round %llu",
              (unsigned long long)group->ID, (unsigned long long)group->Stop.Round);
    } else {
        WARNING("__FinishStopCleanup: recovery group %llu cleanup failed: %u, retry with the same ticket",
                (unsigned long long)group->ID, cleanupStatus);
        __QueueGroupEventLocked(manager, group, DMA_ATTACHMENT_CLEANUP_FAILED);
        notifyManager = manager->State == DMAManagerLive;
    }
    MutexUnlock(&group->Lock);
    MutexUnlock(&manager->Lock);

    if (notifyManager) {
        DMAManagerNotifyEvents(manager, __func__);
    }
}

oserr_t
DMAManagerCompleteStop(
    _In_ DMAManager_t*             manager,
    _In_ const OSDmaStopTicket_t* ticket)
{
    DMARecoveryGroup_t* group = NULL;
    SHMDeviceContext_t* contexts[DMA_MANAGER_MAX_ATTACHMENTS];
    uint32_t            count = 0;
    oserr_t             status;
    oserr_t             propagationStatus;
    bool                outdated;
    bool                cleanup = false;

    if (ticket->RecoveryGroup == 0 || ticket->RecoveryGroup == UINT64_MAX ||
        ticket->Epoch == 0 || ticket->Epoch == UINT64_MAX) {
        WARNING("DMAManagerCompleteStop: stop completion for manager %u rejected, bad ticket group %llu, epoch %llu",
                manager->ID, (unsigned long long)ticket->RecoveryGroup,
                (unsigned long long)ticket->Epoch);
        return OS_EINVALPARAMS;
    }

    status = __AcquireGroupByID(manager, ticket->RecoveryGroup, &group);
    if (status != OS_EOK) {
        return status;
    }

    // Check the ticket before cancelling so an outdated ticket does nothing.
    // A blocked group keeps the same stop round (Round), and we recheck after relocking.
    MutexLock(&group->Lock);
    outdated = group->Stop.Round != ticket->Epoch || group->Access.State == DMARecoveryLive;
    MutexUnlock(&group->Lock);
    if (outdated) {
        WARNING("DMAManagerCompleteStop: stop completion for group %llu refused, epoch %llu is outdated",
                (unsigned long long)ticket->RecoveryGroup, (unsigned long long)ticket->Epoch);
        status = OS_ECANCELLED;
        goto release;
    }

    DMARecoveryCancel(group);

    MutexLock(&manager->Lock);
    MutexLock(&group->Lock);
    status = __BeginStopCleanupLocked(
        manager,
        group,
        ticket,
        contexts,
        &count,
        &cleanup
    );
    MutexUnlock(&group->Lock);
    MutexUnlock(&manager->Lock);
    if (status != OS_EOK) {
        // Contexts collected before the failure still hold a reference.
        __ReleaseContexts(contexts, count);
        WARNING("DMAManagerCompleteStop: stop completion for group %llu, epoch %llu not ready: %u",
                (unsigned long long)ticket->RecoveryGroup, (unsigned long long)ticket->Epoch,
                status);
        goto release;
    }

    if (!cleanup) {
        // The group was already cleaned by an earlier call, nothing to do.
        goto release;
    }

    // The contexts are cleaned outside the locks, because freeing their
    // mappings can wait for the hardware. The first error is kept.
    status = __CleanContexts(ticket, contexts, count);
    __FinishStopCleanup(manager, group, status);

release:
    DMARecoveryGroupRelease(&group);

    // Stop cleanup runs without a counted operation, so nothing else would run
    // the failure pass for it. A device removal or a cleanup callback may have
    // failed a unit that other, still live, recovery groups depend on. Run the
    // failure pass here to block them.
    propagationStatus = DMARecoveryBlockFailedUnits(manager);
    if (propagationStatus != OS_EOK) {
        ERROR("DMAManagerCompleteStop: failure propagation for manager %u failed: %u",
              manager->ID, propagationStatus);
    }
    return status;
}
