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

void
DMARecoveryBlockLocked(
    _In_ DMAManager_t*       manager,
    _In_ DMARecoveryGroup_t* group)
{
    MutexLock(&group->Lock);
    if (manager->State == DMAManagerClosed) {
        // Record that the manager closed even if recovery is already under way.
        // An authorized stop pass can still finish, but a driver cannot retry
        // cleanup later.
        group->Gate.ManagerClosed = true;
    }

    if (group->Gate.State != DMARecoveryLive) {
        MutexUnlock(&group->Lock);
        return;
    }

    // The stop round (Epoch) was reserved before operations could start, so
    // blocking never needs memory or new IDs, and a second close cannot undo a
    // confirmed stop.
    group->Gate.State = DMARecoveryBlocked;
    group->Stop.CancellationError = OS_EBUSY;

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
            group->Stop.Epoch,
            DMA_ATTACHMENT_STOP_REQUIRED
        );
    }
    MutexUnlock(&group->Lock);
}

// Hold manager->Lock and group->Lock. Taking a reference on each context keeps
// it alive after dropping the locks. If a context's reference count already hit
// zero, it is already detaching with no mappings left and must not be reused.
static oserr_t
__SnapshotContextsLocked(
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
            ERROR("__SnapshotContextsLocked: SHMDeviceContextAcquire group %llu, context %p failed: %u",
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
    if (group->Gate.State != DMARecoveryBlocked) {
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
        firstError = __SnapshotContextsLocked(group, contexts, &count);
        if (firstError != OS_EOK) {
            ERROR("DMARecoveryCancel: __SnapshotContextsLocked group %llu failed: %u",
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

void
DMABindingEndOperation(
    _In_ DMABinding_t* binding)
{
    bool retryCancellation;
    bool notifyManager = false;
    bool drained;

    MutexLock(&binding->Group->Lock);
    assert(binding->Group->Gate.Operations != 0);
    binding->Group->Gate.Operations--;
    retryCancellation = binding->Group->Gate.State == DMARecoveryBlocked &&
        (binding->Group->Stop.Phase != DMARecoveryStopIdle ||
         binding->Group->Stop.CancellationError != OS_EOK);
    drained = binding->Group->Gate.State == DMARecoveryBlocked && binding->Group->Gate.Operations == 0;
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
    if (binding->Manager->State == DMAManagerLive && binding->Group->Gate.State == DMARecoveryBlocked &&
        binding->Group->Gate.Operations == 0) {
        foreach (node, &binding->Manager->Registry.Attachments) {
            DMAAttachment_t* attachment = node->value;

            if (attachment->RecoveryGroup == binding->Group) {
                DMAAttachmentQueueEventLocked(
                    binding->Manager, attachment, attachment->Binding.Generation,
                    binding->Group->Stop.Epoch, DMA_ATTACHMENT_STOP_REQUIRED
                );
            }
        }
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
    oserr_t             status = OS_ENOENT;

    if (attachmentID == 0) {
        return OS_EINVALPARAMS;
    }
    MutexLock(&manager->Lock);
    if (manager->State != DMAManagerLive) {
        status = OS_ECANCELLED;
        goto unlock;
    }
    foreach (node, &manager->Registry.Attachments) {
        DMAAttachment_t* attachment = node->value;

        if (attachment->ID == attachmentID) {
            status = DMARecoveryGroupAcquire(attachment->RecoveryGroup);
            if (status == OS_EOK) {
                group = attachment->RecoveryGroup;
            }
            break;
        }
    }

    if (status == OS_EOK) {
        DMARecoveryBlockLocked(manager, group);
        MutexLock(&group->Lock);
        *ticketOut = (OSDmaStopTicket_t) { group->ID, group->Stop.Epoch };
        MutexUnlock(&group->Lock);
    }

unlock:
    MutexUnlock(&manager->Lock);
    if (group != NULL) {
        DMARecoveryCancel(group);
        DMARecoveryGroupRelease(&group);
    }
    return status;
}

oserr_t
DMAManagerCompleteStop(
    _In_ DMAManager_t*             manager,
    _In_ const OSDmaStopTicket_t* ticket)
{
    DMARecoveryGroup_t* group = NULL;
    SHMDeviceContext_t* contexts[DMA_MANAGER_MAX_ATTACHMENTS];
    uint32_t            count = 0;
    oserr_t             status = OS_ENOENT;
    bool                cleanup = false;
    bool                notifyManager = false;

    if (ticket->RecoveryGroup == 0 || ticket->RecoveryGroup == UINT64_MAX ||
        ticket->Epoch == 0 || ticket->Epoch == UINT64_MAX) {
        return OS_EINVALPARAMS;
    }

    // Look up the group in the manager's current list so an old ticket from
    // before a group rebuild cannot match a detached group.
    MutexLock(&manager->Lock);
    if (manager->State != DMAManagerLive) {
        status = OS_ECANCELLED;
    } else {
        foreach (node, &manager->Registry.Groups) {
            DMARecoveryGroup_t* candidate = node->value;

            if (candidate->ID == ticket->RecoveryGroup) {
                status = DMARecoveryGroupAcquire(candidate);
                if (status == OS_EOK) {
                    group = candidate;
                }
                break;
            }
        }
    }
    MutexUnlock(&manager->Lock);
    if (group == NULL) {
        return status;
    }

    // Check the ticket before cancelling so an outdated ticket does nothing.
    // A blocked group keeps the same stop round (Epoch), and we recheck after relocking.
    MutexLock(&group->Lock);
    status = group->Stop.Epoch != ticket->Epoch || group->Gate.State == DMARecoveryLive
        ? OS_ECANCELLED : OS_EOK;
    MutexUnlock(&group->Lock);
    if (status != OS_EOK) {
        goto release;
    }
    DMARecoveryCancel(group);

    MutexLock(&manager->Lock);
    MutexLock(&group->Lock);
    if (manager->State != DMAManagerLive || group->Stop.Epoch != ticket->Epoch ||
        group->Gate.State == DMARecoveryLive) {
        status = OS_ECANCELLED;
    } else if (group->Gate.State == DMARecoveryCleaned) {
        status = OS_EOK;
    } else if (group->Gate.Operations != 0 || group->Stop.Phase != DMARecoveryStopIdle) {
        status = OS_EBUSY;
    } else if (group->Stop.CancellationError != OS_EOK) {
        status = group->Stop.CancellationError;
    } else {
        status = __SnapshotContextsLocked(group, contexts, &count);
        if (status != OS_EOK) {
            ERROR("DMAManagerCompleteStop: __SnapshotContextsLocked group %llu, epoch %llu failed: %u",
                  (unsigned long long)ticket->RecoveryGroup, (unsigned long long)ticket->Epoch, status);
        } else {
            group->Gate.State = DMARecoveryConfirmed;
            group->Stop.Phase = DMARecoveryStopCleaning;
            cleanup = true;
        }
    }
    MutexUnlock(&group->Lock);
    MutexUnlock(&manager->Lock);

    for (uint32_t i = 0; i < count; i++) {
        if (cleanup) {
            oserr_t mappingStatus = SHMDeviceContextCompleteStop(contexts[i]);

            if (mappingStatus != OS_EOK) {
                ERROR("DMAManagerCompleteStop: SHMDeviceContextCompleteStop group %llu, epoch %llu, context %p failed: %u",
                      (unsigned long long)ticket->RecoveryGroup, (unsigned long long)ticket->Epoch,
                      contexts[i], mappingStatus);
                if (status == OS_EOK) {
                    status = mappingStatus;
                }
            }
        }
        SHMDeviceContextRelease(&contexts[i]);
    }
    if (!cleanup) {
        goto release;
    }

    MutexLock(&manager->Lock);
    MutexLock(&group->Lock);
    group->Stop.Phase = DMARecoveryStopIdle;
    if (status == OS_EOK) {
        group->Gate.State = DMARecoveryCleaned;
    } else {
        foreach (node, &manager->Registry.Attachments) {
            DMAAttachment_t* attachment = node->value;

            if (attachment->RecoveryGroup == group) {
                DMAAttachmentQueueEventLocked(
                    manager, attachment, attachment->Binding.Generation,
                    group->Stop.Epoch, DMA_ATTACHMENT_CLEANUP_FAILED
                );
            }
        }
        notifyManager = manager->State == DMAManagerLive;
    }
    MutexUnlock(&group->Lock);
    MutexUnlock(&manager->Lock);
    if (notifyManager) {
        DMAManagerNotifyEvents(manager, __func__);
    }
release:
    DMARecoveryGroupRelease(&group);
    return status;
}
