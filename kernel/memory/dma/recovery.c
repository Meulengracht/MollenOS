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
#include <heap.h>
#include <string.h>

#include "attachment_private.h"

static bool
__SharesRequesterOrResetGroup(
    _In_ const DMAAttachment_t* first,
    _In_ const DMAAttachment_t* second)
{
    // Compare requester keys with requester keys, and reset keys with reset keys.
    return first->Configuration.RequesterGroup == second->Configuration.RequesterGroup ||
           first->Configuration.ResetGroup == second->Configuration.ResetGroup;
}

// Requires Lock. Reserve consecutive IDs without wrapping the counter.
// A failed update may leave unused IDs.
static oserr_t
__ReserveRecoveryGroupIDs(
    _In_  DMAManager_t* manager,
    _In_  uint32_t      count,
    _Out_ uint64_t*     firstGroupIDOut)
{
    uint64_t firstGroupID = manager->Registry.NextRecoveryGroupID;

    if (count > UINT64_MAX - firstGroupID) {
        return OS_EOVERFLOW;
    }

    manager->Registry.NextRecoveryGroupID += count;
    *firstGroupIDOut = firstGroupID;
    return OS_EOK;
}

static bool
__ContainsRecoveryGroup(
    _In_ const uint64_t* groups,
    _In_ uint32_t        groupCount,
    _In_ uint64_t        recoveryGroup)
{
    for (uint32_t i = 0; i < groupCount; i++) {
        if (groups[i] == recoveryGroup) {
            return true;
        }
    }
    return false;
}

oserr_t
DMARecoveryPrepareRegistration(
    _In_  DMAManager_t*         manager,
    _In_  DMAAttachment_t*      addedAttachment,
    _Out_ DMARecoveryUpdate_t** updateOut)
{
    DMARecoveryUpdate_t* update;
    DMAAttachment_t*     attachment;
    uint64_t             connectedGroups[DMA_MANAGER_MAX_ATTACHMENTS];
    uint64_t             mergedGroup;
    uint32_t             connectedGroupCount = 0;
    oserr_t              status;

    update = kmalloc(sizeof(DMARecoveryUpdate_t));
    if (update == NULL) {
        return OS_EOOM;
    }

    update->Count = 0;

    MutexLock(&manager->Lock);
    if (manager->State != DMAManagerLive) {
        status = OS_ECANCELLED;
        goto unlock;
    }

    // A recovery group already includes attachments connected through other
    // attachments. Find the groups that share a key with the new attachment,
    // then collect every member of those groups.
    foreach (node, &manager->Registry.Attachments) {
        attachment = node->value;
        if (__SharesRequesterOrResetGroup(attachment, addedAttachment)) {
            connectedGroups[connectedGroupCount++] = attachment->RecoveryGroup;
        }
    }

    foreach (node, &manager->Registry.Attachments) {
        attachment = node->value;
        if (__ContainsRecoveryGroup(
                connectedGroups,
                connectedGroupCount,
                attachment->RecoveryGroup)) {
            update->Entries[update->Count++].Attachment = attachment;
        }
    }

    assert(update->Count < DMA_MANAGER_MAX_ATTACHMENTS);
    update->Entries[update->Count++].Attachment = addedAttachment;

    status = __ReserveRecoveryGroupIDs(manager, 1, &mergedGroup);
    if (status != OS_EOK) {
        goto unlock;
    }

    for (uint32_t i = 0; i < update->Count; i++) {
        update->Entries[i].RecoveryGroup = mergedGroup;
    }

unlock:
    MutexUnlock(&manager->Lock);
    if (status != OS_EOK) {
        kfree(update);
        return status;
    }

    *updateOut = update;
    return OS_EOK;
}

