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
#include <handles/set.h>
#include <ioset.h>

#include "attachment_private.h"

void
DMAAttachmentQueueEventLocked(
    _In_ DMAManager_t*    manager,
    _In_ DMAAttachment_t* attachment,
    _In_ uint64_t         generation,
    _In_ uint64_t         stopEpoch,
    _In_ uint32_t         reasons)
{
    DMAAttachmentEventState_t* eventState = &attachment->Event;

    assert(reasons != 0);
    assert((reasons & ~(DMA_ATTACHMENT_IDLE | DMA_ATTACHMENT_STOP_REQUIRED |
                       DMA_ATTACHMENT_CLEANUP_FAILED)) == 0);

    if (manager->State != DMAManagerLive) {
        return;
    }

    if (eventState->Queued) {
        // Pending events must be read and acknowledged before reassignment
        // or the start of another recovery epoch.
        assert(eventState->Value.Generation == generation &&
               eventState->Value.StopEpoch == stopEpoch);
        eventState->Value.Reasons |= reasons;
    } else {
        eventState->Value = (OSDmaAttachmentEvent_t) {
            .Attachment = attachment->ID,
            .Generation = generation,
            .StopEpoch = stopEpoch,
            .Reasons = reasons
        };
        eventState->Queued = true;
        list_append(&manager->PendingEvents, &eventState->Link);
    }

    // Stop the sequence at UINT64_MAX so an old snapshot cannot match a new
    // change after wraparound. The event then stays pending permanently.
    // Later calls can still add reason bits.
    if (!eventState->Exhausted) {
        if (eventState->Sequence == UINT64_MAX - 1) {
            eventState->Exhausted = true;
            eventState->Sequence = UINT64_MAX;
            WARNING("DMA attachment %llu event sequence exhausted",
                    (unsigned long long)attachment->ID);
        } else {
            eventState->Sequence++;
        }
    }
}

// Requires Lock. Look up the ID in the current queue so an old snapshot cannot
// reach a record that has been removed or replaced.
static DMAAttachmentEventState_t*
__FindPendingEventLocked(
    _In_ DMAManager_t* manager,
    _In_ uint64_t      attachmentID)
{
    foreach (node, &manager->PendingEvents) {
        DMAAttachment_t* attachment = node->value;
        if (attachment->ID == attachmentID) {
            return &attachment->Event;
        }
    }
    return NULL;
}

oserr_t
DMAManagerSnapshotEvents(
    _In_  DMAManager_t*              manager,
    _In_  uint32_t                   capacity,
    _Out_ DMAAttachmentEventBatch_t* batchOut)
{
    uint32_t eventLimit;
    uint32_t eventCount = 0;
    bool     notifyManager;
    oserr_t  status = OS_EOK;

    MutexLock(&manager->Lock);
    if (manager->State != DMAManagerLive) {
        status = OS_ECANCELLED;
        goto unlock;
    }

    if (capacity == 0) {
        status = OS_EINVALPARAMS;
        goto unlock;
    }

    eventLimit = capacity;
    if (eventLimit > DMA_MANAGER_MAX_EVENTS) {
        eventLimit = DMA_MANAGER_MAX_EVENTS;
    }

    foreach (node, &manager->PendingEvents) {
        DMAAttachment_t* attachment;
        if (eventCount == eventLimit) {
            break;
        }

        attachment = node->value;

        batchOut->Entries[eventCount] = attachment->Event.Value;
        batchOut->Sequences[eventCount] = attachment->Event.Sequence;
        eventCount++;
    }
    batchOut->Count = eventCount;

unlock:
    notifyManager = status != OS_EOK && manager->State == DMAManagerLive &&
                    manager->PendingEvents.count != 0;
    MutexUnlock(&manager->Lock);
    if (notifyManager) {
        // The handle-set notification may have been consumed before this read failed.
        MarkHandle(manager->ID, IOSETIN);
    }
    return status;
}

oserr_t
DMAManagerFinishEvents(
    _In_ DMAManager_t*                    manager,
    _In_ const DMAAttachmentEventBatch_t* batch,
    _In_ bool                             outputsCopied)
{
    DMAAttachmentEventState_t* eventState;
    uint32_t                   eventIndex;
    bool                       notifyManager;
    oserr_t                    status = OS_EOK;

    MutexLock(&manager->Lock);
    if (manager->State != DMAManagerLive) {
        status = OS_ECANCELLED;
        goto unlock;
    }

    if (outputsCopied) {
        for (eventIndex = 0; eventIndex < batch->Count; eventIndex++) {
            eventState = __FindPendingEventLocked(manager, batch->Entries[eventIndex].Attachment);
            if (eventState == NULL || eventState->Exhausted) {
                continue;
            }
            
            if (eventState->Sequence != batch->Sequences[eventIndex]) {
                continue;
            }

            list_remove(&manager->PendingEvents, &eventState->Link);
            eventState->Queued = false;
        }
    }

unlock:
    notifyManager = manager->State == DMAManagerLive && manager->PendingEvents.count != 0;
    MutexUnlock(&manager->Lock);
    if (notifyManager) {
        // Changed events and failed copies leave work queued. Notify after
        // releasing Lock. The operation reference keeps the manager handle valid.
        MarkHandle(manager->ID, IOSETIN);
    }
    return status;
}