// Requires entry zero to be the removed attachment and all groups to be zero.
// Store temporary labels from 1 through groupCount in RecoveryGroup. Scan each
// queued entry once against all candidates, keeping comparisons within N squared.
static uint32_t
__LabelRemainingRecoveryGroups(
    _In_ DMARecoveryUpdate_t* update)
{
    uint32_t pendingEntries[DMA_MANAGER_MAX_ATTACHMENTS];
    uint32_t groupCount = 0;
    uint32_t readIndex;
    uint32_t writeIndex;

    // Leave entry zero's group at zero to mark the removed attachment.
    for (uint32_t startIndex = 1; startIndex < update->Count; startIndex++) {
        if (update->Entries[startIndex].RecoveryGroup != 0) {
            continue;
        }

        update->Entries[startIndex].RecoveryGroup = ++groupCount;

        readIndex = 0;
        writeIndex = 0;
        pendingEntries[writeIndex++] = startIndex;
        while (readIndex != writeIndex) {
            uint32_t currentIndex = pendingEntries[readIndex++];
            
            for (uint32_t candidateIndex = 1; candidateIndex < update->Count; candidateIndex++) {
                if (update->Entries[candidateIndex].RecoveryGroup != 0) {
                    continue;
                }
                
                if (__SharesRequesterOrResetGroup(
                        update->Entries[currentIndex].Attachment,
                        update->Entries[candidateIndex].Attachment)) {
                    update->Entries[candidateIndex].RecoveryGroup = groupCount;
                    pendingEntries[writeIndex++] = candidateIndex;
                }
            }
        }
    }
    return groupCount;
}

oserr_t
DMARecoveryPrepareRemoval(
    _In_  DMAManager_t*         manager,
    _In_  DMAAttachment_t*      removedAttachment,
    _Out_ DMARecoveryUpdate_t** updateOut)
{
    DMARecoveryUpdate_t* update;
    DMAAttachment_t*     attachment;
    uint64_t             firstGroupID;
    uint32_t             groupCount;
    oserr_t              status;

    update = kmalloc(sizeof(DMARecoveryUpdate_t));
    if (update == NULL) {
        return OS_EOOM;
    }

    memset(update, 0, sizeof(DMARecoveryUpdate_t));
    update->Entries[0].Attachment = removedAttachment;
    update->Count = 1;

    MutexLock(&manager->Lock);
    if (manager->State != DMAManagerLive) {
        status = OS_ECANCELLED;
        goto unlock;
    }

    foreach (node, &manager->Registry.Attachments) {
        attachment = node->value;
        if (attachment != removedAttachment &&
            attachment->RecoveryGroup == removedAttachment->RecoveryGroup) {
            update->Entries[update->Count++].Attachment = attachment;
        }
    }
    MutexUnlock(&manager->Lock);

    // UpdateLock keeps the attachments valid while we calculate without Lock.
    // Events can still be posted and the service can close during this work.
    groupCount = __LabelRemainingRecoveryGroups(update);

    MutexLock(&manager->Lock);
    if (manager->State != DMAManagerLive) {
        status = OS_ECANCELLED;
        goto unlock;
    }

    status = __ReserveRecoveryGroupIDs(manager, groupCount, &firstGroupID);
    if (status != OS_EOK) {
        goto unlock;
    }

    // Convert the temporary labels to manager-local IDs. Entry zero stays zero.
    for (uint32_t i = 1; i < update->Count; i++) {
        update->Entries[i].RecoveryGroup += firstGroupID - 1;
    }

unlock:
    MutexUnlock(&manager->Lock);
    if (status != OS_EOK) {
        kfree(update);
        return status;
    }

    *updateOut = update;
    return OS_EOK;
}

void
DMARecoveryApplyUpdate(
    _In_ DMARecoveryUpdate_t* update)
{
    for (uint32_t i = 0; i < update->Count; i++) {
        update->Entries[i].Attachment->RecoveryGroup =
            update->Entries[i].RecoveryGroup;
    }
}

void
DMARecoveryDiscardUpdate(
    _InOut_ DMARecoveryUpdate_t** update)
{
    if (*update != NULL) {
        kfree(*update);
        *update = NULL;
    }
}
